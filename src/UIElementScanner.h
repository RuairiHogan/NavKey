// UIElementScanner.h
#pragma once

#include <Windows.h>
#include <vector>
#include <UIAutomation.h>
#include <wrl/client.h>

namespace hint_map {

    struct HintTarget {
        RECT rect;
        Microsoft::WRL::ComPtr<IUIAutomationElement> element;
        int controlTypeId = 0; // <-- Add this line
    };

    std::vector<HintTarget> GetClickableElements();

}
