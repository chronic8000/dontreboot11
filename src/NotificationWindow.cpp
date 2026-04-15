#include "NotificationWindow.h"
#include <windows.h>
#include <shellapi.h>

const wchar_t CLASS_NAME[] = L"SessionGuardNotification";

void NotificationWindow::RegisterClass(HINSTANCE hInstance) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 23, 42)); // Deep Slate / Navy
    ::RegisterClassW(&wc);
}

void NotificationWindow::Show(const std::wstring& title, const std::wstring& message, NotificationType type) {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Position: Bottom-Right
    int width = 350;
    int height = 120;
    int x = screenWidth - width - 20;
    int y = screenHeight - height - 60; // Above taskbar

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        CLASS_NAME, L"SessionGuard Notification",
        WS_POPUP,
        x, y, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    SetLayeredWindowAttributes(hwnd, 0, 240, LWA_ALPHA);

    NotificationData* data = new NotificationData();
    data->title = title;
    data->message = message;
    data->type = type;
    data->hTitleFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    data->hMessageFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Message loop to keep the window alive and responsive on the current thread
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}


LRESULT CALLBACK NotificationWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NotificationData* data = (NotificationData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        RECT rect;
        GetClientRect(hwnd, &rect);
        
        // Draw Accent Border (Left side)
        RECT accent = { 0, 0, 5, rect.bottom };
        COLORREF accentColor = (data->type == NotificationWindow::TYPE_ALERT) ? RGB(239, 68, 68) : RGB(14, 165, 233); 
        HBRUSH hAccentBrush = CreateSolidBrush(accentColor);
        FillRect(hdc, &accent, hAccentBrush);
        DeleteObject(hAccentBrush);

        SetBkMode(hdc, TRANSPARENT);
        
        // Title
        SelectObject(hdc, data->hTitleFont);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT titleRect = { 15, 15, rect.right - 40, 45 };
        DrawTextW(hdc, data->title.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

        // Message
        SelectObject(hdc, data->hMessageFont);
        SetTextColor(hdc, RGB(203, 213, 225)); // Slate-300
        RECT msgRect = { 15, 50, rect.right - 20, rect.bottom - 10 };
        DrawTextW(hdc, data->message.c_str(), -1, &msgRect, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // Draw Close Button [X]
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT closeRect = { rect.right - 30, 10, rect.right - 10, 30 };
        DrawTextW(hdc, L"X", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT rect;
        GetClientRect(hwnd, &rect);
        RECT closeHit = { rect.right - 40, 0, rect.right, 40 };
        
        if (PtInRect(&closeHit, pt)) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_DESTROY:
        if (data) {
            DeleteObject(data->hTitleFont);
            DeleteObject(data->hMessageFont);
            delete data;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
