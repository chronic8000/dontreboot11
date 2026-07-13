#pragma once
#include <windows.h>
#include <string>

class RebootMonitor {
public:
    enum RebootReason {
        REASON_NONE = 0,
        REASON_WINDOWS_UPDATE,
    };

    static void Start(HWND hMainWnd);
    static void Stop();
    static void ForceRefresh(HWND hMainWnd);

    static std::wstring GetStatusInfo();
    static RebootReason GetLastReason();
    // True only when Windows has a strong restart-required signal (not used to block shutdown).
    static bool IsRestartRequiredByWindows();

private:
    static DWORD WINAPI ThreadProc(LPVOID lpParam);
    static void UpdateStatus();
    static bool CheckStrictRestartFlags();

    static HWND m_hMainWnd;
    static HANDLE m_hThread;
    static bool m_bRunning;
};
