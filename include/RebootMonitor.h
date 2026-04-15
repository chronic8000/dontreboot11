#pragma once
#include <windows.h>
#include <vector>
#include <string>

class RebootMonitor {
public:
    enum RebootReason {
        REASON_NONE = 0,
        REASON_WINDOWS_UPDATE,
        REASON_SYSTEM_DRIVER
    };

    static void Start(HWND hMainWnd);
    static void Stop();
    static std::wstring GetStatusInfo();
    static RebootReason GetLastReason();
    static int GetStatusLevel(); // 0: Safe, 1: Active, 2: RebootPending

private:
    static DWORD WINAPI ThreadProc(LPVOID lpParam);
    static bool CheckRebootPending();
    
    // Vector 1: Registry check
    static bool CheckRegistryKeys();
    
    // Vector 2: COM check (WUA API)
    static bool CheckCOMInterface();

    static HWND m_hMainWnd;
    static HANDLE m_hThread;
    static bool m_bRunning;
};
