#pragma once
#include <windows.h>
#include <string>

class NotificationWindow {
public:
    enum NotificationType {
        TYPE_INFO,
        TYPE_ALERT
    };

    static void Show(const std::wstring& title, const std::wstring& message, NotificationType type = TYPE_INFO);
    static void RegisterClass(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    struct NotificationData {
        std::wstring title;
        std::wstring message;
        NotificationType type;
        HFONT hTitleFont;
        HFONT hMessageFont;
    };
};
