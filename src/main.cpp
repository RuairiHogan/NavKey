// main.cpp
#include <Windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <sstream>
#include <vector>
#include "UIElementScanner.h"
#include "HintOverlay.h"
#include "InputHandler.h"
#include <thread>
#include <chrono>
#include "TrayIcon.h"
#include <unordered_set>
#include <algorithm>
#include "global.h"
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")


using namespace hint_map;

static HHOOK g_suppressHook = nullptr;

static LRESULT CALLBACK SuppressKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_suppressHook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    if (!isDown && !isUp) {
        return CallNextHookEx(g_suppressHook, nCode, wParam, lParam);
    }

    // Ignore injected (SendInput) so we don't eat our own keystrokes.
    const bool injected = (k->flags & LLKHF_INJECTED) || (k->flags & LLKHF_LOWER_IL_INJECTED);
    if (injected) return CallNextHookEx(g_suppressHook, nCode, wParam, lParam);

    const UINT vk = static_cast<UINT>(k->vkCode);

    // --- HARD GUARANTEE: In command mode, swallow EVERYTHING except Ctrl ---
    if (!hint_map::IsInsertMode()) {
        const bool isCtrl = (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL);

        // Still update our internal state (shortcuts, overlay, etc.)
        (void)hint_map::HandleKeyFromHook(vk, isDown);

        if (!isCtrl) {
            // Swallow S, D, F (and any other non-Ctrl keys) so nothing "prints".
            return 1;
        }

        // Let Ctrl through (needed for Ctrl-tap to toggle modes)
        return CallNextHookEx(g_suppressHook, nCode, wParam, lParam);
    }

    // --- Insert mode: normal behavior ---
    // Feed our logic (this may consume overlay A–Z/ESC, etc.)
    bool consumed = hint_map::HandleKeyFromHook(vk, isDown);
    if (consumed) return 1;

    return CallNextHookEx(g_suppressHook, nCode, wParam, lParam);
}




bool app_is_running = true;

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
)
{
	// Set DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Tray Icon Initialization
    InitTrayIcon(hInstance);

    // Initialize the dedicated Raw Input sink window
    hint_map::InitInputSink(hInstance);

    // Initialize shortcut handling 
    shortcut::InitShortcuts(hInstance);

    // Install suppression hook (for non-insert mode)
    g_suppressHook = SetWindowsHookEx(WH_KEYBOARD_LL, SuppressKeyboardProc, GetModuleHandle(NULL), 0);
    if (!g_suppressHook) {
        MessageBox(NULL, L"Failed to install suppression hook.", L"Error", MB_ICONERROR);
    }

    while (app_is_running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Optional: You can omit this because WM_INPUT already triggers it.
        // if (!hint_map::IsInsertMode()) {
        //     shortcut::ProcessShortcuts(keysDown);
        // }

        Sleep(10);
    }

    // Cleanup
    shortcut::ClearShortcuts();
    hint_map::ShutdownInputSink();
    if (g_suppressHook) {
        UnhookWindowsHookEx(g_suppressHook);
        g_suppressHook = nullptr;
    }
    CleanupTrayIcon();

    return 0;
}
