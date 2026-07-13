#pragma once
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

class OrchestratorProtector {
public:
    static bool Initialize();
    static void Shutdown();
    static void ReleaseLegacyWinRtState();
    static bool RestoreScheduledTasks();

private:
    static DWORD WINAPI EtwThreadProc(LPVOID lpParam);
    static void WINAPI EventRecordCallback(EVENT_RECORD* pEventRecord);

    static TRACEHANDLE m_hTraceSession;
    static TRACEHANDLE m_hTraceHandle;
    static HANDLE m_hEtwThread;
    static bool m_bEtwRunning;
};
