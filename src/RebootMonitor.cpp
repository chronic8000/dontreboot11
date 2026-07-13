#include "RebootMonitor.h"
#include <windows.h>

#define WM_REBOOT_STATUS_CHANGE (WM_USER + 2)

HWND RebootMonitor::m_hMainWnd = NULL;
HANDLE RebootMonitor::m_hThread = NULL;
bool RebootMonitor::m_bRunning = false;

static std::wstring g_lastStatusInfo = L"Restart check: ...";
static RebootMonitor::RebootReason g_lastReason = RebootMonitor::REASON_NONE;
static bool g_restartFlagged = false;

static bool KeyExists(HKEY root, const wchar_t* path) {
    HKEY key = nullptr;
    const LONG r = RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    return false;
}

bool RebootMonitor::CheckStrictRestartFlags() {
    // Only these two keys mean "restart required to finish installed updates".
    // Do NOT use PendingFileRenameOperations — it causes constant false positives.
    if (KeyExists(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired")) {
        return true;
    }
    if (KeyExists(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending")) {
        return true;
    }
    return false;
}

void RebootMonitor::UpdateStatus() {
    g_restartFlagged = CheckStrictRestartFlags();
    if (g_restartFlagged) {
        g_lastStatusInfo = L"Windows: restart needed to finish installed updates";
        g_lastReason = REASON_WINDOWS_UPDATE;
    } else {
        g_lastStatusInfo = L"Windows: no restart required";
        g_lastReason = REASON_NONE;
    }
}

bool RebootMonitor::IsRestartRequiredByWindows() {
    return g_restartFlagged;
}

void RebootMonitor::Start(HWND hMainWnd) {
    m_hMainWnd = hMainWnd;
    m_bRunning = true;
    m_hThread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
}

void RebootMonitor::Stop() {
    m_bRunning = false;
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 5000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

void RebootMonitor::ForceRefresh(HWND hMainWnd) {
    UpdateStatus();
    if (hMainWnd) {
        PostMessageW(hMainWnd, WM_REBOOT_STATUS_CHANGE, 0, 0);
    }
}

std::wstring RebootMonitor::GetStatusInfo() {
    return g_lastStatusInfo;
}

RebootMonitor::RebootReason RebootMonitor::GetLastReason() {
    return g_lastReason;
}

DWORD WINAPI RebootMonitor::ThreadProc(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);

    UpdateStatus();
    PostMessageW(m_hMainWnd, WM_REBOOT_STATUS_CHANGE, 0, 0);

    while (m_bRunning) {
        const bool wasFlagged = g_restartFlagged;
        UpdateStatus();
        if (wasFlagged != g_restartFlagged) {
            PostMessageW(m_hMainWnd, WM_REBOOT_STATUS_CHANGE, 0, 0);
        }
        Sleep(30000);
    }
    return 0;
}
