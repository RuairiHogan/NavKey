// UIElementScanner.cpp

#include "UIElementScanner.h"
#include <Windows.h>
#include <UIAutomation.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>
#include <comdef.h>
#include <unordered_set>

using Microsoft::WRL::ComPtr;



namespace hint_map {

    // a set of control types that are generally not useful for clickable elements
    static const std::unordered_set<LONG> controlTypesToSkip = {
        UIA_PaneControlTypeId,
        UIA_GroupControlTypeId,
        UIA_TextControlTypeId,
        UIA_CustomControlTypeId,
        UIA_DocumentControlTypeId,
        UIA_ListItemControlTypeId
    };


    std::vector<HintTarget> GetClickableElements() {
        std::vector<HintTarget> targets;

        // Initialize COM as multithreaded (MTA is generally safe for UIAutomation client usage here)
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            OutputDebugStringW(L"[hint_map] CoInitializeEx failed in GetClickableElements\n");
            return targets;
        }

        Microsoft::WRL::ComPtr<IUIAutomation> automation;
        hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (FAILED(hr) || !automation) {
            OutputDebugStringW(L"[hint_map] CoCreateInstance for CUIAutomation failed\n");
            CoUninitialize();
            return targets;
        }

        // Get the root element: try foreground window first, fallback to root
        Microsoft::WRL::ComPtr<IUIAutomationElement> root;
        HWND foregroundHwnd = GetForegroundWindow();
        if (foregroundHwnd) {
            hr = automation->ElementFromHandle(foregroundHwnd, &root);
            if (FAILED(hr) || !root) {
                OutputDebugStringW(L"[hint_map] ElementFromHandle failed, falling back to GetRootElement\n");
                root.Reset();
            }
        }

        RECT foregroundRect;
        GetWindowRect(foregroundHwnd, &foregroundRect);


        if (!root) {
            hr = automation->GetRootElement(&root);
            if (FAILED(hr) || !root) {
                OutputDebugStringW(L"[hint_map] GetRootElement failed\n");
                CoUninitialize();
                return targets;
            }
        }

        // Build clickable condition: InvokePatternAvailable OR LegacyIAccessibleAvailable OR KeyboardFocusable
        //ComPtr<IUIAutomationCondition> invokeCond;
        //automation->CreatePropertyCondition(UIA_IsInvokePatternAvailablePropertyId, _variant_t(true), &invokeCond);

        //ComPtr<IUIAutomationCondition> focusableCond;
        //automation->CreatePropertyCondition(UIA_IsKeyboardFocusablePropertyId, _variant_t(true), &focusableCond);

        //// Combine
        //ComPtr<IUIAutomationCondition> clickableCond;
        //automation->CreateOrCondition(invokeCond.Get(), focusableCond.Get(), &clickableCond);

        //// Add IsOffscreen == false
        //ComPtr<IUIAutomationCondition> offscreenFalseCond;
        //automation->CreatePropertyCondition(UIA_IsOffscreenPropertyId, _variant_t(false), &offscreenFalseCond);

        //// Final combined condition
        //ComPtr<IUIAutomationCondition> finalCond;
        //automation->CreateAndCondition(clickableCond.Get(), offscreenFalseCond.Get(), &finalCond); 
        // Filter to elements that are:
   // - InvokePatternAvailable OR SelectionItemPatternAvailable
   // - NOT offscreen
   // - Keyboard focusable

        ComPtr<IUIAutomationCondition> invokeCond, focusableCond, selectCond;
        automation->CreatePropertyCondition(UIA_IsInvokePatternAvailablePropertyId, _variant_t(true), &invokeCond);
        automation->CreatePropertyCondition(UIA_IsKeyboardFocusablePropertyId, _variant_t(true), &focusableCond);
        automation->CreatePropertyCondition(UIA_IsSelectionItemPatternAvailablePropertyId, _variant_t(true), &selectCond);

        ComPtr<IUIAutomationCondition> interactionCond;
        automation->CreateOrCondition(invokeCond.Get(), selectCond.Get(), &interactionCond);

        ComPtr<IUIAutomationCondition> clickableCond;
        automation->CreateOrCondition(interactionCond.Get(), focusableCond.Get(), &clickableCond);

        ComPtr<IUIAutomationCondition> offscreenFalseCond;
        automation->CreatePropertyCondition(UIA_IsOffscreenPropertyId, _variant_t(false), &offscreenFalseCond);

        ComPtr<IUIAutomationCondition> finalCond;
        automation->CreateAndCondition(clickableCond.Get(), offscreenFalseCond.Get(), &finalCond);

        ComPtr<IUIAutomationCacheRequest> cacheRequest;
        hr = automation->CreateCacheRequest(&cacheRequest);
        if (FAILED(hr) || !cacheRequest) {
            OutputDebugStringW(L"[hint_map] CreateCacheRequest failed\n");
            CoUninitialize();
            return targets;
        }

        // Add properties you want cached:
        cacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);
        cacheRequest->AddProperty(UIA_ControlTypePropertyId);
        cacheRequest->AddProperty(UIA_IsOffscreenPropertyId);

        // Query matching elements in subtree
        Microsoft::WRL::ComPtr<IUIAutomationElementArray> found;
        hr = root->FindAllBuildCache(TreeScope_Descendants, finalCond.Get(), cacheRequest.Get(), &found);
        if (FAILED(hr) || !found) {
            OutputDebugStringW(L"[hint_map] FindAllBuildCache failed\n");
            CoUninitialize();
            return targets;
        }

        int length = 0;
        found->get_Length(&length);

        wchar_t buf[100];
        swprintf(buf, 100, L"[hint_map] Elements found: %d\n", length);
        OutputDebugStringW(buf);

        for (int i = 0; i < length; ++i) {
            ComPtr<IUIAutomationElement> element;
            if (FAILED(found->GetElement(i, &element)) || !element) continue;

            RECT r{};
            if (SUCCEEDED(element->get_CachedBoundingRectangle(&r))) {
                OutputDebugString(L"Got cached bounding Rectangle");
                RECT intersect;
                if (!IntersectRect(&intersect, &foregroundRect, &r)) continue;

                LONG width = r.right - r.left;
                LONG height = r.bottom - r.top;

                if (width > 5 && height > 5 && width < 1000 && height < 1000) {
                    int controlType = 0;
                    if (SUCCEEDED(element->get_CachedControlType(&controlType))) {
                        OutputDebugString(L"Code found a cached control type");
                        if (controlTypesToSkip.count(controlType)) continue;

                        HintTarget target;
                        target.rect = r;
                        target.element = element;
                        target.controlTypeId = controlType;
                        targets.push_back(target);
                        //int length = 0;
                        //found->get_Length(&length);
                        //for (int i = 0; i < length; ++i) {

                        //    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                        //    if (FAILED(found->GetElement(i, &element)) || !element) continue;

                        //    BOOL isOffscreen = TRUE;
                        //    element->get_CurrentIsOffscreen(&isOffscreen);
                        //    if (isOffscreen) continue;

                        //    RECT r{};
                        //    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&r))) {

                        //        // FILTER: Only elements inside the active window
                        //        RECT intersect;
                        //        if (!IntersectRect(&intersect, &foregroundRect, &r)) continue;

                        //        LONG width = r.right - r.left;
                        //        LONG height = r.bottom - r.top;

                        //        if (width > 5 && height > 5 &&
                        //            width < 1000 && height < 1000){

                        //            
                        //            int controlType = 0;
                        //            element->get_CurrentControlType(&controlType);

                        //            // Skip certain generic or noisy control types
                        //            if (controlTypesToSkip.count(controlType)) {
                        //                continue;  // don't add this element
                        //            }

                        //            HintTarget target;
                        //            target.rect = r;
                        //            target.element = element;
                        //            target.controlTypeId = controlType; // Store control type
                        //            targets.push_back(target);


                    }
                }
            }
        }

        CoUninitialize();
        return targets;
    }

}
