#include "HintOverlay.h"
#include <Windows.h>
#include <UIAutomation.h>
#include <UIAutomationClient.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <d2d1.h>
#include <dwrite.h>
#include "UIElementScanner.h"
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

namespace hint_map {

    static HWND overlayWnd = nullptr;

    COLORREF GetColorForControlType(LONG controlTypeId) {
        switch (controlTypeId) {
        case UIA_ButtonControlTypeId:         return RGB(255, 220, 220); // Light red
        case UIA_EditControlTypeId:           return RGB(220, 255, 220); // Light green
        case UIA_HyperlinkControlTypeId:      return RGB(220, 220, 255); // Light blue
        case UIA_ListItemControlTypeId:       return RGB(255, 255, 200); // Light yellow
        case UIA_MenuItemControlTypeId:       return RGB(240, 200, 255); // Light purple
        default:                              return RGB(200, 200, 200); // Default gray
        }
    }

    LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        // Static shared D2D/DWrite objects
        static ID2D1Factory* d2dFactory = nullptr;
        static IDWriteFactory* dwriteFactory = nullptr;
        static ID2D1HwndRenderTarget* renderTarget = nullptr;
        static IDWriteTextFormat* textFormat = nullptr;
        static std::unordered_map<LONG, ID2D1SolidColorBrush*> brushCache;
        int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);

        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            RECT monitorRect = mi.rcMonitor;

            // DPI scaling
            UINT dpiX, dpiY;
            GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            float dpiScale = dpiX / 96.0f;


            // Initialize factories/render target if not yet (same as before)...
            if (!d2dFactory) {
                D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory);
            }
            if (!dwriteFactory) {
                DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwriteFactory));
            }
            if (!renderTarget) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
                D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
                D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, size);
                d2dFactory->CreateHwndRenderTarget(props, hwndProps, &renderTarget);
            }
            if (!textFormat) {
                dwriteFactory->CreateTextFormat(
                    L"Segoe UI",
                    nullptr,
                    DWRITE_FONT_WEIGHT_BOLD,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    11.0f * dpiScale,                  
                    L"",
                    &textFormat
                );
                textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }



            // Begin draw
            renderTarget->BeginDraw();

            // Clear to magenta (color-keyed transparent)
            renderTarget->Clear(D2D1::ColorF(1.0f, 0.0f, 1.0f)); // magenta

			// Create brushes
            ID2D1SolidColorBrush* borderBrush = nullptr;
            renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &borderBrush);

            ID2D1SolidColorBrush* blackTextBrush = nullptr;
            renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &blackTextBrush);

            auto* hintTargets = reinterpret_cast<std::vector<HintTarget>*>(GetProp(hwnd, L"HINTTARGETS"));
            auto* labels = reinterpret_cast<std::vector<std::wstring>*>(GetProp(hwnd, L"LABELS"));

            if (hintTargets && labels) {
                /*const float padX = 2.0f;
                const float padY = 1.0f;
                const float cornerRadius = 4.0f;*/
                const float BASE_PAD_X = 2.0f;
                const float BASE_PAD_Y = 1.0f;
                const float BASE_RADIUS = 4.0f;
                const float BASE_GAP = 6.0f;

                float padX = BASE_PAD_X * dpiScale;
                float padY = BASE_PAD_Y * dpiScale;
                float cornerRadius = BASE_RADIUS * dpiScale;


                for (size_t i = 0; i < hintTargets->size(); ++i) {
                    const HintTarget& target = (*hintTargets)[i];
                    const std::wstring& label = labels->at(i);
                    int controlTypeId = target.controlTypeId;

                    if (label.empty()) continue;

                    // Layout text
                    IDWriteTextLayout* textLayout = nullptr;
                    dwriteFactory->CreateTextLayout(
                        label.c_str(),
                        (UINT32)label.size(),
                        textFormat,
                        1000.0f,
                        100.0f,
                        &textLayout
                    );

                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(textLayout->GetMetrics(&metrics))) {
                        float boxW = metrics.width + padX * 2;
                        float boxH = metrics.height + padY * 2;

                        float gap = BASE_GAP * dpiScale;

                        //const float gap = 6.0f; // space between target and label
						// Base virtual position on target rect
                        float boxX = static_cast<float>(target.rect.left - virtualLeft) + 8.0f;
                        float boxY = static_cast<float>(target.rect.top - virtualTop) - gap; // above rect

                        if ((target.rect.top - gap) < 0) {
                            boxY = static_cast<float>(target.rect.top - virtualTop) + gap - 2.0f;
                        }

                        D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
                            D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH),
                            cornerRadius,
                            cornerRadius
                        );

                        // Get background color for this control type
                        //COLORREF bgColor = GetColorForControlType(target.controlTypeId);
                        ID2D1SolidColorBrush* bgBrush = nullptr;

                        auto it = brushCache.find(controlTypeId);

                        if (it != brushCache.end()) {
                            bgBrush = it->second; // Use cached
                        }
                        else {
                            COLORREF bgColor = GetColorForControlType(controlTypeId);

                            float r = ((bgColor >> 0) & 0xFF) / 255.0f;
                            float g = ((bgColor >> 8) & 0xFF) / 255.0f;
                            float b = ((bgColor >> 16) & 0xFF) / 255.0f;
                            if (SUCCEEDED(renderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b), &bgBrush))) {
                                brushCache[controlTypeId] = bgBrush; // Cache for future use
                            }
                            else {
                                bgBrush = nullptr; // Failed to create brush
							}
                        }

                        // Draw background and border
                        renderTarget->FillRoundedRectangle(roundedRect, bgBrush);

                        renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                        //renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                        renderTarget->DrawRoundedRectangle(roundedRect, borderBrush, 1.2f);
                        renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                        // Draw text
                        renderTarget->DrawTextLayout(D2D1::Point2F(boxX + padX, boxY + padY), textLayout, blackTextBrush);
                    }

                    if (textLayout) {
                        textLayout->Release();
                    }
                }
            }

            if (borderBrush) borderBrush->Release();
            if (blackTextBrush) blackTextBrush->Release();
            renderTarget->EndDraw();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            RemoveProp(hwnd, L"HINTTARGETS");
            RemoveProp(hwnd, L"LABELS");

            for (auto& pair : brushCache) {
                if (pair.second) {
                    pair.second->Release();
                    pair.second = nullptr;
                }
            }
            brushCache.clear();

            if (renderTarget) { renderTarget->Release(); renderTarget = nullptr; }
            if (textFormat) { textFormat->Release(); textFormat = nullptr; }
            if (dwriteFactory) { dwriteFactory->Release(); dwriteFactory = nullptr; }
            if (d2dFactory) { d2dFactory->Release(); d2dFactory = nullptr; }
            break;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }


    void ShowHintOverlay(HINSTANCE hInstance, const std::vector<HintTarget>& hintTargets, const std::vector<std::wstring>& labels) {
        if (overlayWnd) return;

        int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);

        const wchar_t CLASS_NAME[] = L"HintOverlayWindow";

        WNDCLASS wc = {};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        RegisterClass(&wc);

        HWND hwnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
            CLASS_NAME,
            NULL,
            WS_POPUP,
            virtualLeft, virtualTop, virtualWidth, virtualHeight,
            NULL, NULL, hInstance, NULL
        );

        if (!hwnd) {
            MessageBox(NULL, L"Failed to create overlay window!", L"Error", MB_OK | MB_ICONERROR);
            return;
		}
        SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);
        SetProp(hwnd, L"HINTTARGETS", (HANDLE)&hintTargets);
        SetProp(hwnd, L"LABELS", (HANDLE)&labels);

        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        overlayWnd = hwnd;
    }

    void CloseHintOverlay() {
        if (overlayWnd) {
            DestroyWindow(overlayWnd);
            overlayWnd = nullptr;
        }
    }

    std::vector<std::wstring> GenerateHintLabels(int count) {
        const std::wstring singles = L"EMCGHWLP";    // single-letter hints, used first
        const std::wstring doubles = L"ASDFJKIO";      // prefixes for two-letter hints

        const std::wstring allLetters = singles + doubles;


        std::vector<std::wstring> labels;
        labels.reserve(count);

        // 1. Take single-letter codes first
        for (wchar_t c : singles) {
            if ((int)labels.size() >= count) break;
            labels.emplace_back(1, c);
        }
        if ((int)labels.size() >= count) return labels;

        // 2. Then two-letter codes with both from doubles
        for (wchar_t prefix : doubles) {
            for (wchar_t suffix : doubles) {
                if ((int)labels.size() >= count) break;
                std::wstring lab;
                lab.push_back(prefix);
                lab.push_back(suffix);
                labels.push_back(lab);
            }
            if ((int)labels.size() >= count) break;
        }
        if ((int)labels.size() >= count) return labels;

        // 3. Then three-letter codes with doubles then singles then all
        for (wchar_t prefix : doubles) {
            for (wchar_t mid : singles) {
                for (wchar_t suffix : allLetters) {
                    if ((int)labels.size() >= count) break;
                    std::wstring lab;
                    lab.push_back(prefix);
                    lab.push_back(mid);
                    lab.push_back(suffix);
                    labels.push_back(lab);
                }
                if ((int)labels.size() >= count) break;
            }
            if ((int)labels.size() >= count) break;
        }

        // 4. Then four-letter codes with doubles + singles + all + all
        for (wchar_t prefix : doubles) {
            for (wchar_t mid1 : singles) {
                for (wchar_t mid2 : allLetters) {
                    for (wchar_t suffix : allLetters) {
                        if ((int)labels.size() >= count) break;
                        std::wstring lab;
                        lab.push_back(prefix);
                        lab.push_back(mid1);
                        lab.push_back(mid2);
                        lab.push_back(suffix);
                        labels.push_back(lab);
                    }
                    if ((int)labels.size() >= count) break;
                }
                if ((int)labels.size() >= count) break;
            }
            if ((int)labels.size() >= count) break;
        }
        if ((int)labels.size() <= count) {
            std::wstring msg = L"[hint_map] Warning: Not enough labels generated, expected at least " +
                std::to_wstring(count) + L", got " +
                std::to_wstring(labels.size()) + L".\n";
            OutputDebugString(msg.c_str());
        }
        return labels;
    }

}
