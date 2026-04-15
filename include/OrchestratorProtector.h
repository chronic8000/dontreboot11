#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Management.Update.h>
#include <winrt/Windows.System.Update.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

class OrchestratorProtector {
public:
    static bool Initialize();
    static void Shutdown();

    // Section 3: WinRT Methods
    static winrt::Windows::Foundation::IAsyncAction RegisterAsAdministrator();
    static winrt::Windows::Foundation::IAsyncAction BlockRebootsAsync();

    // Section 5: Task Methods
    static bool InterdictScheduledTasks();
    static bool RestoreScheduledTasks();

private:
    // Section 4: ETW Methods
    static DWORD WINAPI EtwThreadProc(LPVOID lpParam);
    static void WINAPI EventRecordCallback(EVENT_RECORD* pEventRecord);
    
    static TRACEHANDLE m_hTraceSession;
    static TRACEHANDLE m_hTraceHandle;
    static HANDLE m_hEtwThread;
    static bool m_bEtwRunning;
};
