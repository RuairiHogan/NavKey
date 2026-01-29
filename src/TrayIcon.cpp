#include "TrayIcon.h"
#include "../resources/resource.h"  // Your icon resource header
#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ICON 1001
#define ID_MENU_EXIT 2001

static NOTIFYICONDATAW g_nid = {};
static HWND g_hwnd = nullptr;
static HINSTANCE g_hInst = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void InitTrayIcon(HINSTANCE hInstance) {
    g_hInst = hInstance;

    const wchar_t CLASS_NAME[] = L"TrayIconHiddenWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, CLASS_NAME, L"TrayIconApp", 0, 0, 0, 0, 0,
        nullptr, nullptr, g_hInst, nullptr);

    if (!g_hwnd) {
        MessageBox(nullptr, L"Failed to create hidden window!", L"Error", MB_OK);
        return;
    }

    //// Register for raw keyboard input (global to this desktop session)
    //RAWINPUTDEVICE rid{};
    //rid.usUsagePage = 0x01;   // Generic desktop controls
    //rid.usUsage = 0x06;   // Keyboard
    //rid.dwFlags = RIDEV_INPUTSINK;   // receive input even when not focused
    //rid.hwndTarget = g_hwnd;

    //if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
    //    MessageBox(nullptr, L"RegisterRawInputDevices failed", L"Error", MB_OK | MB_ICONERROR);
    //}

    HICON hIcon = (HICON)LoadImageW(
        g_hInst,
        MAKEINTRESOURCE(IDI_ICON1),
        IMAGE_ICON,
        16, 16,
        LR_DEFAULTCOLOR);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hIcon;
    wcscpy_s(g_nid.szTip, L"Keyboard Fluency App");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void CleanupTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon)
        DestroyIcon(g_nid.hIcon);

    if (g_hwnd)
        DestroyWindow(g_hwnd);

    UnregisterClass(L"TrayIconHiddenWindow", g_hInst);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    InsertMenuW(hMenu, -1, MF_BYPOSITION, ID_MENU_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hwnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            MessageBox(
                nullptr,
                L"Keyboard Shortcuts\n\n"
                L"S + D + F   – Enter hint mode\n"
                L"ESC         – Exit hint mode\n"
                L"A–Z         – Select hint\n"
                L"G + H       – Switch window (Alt+Tab)\n"
                L"Ctrl        - Toggle Insert Mode\n\n"
                L"Ctrl shortcuts always pass through",
                L"Shortcut Project",
                MB_OK | MB_ICONINFORMATION
            );
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_MENU_EXIT) {
            DestroyWindow(hwnd);  // This will trigger WM_DESTROY
        }
        break;

    case WM_DESTROY:
        CleanupTrayIcon();      // Remove the tray icon here
        PostQuitMessage(0);     // Signal message loop to quit
		app_is_running = false; // Set the running flag to false
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

