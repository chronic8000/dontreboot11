#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <winrt/base.h>
#include "../include/NotificationWindow.h"
#include "../include/RebootMonitor.h"
#include "../include/OrchestratorProtector.h"
#include "../resources/resource.h"

// Modern manifest-less Common Controls inclusion
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "comctl32.lib")

// User-defined messages
#define WM_TRAY_MSG (WM_USER + 1)
#define WM_REBOOT_STATUS_CHANGE (WM_USER + 2)

#define APP_TITLE L"Don't Reboot 11"
#define BLOCK_REASON_NORMAL L"Don't Reboot 11 is protecting your session."
#define BLOCK_REASON_PENDING L"WARNING: A critical reboot is pending! Don't Reboot 11 is intercepting."

// Registry Autostart Helpers
const wchar_t* AUTOSTART_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* AUTOSTART_VALUE = L"DontReboot11";

bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type;
        LONG result = RegQueryValueExW(hKey, AUTOSTART_VALUE, NULL, &type, NULL, NULL);
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return false;
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, AUTOSTART_VALUE, 0, REG_SZ, (BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, AUTOSTART_VALUE);
        }
        RegCloseKey(hKey);
    }
}


bool g_rebootPending = false;

NOTIFYICONDATAW nid = { 0 };

void SetupTray(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = IDI_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID;
    nid.uCallbackMessage = WM_TRAY_MSG;
    nid.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
    
    // Fallback if resource icon missing
    if (!nid.hIcon) nid.hIcon = LoadIcon(NULL, IDI_SHIELD);
    
    wcscpy_s(nid.szTip, APP_TITLE);
    
    // Unique GUID for the tray icon to ensure consistent behavior across reboots
    static const GUID myGuid = { 0x6e914148, 0x2aa2, 0x4e93, { 0x8c, 0x1c, 0x5f, 0x63, 0x80, 0xfc, 0x45, 0x0e } };
    nid.guidItem = myGuid;

    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void ShowTrayMenu(HWND hwnd) {
    POINT cursor;
    GetCursorPos(&cursor);
    HMENU hMenu = CreatePopupMenu();
    
    std::wstring statusText = g_rebootPending ? L"Status: REBOOT PENDING (Intercepted)" : L"Status: Protected";
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, ID_TRAY_STATUS, statusText.c_str());
    
    std::wstring updateStatus = RebootMonitor::GetStatusInfo();
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 1003, updateStatus.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    UINT autostartFlags = MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autostartFlags, ID_TRAY_AUTOSTART, L"Start with Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit Don't Reboot 11...");

    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        ShutdownBlockReasonCreate(hwnd, BLOCK_REASON_NORMAL);
        // Initialize 'God Mode' Orchestrator Protection
        OrchestratorProtector::Initialize();
        // Start Reboot Monitor
        RebootMonitor::Start(hwnd);
        return 0;

    case WM_REBOOT_STATUS_CHANGE: {
        g_rebootPending = (bool)wParam;
        int statusLevel = RebootMonitor::GetStatusLevel();
        
        // Dynamic Icon and Tooltip logic
        if (statusLevel == 2) {
            nid.hIcon = LoadIcon(NULL, IDI_ERROR);
            wcscpy_s(nid.szTip, L"Don't Reboot 11: REBOOT DETECTED");
        } else if (statusLevel == 1) {
            nid.hIcon = LoadIcon(NULL, IDI_INFORMATION);
            wcscpy_s(nid.szTip, L"Don't Reboot 11: Updates Active");
        } else {
            nid.hIcon = LoadIcon(NULL, IDI_SHIELD);
            wcscpy_s(nid.szTip, APP_TITLE);
        }
        Shell_NotifyIconW(NIM_MODIFY, &nid);

        if (g_rebootPending) {
            RebootMonitor::RebootReason reason = RebootMonitor::GetLastReason();
            std::wstring alertMsg = L"Proactive sweep detected a pending reboot. Protection has been escalated.";
            
            if (reason == RebootMonitor::REASON_WINDOWS_UPDATE) {
                alertMsg = L"Proactive sweep detected a pending Windows Update reboot. Protection has been escalated.";
            } else if (reason == RebootMonitor::REASON_SYSTEM_DRIVER) {
                alertMsg = L"Proactive sweep detected a critical System or Driver update reboot. Protection has been escalated.";
            }

            // Update block reason to be more aggressive
            ShutdownBlockReasonDestroy(hwnd);
            ShutdownBlockReasonCreate(hwnd, BLOCK_REASON_PENDING);
            
            // Pop the alert toast
            NotificationWindow::Show(L"DEFENSE ALERT", alertMsg.c_str(), NotificationWindow::TYPE_ALERT);
            
            // Update Tray Tooltip
            wcscpy_s(nid.szTip, L"Don't Reboot 11: REBOOT DETECTED");
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        } else {
            ShutdownBlockReasonDestroy(hwnd);
            ShutdownBlockReasonCreate(hwnd, BLOCK_REASON_NORMAL);
            wcscpy_s(nid.szTip, APP_TITLE);
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        return 0;
    }

    case WM_TRAY_MSG:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            break;
        case NIN_BALLOONUSERCLICK:
            // Handle tray notification clicks if any
            break;
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == ID_TRAY_AUTOSTART) {
            bool newState = !IsAutoStartEnabled();
            SetAutoStart(newState);
            // Optional: Show status feedback
            if (newState) {
                NotificationWindow::Show(L"Persistence Enabled", L"Don't Reboot 11 will now start automatically with Windows.");
            } else {
                NotificationWindow::Show(L"Persistence Disabled", L"Don't Reboot 11 will no longer start automatically.");
            }
        }
        return 0;


    case WM_QUERYENDSESSION:
        // Premium behavior: Show custom persistent toast
        NotificationWindow::Show(L"Reboot Blocked", L"An automated Windows Update reboot attempt was intercepted. Your session is safe.");
        
        // Return FALSE to tell Windows we are blocking the shutdown
        return FALSE;

    case WM_DESTROY:
        RebootMonitor::Stop();
        ShutdownBlockReasonDestroy(hwnd);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Initialize WinRT MTA context for God Mode
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Modern DPI Awareness (Win 10/11)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Enable Visual Styles
    InitCommonControls();

    NotificationWindow::RegisterClass(hInstance);

    const wchar_t CLASS_NAME[] = L"DontReboot11Main";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, APP_TITLE, 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;

    SetupTray(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RebootMonitor::Stop();
    OrchestratorProtector::Shutdown();
    return (int)msg.wParam;
}
