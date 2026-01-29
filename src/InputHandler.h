#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_set>
#include "UIElementScanner.h"

namespace hint_map {

    // Create/Destroy the hidden Raw Input sink window and register for WM_INPUT
    void InitInputSink(HINSTANCE hInstance);
    void ShutdownInputSink();

    // Start/stop overlay input handling (no hooks anymore; Raw Input will call into us)
    void StartInputHandler(HINSTANCE hInstance,
        const std::vector<HintTarget>& targets,
        const std::vector<std::wstring>& labels,
        std::function<void()> onCancel);

    void StopInputHandler();

    // Query app mode (insert vs command)
    bool IsInsertMode();

    // Add: called by the low-level keyboard hook to feed key activity.
// Returns true if the key was consumed (e.g., overlay label typing).
    bool HandleKeyFromHook(UINT vk, bool isDown);

    // Add: query whether the overlay is currently taking label input
    bool IsOverlayActive();

}

namespace shortcut {

    struct ShortcutInternal {
        std::vector<UINT> keys;
        std::function<void()> action;
        bool lastPressed = false;
    };

    void InitShortcuts(HINSTANCE hInstance);
    void RegisterShortcut(const std::vector<UINT>& keys, std::function<void()> action);
    void ProcessShortcuts(const std::unordered_set<UINT>& keysDown);
    void ClearShortcuts();

    extern std::vector<ShortcutInternal> registeredShortcuts;

}
