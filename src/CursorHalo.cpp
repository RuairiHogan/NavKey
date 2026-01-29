#include <windows.h>
#include <ShellScalingApi.h>
#include <math.h>

#pragma comment(lib, "Shcore.lib")

// ============================================================
// Globals
// ============================================================

static HWND   haloHwnd = nullptr;
static HDC    haloDC = nullptr;
static HBITMAP haloBmp = nullptr;

static const UINT BASE_SIZE = 36;   // halo diameter
static const float HALO_ALPHA = 0.8f;

// ============================================================
// Helpers
// ============================================================

static float GetCursorScale()
{
    POINT pt;
    GetCursorPos(&pt);

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    return dpiX / 96.0f;
}

static void CleanupBitmap()
{
    if (haloBmp) {
        DeleteObject(haloBmp);
        haloBmp = nullptr;
    }
}

// ============================================================
// Halo Rendering (REAL TRANSPARENCY)
// ============================================================

static void DrawHaloBitmap(UINT size)
{
    CleanupBitmap();

    if (!haloDC)
        haloDC = CreateCompatibleDC(nullptr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -((LONG)size); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    haloBmp = CreateDIBSection(
        haloDC,
        &bmi,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0
    );

    SelectObject(haloDC, haloBmp);
    ZeroMemory(bits, size * size * 4);

    float cx = size / 2.0f;
    float cy = size / 2.0f;
    float radius = size * 0.35f;

    for (UINT y = 0; y < size; y++) {
        for (UINT x = 0; x < size; x++) {

            float dx = x - cx;
            float dy = y - cy;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist > radius)
                continue;

            float t = 1.0f - (dist / radius);
            BYTE a = (BYTE)(255 * t * HALO_ALPHA);

            UINT32* px = (UINT32*)bits + (y * size + x);
            *px =
                (a << 24) |   // A
                (255 << 16) | // R
                (40 << 8) | // G
                (40);         // B
        }
    }
}

static void UpdateHaloWindow(HWND hwnd, POINT pt, UINT size)
{
    DrawHaloBitmap(size);

    SIZE wndSize = { (LONG)size, (LONG)size };
    POINT src = { 0, 0 };
    POINT dst = {
        pt.x - (int)size / 2,
        pt.y - (int)size / 2
    };

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(
        hwnd,
        nullptr,
        &dst,
        &wndSize,
        haloDC,
        &src,
        0,
        &blend,
        ULW_ALPHA
    );
}

// ============================================================
// Window Proc
// ============================================================

LRESULT CALLBACK HaloWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
        SetTimer(hwnd, 1, 16, nullptr); // ~60 FPS
        return 0;

    case WM_TIMER: {
        POINT pt;
        GetCursorPos(&pt);

        UINT size = (UINT)(BASE_SIZE * GetCursorScale());
        UpdateHaloWindow(hwnd, pt, size);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        CleanupBitmap();
        if (haloDC) {
            DeleteDC(haloDC);
            haloDC = nullptr;
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================
// Window Class
// ============================================================

static bool haloClassRegistered = false;

void RegisterCursorHaloClass(HINSTANCE hInstance)
{
    if (haloClassRegistered)
        return;

    WNDCLASS wc = {};
    wc.lpfnWndProc = HaloWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CursorHalo";
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;

    RegisterClass(&wc);
    haloClassRegistered = true;
}

// ============================================================
// Public API (UNCHANGED)
// ============================================================

void ShowCursorHalo()
{
    if (haloHwnd)
        return;

    HINSTANCE hInst = GetModuleHandle(nullptr);
    RegisterCursorHaloClass(hInst);

    haloHwnd = CreateWindowEx(
        WS_EX_LAYERED |
        WS_EX_TRANSPARENT |
        WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW,
        L"CursorHalo",
        nullptr,
        WS_POPUP,
        0, 0, 1, 1,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    ShowWindow(haloHwnd, SW_SHOW);
}

void HideCursorHalo()
{
    if (!haloHwnd)
        return;

    DestroyWindow(haloHwnd);
    haloHwnd = nullptr;
}
