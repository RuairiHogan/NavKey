#include <Windows.h>
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <wrl/client.h>
#include "UIElementScanner.h"
#include "HintOverlay.h"
#include "InputHandler.h"
#include "CursorHalo.h"
#include <chrono>
#include <thread>
#include <map>
#include <algorithm>
#include <unordered_set>
#include "global.h"

static std::vector<hint_map::HintTarget> currentTargets;
static std::vector<std::wstring> currentLabels;
static bool overlayActive = false;

// Global key-state set (already declared in global.h)
std::unordered_set<UINT> keysDown;

namespace hint_map {

    // --- Raw Input sink window state ---
    static HWND s_inputWnd = nullptr;
    static HINSTANCE s_hInst = nullptr;

    // Overlay input state (reusing your earlier fields but without hooks)
    static std::wstring typedBuffer;
    static std::vector<HintTarget> g_targets;
    static std::vector<std::wstring> g_labels;
    static std::function<void()> g_onCancel;
    static std::atomic<bool> overlayInputActive{ false };

    // App mode state (moved here from main.cpp)
    static bool insertMode = true;
    static bool ctrlDown = false;
    static bool ctrlUsedWithOtherKey = false;

    bool IsInsertMode() { return insertMode; }

    // Add (next to the existing state):
    static bool g_useLowLevelHook = true; // when true, WM_INPUT won't double-process

    bool IsOverlayActive() { return overlayInputActive.load(); }


    // Forward decl
    static LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Feed overlay with A–Z / ESC coming from Raw Input
    /*static bool FeedOverlayKey(UINT vk, bool isDown) {
        if (!overlayInputActive.load() || !isDown) return false;

        if (vk == VK_ESCAPE) {
            overlayInputActive.store(false);
            CloseHintOverlay();
            if (g_onCancel) g_onCancel();
            typedBuffer.clear();
            return true;
        }

        if (vk >= 'A' && vk <= 'Z') {
            typedBuffer += (wchar_t)vk;

            for (size_t i = 0; i < g_labels.size(); ++i) {
                if (_wcsicmp(g_labels[i].c_str(), typedBuffer.c_str()) == 0) {
                    auto& target = g_targets[i];

                    Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invoke;
                    if (SUCCEEDED(target.element->GetCurrentPattern(UIA_InvokePatternId, (IUnknown**)&invoke)) && invoke) {
                        invoke->Invoke();
                    }
                    else {
                        Microsoft::WRL::ComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;
                        if (SUCCEEDED(target.element->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, (IUnknown**)&legacy)) && legacy) {
                            legacy->DoDefaultAction();
                        }
                    }

                    overlayInputActive.store(false);
                    CloseHintOverlay();
                    if (g_onCancel) g_onCancel();
                    typedBuffer.clear();
                    return true;
                }
            }

            if (typedBuffer.length() > 3) typedBuffer.clear();
        }

        return false;
    }*/
    // Add: shared key processing for both LL hook and WM_INPUT
    static bool ProcessKeyCommonAndMaybeConsume(UINT vk, bool isDown) {
        // Update global set
        if (isDown) keysDown.insert(vk); else keysDown.erase(vk);

        // Overlay label typing (ESC / A–Z) — consume when overlay is active
        if (overlayInputActive.load()) {
            if (isDown && vk == VK_ESCAPE) {
                overlayInputActive.store(false);
                CloseHintOverlay();
                if (g_onCancel) g_onCancel();
                typedBuffer.clear();
                return true; // consumed
            }
            if (isDown && vk >= 'A' && vk <= 'Z') {
                typedBuffer += (wchar_t)vk;

                for (size_t i = 0; i < g_labels.size(); ++i) {
                    if (_wcsicmp(g_labels[i].c_str(), typedBuffer.c_str()) == 0) {
                        auto& target = g_targets[i];
                        Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invoke;
                        if (SUCCEEDED(target.element->GetCurrentPattern(UIA_InvokePatternId, (IUnknown**)&invoke)) && invoke) {
                            invoke->Invoke();
                        }
                        else {
                            Microsoft::WRL::ComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;
                            if (SUCCEEDED(target.element->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, (IUnknown**)&legacy)) && legacy) {
                                legacy->DoDefaultAction();
                            }
                        }
                        overlayInputActive.store(false);
                        CloseHintOverlay();
                        if (g_onCancel) g_onCancel();
                        typedBuffer.clear();
                        return true; // consumed
                    }
                }
                if (typedBuffer.length() > 3) typedBuffer.clear();
                return true; // letters are “for overlay” while active
            }
        }

        // Insert-mode toggle by Ctrl tap
        if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) {
            if (isDown) { ctrlDown = true; ctrlUsedWithOtherKey = false; }
            else {
                if (!ctrlUsedWithOtherKey) {
                    insertMode = !insertMode;
                    if (insertMode) {
                        HideCursorHalo();   // INSERT MODE
                    }
                    else {
                        ShowCursorHalo();   // COMMAND MODE
                    }
                    OutputDebugString(insertMode ? L"[Insert Mode ON]\n" : L"[Insert Mode OFF]\n");
                }
                ctrlDown = false;
            }
        }
        else if (isDown && ctrlDown) {
            ctrlUsedWithOtherKey = true;
        }

        // Shortcuts only in command mode
        if (!insertMode) {
            shortcut::ProcessShortcuts(keysDown);
        }

        return false; // not consumed here
    }

    // Add: entry point used by the low-level hook
    bool HandleKeyFromHook(UINT vk, bool isDown) {
        return ProcessKeyCommonAndMaybeConsume(vk, isDown);
    }


    // ---------- Raw Input sink window ----------
    void InitInputSink(HINSTANCE hInstance) {
        if (s_inputWnd) return;
        s_hInst = hInstance;

        const wchar_t CLASS_NAME[] = L"KeyboardInputSinkWindow";

        WNDCLASSW wc = {};
        wc.lpfnWndProc = InputWndProc;
        wc.hInstance = s_hInst;
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        RegisterClassW(&wc);

        // Hidden, message-only style window (not truly message-only so WM_INPUT will deliver)
        s_inputWnd = CreateWindowExW(
            0, CLASS_NAME, L"KeyboardInputSink",
            WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, s_hInst, nullptr);

        if (!s_inputWnd) {
            DWORD err = GetLastError();
            wchar_t buf[128];
            swprintf(buf, 128, L"CreateWindowExW failed, error=%lu\n", err);
            OutputDebugString(buf);
        }


        // Register for RAW keyboard input (global within the session)
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;        // Generic desktop controls
        rid.usUsage = 0x06;        // Keyboard
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = s_inputWnd;

        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            DWORD err = GetLastError();
            wchar_t buf[128];
            swprintf(buf, 128, L"RegisterRawInputDevices failed, error=%lu\n", err);
            OutputDebugString(buf);
        }

    }

    void ShutdownInputSink() {
        if (s_inputWnd) {
            DestroyWindow(s_inputWnd);
            s_inputWnd = nullptr;
        }
        UnregisterClass(L"KeyboardInputSinkWindow", s_hInst);
    }

    // Start overlay input without installing any hooks
    void StartInputHandler(HINSTANCE /*hInstance*/,
        const std::vector<HintTarget>& targets,
        const std::vector<std::wstring>& labels,
        std::function<void()> onCancel)
    {
        if (overlayInputActive.load()) return;
        g_targets = targets;
        g_labels = labels;
        g_onCancel = onCancel;
        typedBuffer.clear();
        overlayInputActive.store(true);
        OutputDebugString(L"[hint_map] Overlay input via Raw Input.\n");
    }

    void StopInputHandler() {
        if (!overlayInputActive.load()) return;
        overlayInputActive.store(false);
        typedBuffer.clear();
        OutputDebugString(L"[hint_map] Overlay Raw Input stopped.\n");
    }

    // Window proc: handles WM_INPUT and updates keysDown, insert mode, and shortcuts
    static LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_INPUT: {
            if (g_useLowLevelHook) {
                // Low-level keyboard hook is authoritative; skip WM_INPUT to avoid duplicates.
                return 0;
            }

            UINT size = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            if (!size) break;

            std::vector<BYTE> buf(size);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
                break;

            RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buf.data());
            if (ri->header.dwType != RIM_TYPEKEYBOARD) break;

            const RAWKEYBOARD& k = ri->data.keyboard;
            const USHORT vk = k.VKey;
            const bool isDown = (k.Flags & RI_KEY_BREAK) == 0;

            (void)ProcessKeyCommonAndMaybeConsume(vk, isDown);
            return 0;
        }

        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    } 

} // namespace hintmap

namespace shortcut {
    std::vector<ShortcutInternal> registeredShortcuts;

    void SendBackspaces(int count) {
        for (int i = 0; i < count; ++i) {
            keybd_event(VK_BACK, 0, 0, 0);
            keybd_event(VK_BACK, 0, KEYEVENTF_KEYUP, 0);
            Sleep(10);
        }
    }

    bool IsFocusedElementEditable() {
        Microsoft::WRL::ComPtr<IUIAutomation> automation;
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) {
            return false;
        }
        Microsoft::WRL::ComPtr<IUIAutomationElement> focusedElement;
        if (FAILED(automation->GetFocusedElement(&focusedElement)) || !focusedElement) {
            return false;
        }
        CONTROLTYPEID controlType = 0;
        if (FAILED(focusedElement->get_CurrentControlType(&controlType))) {
            return false;
        }
        return (controlType == UIA_EditControlTypeId);
    }

    bool IsPartialMatch(const std::unordered_set<UINT>& keysDown) {
        for (const auto& sc : shortcut::registeredShortcuts) {
            bool allMatchSoFar = std::all_of(sc.keys.begin(), sc.keys.end(),
                [&](UINT key) { return keysDown.count(key) > 0; });

            bool somePressed = std::any_of(sc.keys.begin(), sc.keys.end(),
                [&](UINT key) { return keysDown.count(key) > 0; });

            if (somePressed && allMatchSoFar) {
                return true;
            }
        }
        return false;
    }

    bool IsShortcut(const std::unordered_set<UINT>& keysDown) {
        for (const auto& sc : registeredShortcuts) {
            bool allPressed = std::all_of(sc.keys.begin(), sc.keys.end(),
                [&keysDown](UINT k) { return keysDown.count(k); });
            if (allPressed) return true;
        }
        return false;
    }

    void SendAltTab() {
        HWND hwnd = GetForegroundWindow();
        SetForegroundWindow(hwnd);

        INPUT inputs[4] = {};
        int i = 0;

        inputs[i].type = INPUT_KEYBOARD; inputs[i].ki.wVk = VK_MENU; inputs[i].ki.wScan = MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC); i++;
        inputs[i].type = INPUT_KEYBOARD; inputs[i].ki.wVk = VK_TAB;  inputs[i].ki.wScan = MapVirtualKey(VK_TAB, MAPVK_VK_TO_VSC); i++;
        inputs[i].type = INPUT_KEYBOARD; inputs[i].ki.wVk = VK_TAB;  inputs[i].ki.wScan = MapVirtualKey(VK_TAB, MAPVK_VK_TO_VSC); inputs[i].ki.dwFlags = KEYEVENTF_KEYUP; i++;
        inputs[i].type = INPUT_KEYBOARD; inputs[i].ki.wVk = VK_MENU; inputs[i].ki.wScan = MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC); inputs[i].ki.dwFlags = KEYEVENTF_KEYUP; i++;

        UINT sent = SendInput(i, inputs, sizeof(INPUT));
        if (sent != (UINT)i) {
            INPUT fix = {}; fix.type = INPUT_KEYBOARD; fix.ki.wVk = VK_MENU; fix.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &fix, sizeof(INPUT));
            OutputDebugString(L"SendInput for Alt+Tab failed.\n");
        }
        else {
            OutputDebugString(L"SendInput sent Alt+Tab.\n");
        }
    }

    void ScrollForegroundWindow(int direction /* +1 = up, -1 = down */) {
        HWND hwndForeground = GetForegroundWindow();
        if (!hwndForeground) return;

        RECT rect;
        if (!GetWindowRect(hwndForeground, &rect)) return;

        POINT center;
        center.x = (rect.left + rect.right) / 2;
        center.y = (rect.top + rect.bottom) / 2;
        SetCursorPos(center.x, center.y);
        Sleep(10);

        POINT clientPt = center;
        ScreenToClient(hwndForeground, &clientPt);

        HWND hwndTarget = ChildWindowFromPointEx(hwndForeground, clientPt, CWP_ALL);
        if (!hwndTarget) hwndTarget = hwndForeground;

        int delta = (direction > 0) ? 70 : -70;
        LPARAM lParam = MAKELPARAM(center.x, center.y);
        WPARAM wParam = MAKEWPARAM(0, delta);

        SendMessage(hwndTarget, WM_MOUSEWHEEL, wParam, lParam);
    }

    void RegisterShortcut(const std::vector<UINT>& keys, std::function<void()> action) {
        registeredShortcuts.push_back({ keys, action, false });
    }

    bool AreKeysPressed(const std::vector<UINT>& keys) {
        for (UINT key : keys) {
            if (!(GetAsyncKeyState(key) & 0x8000)) {
                return false;
            }
        }
        return true;
    }

    void ProcessShortcuts(const std::unordered_set<UINT>& keysDown) {
        for (auto& shortcut : registeredShortcuts) {
            bool isPressed = std::all_of(shortcut.keys.begin(), shortcut.keys.end(),
                [&keysDown](UINT key) {
                    return keysDown.count(key) > 0;
                });

            if (isPressed && !shortcut.lastPressed) {
                shortcut.action();
            }
            shortcut.lastPressed = isPressed;
        }

        // Block continuous scrolling while the hint overlay is up
        if (overlayActive) return;

        if (keysDown.size() == 2 && keysDown.count('D') && keysDown.count('F') && !keysDown.count('S')) {
            ScrollForegroundWindow(-1);
        }
        if (keysDown.size() == 2 && keysDown.count('J') && keysDown.count('K')) {
            ScrollForegroundWindow(+1);
        }
    }

    void ClearShortcuts() {
        registeredShortcuts.clear();
    }

    void InitShortcuts(HINSTANCE hInstance) {
        shortcut::RegisterShortcut({ 'S', 'D', 'F' }, [hInstance]() {
            OutputDebugString(L"[hint_map] ActivateHintMode called.\n");

            if (overlayActive) {
                MessageBox(NULL, L"[hint_map] Overlay is still active.\n", L"Debug", MB_OK | MB_ICONINFORMATION);
                return;
            }

            // Clear previous keyboard state before opening hints
            keysDown.clear();

            currentTargets = hint_map::GetClickableElements();
            OutputDebugString(L"[hint_map] GetClickableElements called.\n");

            if (currentTargets.empty()) {
                OutputDebugString(L"[hint_map] currentTargets is empty.\n");
                return;
            }

            std::vector<RECT> rects;
            for (const auto& t : currentTargets) rects.push_back(t.rect);

            currentLabels = hint_map::GenerateHintLabels((int)currentTargets.size());
            OutputDebugString(L"[hint_map] GenerateHintLabels called.\n");

            hint_map::ShowHintOverlay(hInstance, currentTargets, currentLabels);
            OutputDebugString(L"[hint_map] ShowHintOverlay called.\n");

            overlayActive = true;

            hint_map::StartInputHandler(hInstance, currentTargets, currentLabels, []() {
                overlayActive = false;
                hint_map::StopInputHandler();
                });
            });

        shortcut::RegisterShortcut({ 'Q', 'W', 'E' }, []() {
            MessageBox(nullptr, L"QWE shortcut triggered!", L"Info", MB_OK);
            });

        shortcut::RegisterShortcut({ 'G', 'H' }, []() {
            SendAltTab();
            OutputDebugString(L"[hint_map] SendAltTab called.\n");
            });
    }

} // namespace shortcut
