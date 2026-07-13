#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <winrt/base.h>
#include "../include/NotificationWindow.h"
#include "../include/RebootMonitor.h"
#include "../include/OrchestratorProtector.h"
#include "../include/UpdatePolicyManager.h"
#include "../include/Diagnostics.h"
#include "../resources/resource.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "comctl32.lib")

#define WM_TRAY_MSG (WM_USER + 1)
#define WM_REBOOT_STATUS_CHANGE (WM_USER + 2)

#define APP_TITLE L"Don't Reboot 11"
#define BLOCK_REASON_NORMAL L"Don't Reboot 11 is running (automatic restarts blocked)."

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

bool IsElevated() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void RelaunchElevated(HWND hwnd, const wchar_t* params) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) return;

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = params;
    sei.hwnd = hwnd;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        NotificationWindow::Show(L"Elevation failed",
            L"Could not elevate to restore Windows Update control.",
            NotificationWindow::TYPE_ALERT);
    }
}

bool g_userAuthorizedReboot = false;

std::wstring PolicyResultMessage(UpdatePolicyManager::PolicyResult r) {
    switch (r) {
    case UpdatePolicyManager::PolicyResult::Ok:
        return L"Done.";
    case UpdatePolicyManager::PolicyResult::AccessDenied:
        return L"Access denied. Right-click dontreboot11.exe and Run as administrator.";
    default:
        return L"Registry update failed.";
    }
}

NOTIFYICONDATAW nid = { 0 };

void RefreshTrayIcon(HWND hwnd) {
    UNREFERENCED_PARAMETER(hwnd);
    nid.hIcon = LoadIcon(NULL, IDI_SHIELD);
    wcscpy_s(nid.szTip, APP_TITLE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void SetupTray(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = IDI_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID;
    nid.uCallbackMessage = WM_TRAY_MSG;
    nid.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIcon(NULL, IDI_SHIELD);
    wcscpy_s(nid.szTip, APP_TITLE);
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

    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, ID_TRAY_STATUS, L"Status: Protecting against auto-restart");
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 1003, UpdatePolicyManager::GetStatusLine().c_str());
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 1008, RebootMonitor::GetStatusInfo().c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    UINT pauseFlags = MF_STRING | (UpdatePolicyManager::IsPauseEffectivelyActive() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, pauseFlags, ID_TRAY_PAUSE_WU, L"Pause updates (Settings — 35 days)");

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE_WU, L"Give me Windows Update control back");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_WRITE_LOG, L"Write diagnostics log");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    UINT autostartFlags = MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autostartFlags, ID_TRAY_AUTOSTART, L"Start with Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit Don't Reboot 11...");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void FullRestore(HWND hwnd) {
    OrchestratorProtector::ReleaseLegacyWinRtState();
    UpdatePolicyManager::RecoverWindowsUpdateControl();
    RebootMonitor::ForceRefresh(hwnd);
    ShutdownBlockReasonDestroy(hwnd);
    ShutdownBlockReasonCreate(hwnd, BLOCK_REASON_NORMAL);
    RefreshTrayIcon(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        ShutdownBlockReasonCreate(hwnd, BLOCK_REASON_NORMAL);
        OrchestratorProtector::ReleaseLegacyWinRtState();
        UpdatePolicyManager::RecoverWindowsUpdateControl();
        if (UpdatePolicyManager::IsPauseEnabled()) {
            UpdatePolicyManager::EnablePause();
        }
        OrchestratorProtector::Initialize();
        RebootMonitor::Start(hwnd);
        return 0;

    case WM_REBOOT_STATUS_CHANGE:
        RefreshTrayIcon(hwnd);
        return 0;

    case WM_TRAY_MSG:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            break;
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == ID_TRAY_AUTOSTART) {
            bool newState = !IsAutoStartEnabled();
            SetAutoStart(newState);
            NotificationWindow::Show(
                newState ? L"Autostart on" : L"Autostart off",
                newState ? L"Don't Reboot 11 will start with Windows." : L"Autostart disabled.",
                NotificationWindow::TYPE_INFO);
        } else if (LOWORD(wParam) == ID_TRAY_PAUSE_WU) {
            UpdatePolicyManager::PolicyResult r;
            std::wstring title;
            std::wstring body;
            if (UpdatePolicyManager::IsPauseEffectivelyActive()) {
                r = UpdatePolicyManager::DisablePause();
                title = L"Pause off";
                body = L"Updates are no longer paused.";
            } else {
                r = UpdatePolicyManager::EnablePause();
                title = L"Updates paused";
                body = L"Paused for up to 35 days (Settings-style). Turn off pause to install updates.";
            }
            if (r != UpdatePolicyManager::PolicyResult::Ok) {
                body = PolicyResultMessage(r);
            }
            NotificationWindow::Show(title, body, NotificationWindow::TYPE_INFO);
        } else if (LOWORD(wParam) == ID_TRAY_RESTORE_WU) {
            if (IsElevated()) {
                FullRestore(hwnd);
                const auto logPath = Diagnostics::WriteReport(L"Restore Windows Update control");
                if (!logPath.empty()) {
                    NotificationWindow::Show(L"Windows Update restored",
                        L"Restore completed.\nA diagnostics log was written next to the exe:\n" + logPath,
                        NotificationWindow::TYPE_INFO);
                } else {
                    NotificationWindow::Show(L"Windows Update restored",
                        L"Restore completed.\n(Unable to write diagnostics log file.)",
                        NotificationWindow::TYPE_INFO);
                }
            } else {
                RelaunchElevated(hwnd, L"--restore-wu");
            }
        } else if (LOWORD(wParam) == ID_TRAY_WRITE_LOG) {
            const auto logPath = Diagnostics::WriteReport(L"User requested diagnostics");
            if (!logPath.empty()) {
                NotificationWindow::Show(L"Diagnostics written",
                    L"Saved next to dontreboot11.exe:\n" + logPath,
                    NotificationWindow::TYPE_INFO);
            } else {
                NotificationWindow::Show(L"Diagnostics failed",
                    L"Could not write a log next to dontreboot11.exe.",
                    NotificationWindow::TYPE_ALERT);
            }
        } else if (LOWORD(wParam) == ID_REBOOT_TRIGGERED) {
            g_userAuthorizedReboot = true;
            OrchestratorProtector::ReleaseLegacyWinRtState();
            ShutdownBlockReasonDestroy(hwnd);
            if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, 0)) {
                system("shutdown /r /t 5 /c \"Don't Reboot 11: authorized reboot\"");
            }
        }
        return 0;

    case WM_QUERYENDSESSION:
        // ShutdownBlockReasonCreate handles restart prompts; do not cancel user shutdown here.
        UNREFERENCED_PARAMETER(g_userAuthorizedReboot);
        return TRUE;

    case WM_DESTROY:
        RebootMonitor::Stop();
        OrchestratorProtector::Shutdown();
        UpdatePolicyManager::RecoverWindowsUpdateControl();
        ShutdownBlockReasonDestroy(hwnd);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Check if command line requests restore
    if (lpCmdLine && wcsstr(lpCmdLine, L"--restore-wu") != nullptr) {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        if (IsElevated()) {
            OrchestratorProtector::ReleaseLegacyWinRtState();
            UpdatePolicyManager::RecoverWindowsUpdateControl();
            const auto logPath = Diagnostics::WriteReport(L"Command-line restore (elevated)");
            std::wstring msg = L"Windows Update control has been successfully restored!\n\nAll policy locks have been removed, the WinRT administration registration has been cleared, and the Windows Update cache has been refreshed.\n\nPlease check Windows Update in Settings.";
            if (!logPath.empty()) {
                msg += L"\n\nDiagnostics log written to:\n" + logPath;
            }
            MessageBoxW(NULL, msg.c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION);
        } else {
            RelaunchElevated(NULL, L"--restore-wu");
        }
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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

    return (int)msg.wParam;
}
