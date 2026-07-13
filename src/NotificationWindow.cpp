#include "NotificationWindow.h"
#include "../resources/resource.h"
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
    int height = (type == TYPE_CONSENT) ? 180 : 120; // Taller for buttons
    int x = screenWidth - width - 20;
    int y = screenHeight - height - 60; // Above taskbar

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        CLASS_NAME, L"Don't Reboot 11 Notification",
        WS_POPUP,
        x, y, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    SetLayeredWindowAttributes(hwnd, 0, 245, LWA_ALPHA);

    NotificationData* data = new NotificationData();
    data->title = title;
    data->message = message;
    data->type = type;
    data->hTitleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    data->hMessageFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    
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
        
        // Draw Background
        HBRUSH hBg = CreateSolidBrush(RGB(15, 23, 42));
        FillRect(hdc, &rect, hBg);
        DeleteObject(hBg);

        // Draw Accent Border (Left side)
        RECT accent = { 0, 0, 5, rect.bottom };
        COLORREF accentColor = RGB(14, 165, 233); // Info Blue
        if (data->type == TYPE_ALERT) accentColor = RGB(239, 68, 68); // Alert Red
        if (data->type == TYPE_CONSENT) accentColor = RGB(245, 158, 11); // Amber
        
        HBRUSH hAccentBrush = CreateSolidBrush(accentColor);
        FillRect(hdc, &accent, hAccentBrush);
        DeleteObject(hAccentBrush);

        SetBkMode(hdc, TRANSPARENT);
        
        // Title
        SelectObject(hdc, data->hTitleFont);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT titleRect = { 15, 12, rect.right - 40, 42 };
        DrawTextW(hdc, data->title.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

        // Message
        SelectObject(hdc, data->hMessageFont);
        SetTextColor(hdc, RGB(203, 213, 225)); // Slate-300
        RECT msgRect = { 15, 45, rect.right - 20, rect.bottom - 60 };
        DrawTextW(hdc, data->message.c_str(), -1, &msgRect, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // Draw Buttons if TYPE_CONSENT
        if (data->type == TYPE_CONSENT) {
            // Button 1: Reboot Now (Primary)
            RECT btn1 = { 15, rect.bottom - 45, 160, rect.bottom - 15 };
            HBRUSH hBtn1 = CreateSolidBrush(RGB(30, 41, 59));
            FillRect(hdc, &btn1, hBtn1);
            DeleteObject(hBtn1);
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawTextW(hdc, L"Reboot Now", -1, &btn1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Button 2: Later (Secondary)
            RECT btn2 = { 175, rect.bottom - 45, 320, rect.bottom - 15 };
            HBRUSH hBtn2 = CreateSolidBrush(RGB(30, 41, 59));
            FillRect(hdc, &btn2, hBtn2);
            DeleteObject(hBtn2);
            SetTextColor(hdc, RGB(148, 163, 184));
            DrawTextW(hdc, L"Remind Later", -1, &btn2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Draw Close Button [X]
        SetTextColor(hdc, RGB(100, 116, 139));
        RECT closeRect = { rect.right - 25, 8, rect.right - 5, 28 };
        DrawTextW(hdc, L"X", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT rect;
        GetClientRect(hwnd, &rect);
        
        // Close Box
        RECT closeHit = { rect.right - 30, 0, rect.right, 30 };
        if (PtInRect(&closeHit, pt)) {
            DestroyWindow(hwnd);
            return 0;
        }

        if (data->type == TYPE_CONSENT) {
            // Reboot Now Button
            RECT btn1 = { 15, rect.bottom - 45, 160, rect.bottom - 15 };
            if (PtInRect(&btn1, pt)) {
                // Signal Main App to Reboot
                HWND hMain = FindWindowW(L"DontReboot11Main", NULL);
                if (hMain) {
                    // We'll define WM_USER + 10 as ID_REBOOT_TRIGGERED
                    PostMessageW(hMain, WM_COMMAND, ID_REBOOT_TRIGGERED, 0); 
                }
                DestroyWindow(hwnd);
                return 0;
            }

            // Remind Later Button
            RECT btn2 = { 175, rect.bottom - 45, 320, rect.bottom - 15 };
            if (PtInRect(&btn2, pt)) {
                DestroyWindow(hwnd);
                return 0;
            }
        }
        return 0;
    }
    case WM_DESTROY:
        if (data) {
            DeleteObject(data->hTitleFont);
            DeleteObject(data->hMessageFont);
            delete data;
        }
        PostQuitMessage(0); // Exit the thread's message loop
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
