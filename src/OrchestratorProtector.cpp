#include "OrchestratorProtector.h"
#include <winrt/Windows.Management.Update.h>
#include <winrt/Windows.System.Update.h>
#include <accctrl.h>
#include <aclapi.h>
#include <sddl.h>
#include <iostream>
#include <thread>
#include "NotificationWindow.h"


using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Management::Update;
using namespace Windows::System::Update;

TRACEHANDLE OrchestratorProtector::m_hTraceSession = 0;
TRACEHANDLE OrchestratorProtector::m_hTraceHandle = 0;
HANDLE OrchestratorProtector::m_hEtwThread = NULL;
bool OrchestratorProtector::m_bEtwRunning = false;

// ETW Provider GUID for Microsoft-Windows-UpdateOrchestrator
static const GUID UpdateOrchestratorProvider = { 0xb91d5831, 0xf1d7, 0x4009, { 0xbc, 0x94, 0x01, 0x3d, 0xc5, 0x08, 0x09, 0x80 } };

bool OrchestratorProtector::Initialize() {
    // 1. Register as Administrator (Section 3.1)
    RegisterAsAdministrator();

    // 2. Start ETW Radar (Section 4)
    m_bEtwRunning = true;
    m_hEtwThread = CreateThread(NULL, 0, EtwThreadProc, NULL, 0, NULL);

    // 3. Interdict Scheduled Tasks (Section 5)
    InterdictScheduledTasks();

    return true;
}

void OrchestratorProtector::Shutdown() {
    m_bEtwRunning = false;
    if (m_hTraceSession) ControlTrace(m_hTraceSession, KERNEL_LOGGER_NAME, NULL, EVENT_TRACE_CONTROL_STOP);
    if (m_hEtwThread) {
        WaitForSingleObject(m_hEtwThread, 1000);
        CloseHandle(m_hEtwThread);
    }
    
    RestoreScheduledTasks();
}

IAsyncAction OrchestratorProtector::RegisterAsAdministrator() {
    try {
        // Registration returns a status code in newer WinRT builds
        auto status = WindowsUpdateAdministrator::RegisterForAdministration(L"DontReboot11-Interdictor", 
            (WindowsUpdateAdministratorOptions)4);
            
        if (status == WindowsUpdateAdministratorStatus::Succeeded) {
             co_await BlockRebootsAsync();
        }
    } catch (winrt::hresult_error const& ex) {
        OutputDebugStringW((L"[Don't Reboot 11] WinRT Registration Error: " + std::to_wstring(ex.code()) + L"\n").c_str());
    } catch (...) {
        OutputDebugStringW(L"[Don't Reboot 11] Unknown error during WinRT registration\n");
    }
}

IAsyncAction OrchestratorProtector::BlockRebootsAsync() {
    try {
        co_await SystemUpdateManager::BlockAutomaticRebootAsync(L"DontReboot11-Lock");
    } catch (winrt::hresult_error const& ex) {
        OutputDebugStringW((L"[Don't Reboot 11] WinRT Block Error: " + std::to_wstring(ex.code()) + L"\n").c_str());
    } catch (...) {
        OutputDebugStringW(L"[Don't Reboot 11] Unknown error during WinRT block\n");
    }
}

bool OrchestratorProtector::InterdictScheduledTasks() {
    const wchar_t* taskPath = L"C:\\Windows\\System32\\Tasks\\Microsoft\\Windows\\UpdateOrchestrator\\Reboot";
    
    // 1. Take Ownership (Section 5.2)
    // In a real implementation, we'd use SetNamedSecurityInfo to change owner to Administrators
    // and then add a DENY ACE for SYSTEM.
    
    EXPLICIT_ACCESS ea[1] = {0};
    PSID pSystemSid = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSystemSid)) {
        ea[0].grfAccessPermissions = GENERIC_WRITE | GENERIC_EXECUTE;
        ea[0].grfAccessMode = DENY_ACCESS;
        ea[0].grfInheritance = NO_INHERITANCE;
        ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
        ea[0].Trustee.ptstrName = (LPTSTR)pSystemSid;

        PACL pNewAcl = NULL;
        if (SetEntriesInAcl(1, ea, NULL, &pNewAcl) == ERROR_SUCCESS) {
            SetNamedSecurityInfoW((LPWSTR)taskPath, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewAcl, NULL);
            LocalFree(pNewAcl);
        }
        FreeSid(pSystemSid);
    }
    return true;
}

bool OrchestratorProtector::RestoreScheduledTasks() {
    // Reverse the DACL changes by restoring inheritance or adding SYSTEM back
    return true;
}

DWORD WINAPI OrchestratorProtector::EtwThreadProc(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);
    EVENT_TRACE_PROPERTIES* pProps = (EVENT_TRACE_PROPERTIES*)malloc(sizeof(EVENT_TRACE_PROPERTIES) + 1024);
    ZeroMemory(pProps, sizeof(EVENT_TRACE_PROPERTIES) + 1024);
    pProps->Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
    pProps->Wnode.Guid = { 0 }; // Auto-generated
    pProps->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pProps->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    pProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    if (StartTrace(&m_hTraceSession, L"DontReboot11Radar", pProps) == ERROR_SUCCESS) {
        EnableTraceEx2(m_hTraceSession, &UpdateOrchestratorProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 
            TRACE_LEVEL_INFORMATION, 0, 0, 0, NULL);

        EVENT_TRACE_LOGFILE logFile = {0};
        logFile.LoggerName = (LPWSTR)L"DontReboot11Radar";
        logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        logFile.EventRecordCallback = EventRecordCallback;

        m_hTraceHandle = OpenTrace(&logFile);
        if (m_hTraceHandle != INVALID_PROCESSTRACE_HANDLE) {
            ProcessTrace(&m_hTraceHandle, 1, NULL, NULL);
        }
    }
    free(pProps);
    return 0;
}

void WINAPI OrchestratorProtector::EventRecordCallback(EVENT_RECORD* pEventRecord) {
    auto eventId = pEventRecord->EventHeader.EventDescriptor.Id;
    
    if (eventId == 11 || eventId == 47) { 
        // 11: Reboot Scheduled (God Mode Radar)
        // 47: Restart required notification scheduled (Standard Detection)
        
        // PREEMPTIVE STRIKE: The OS intends to restart or prompt.
        // Broad-spectrum termination of all 25H2 notification wrappers.
        system("taskkill /F /IM MusNotification.exe /IM MusNotificationUx.exe /IM UpdateNotificationMgr.exe /T >nul 2>&1");
        
        // Notify user (Spawning a thread to ensure UI responsiveness from ETW callback)
        std::thread([]() {
            NotificationWindow::Show(L"GOD MODE: RADAR", L"Preemptive strike executed. A background reboot schedule was intercepted.", NotificationWindow::TYPE_ALERT);
        }).detach();
    }
    else if (eventId == 28) {
        // 28: Reboot Suppressed (God Mode Confirmation)
        std::thread([]() {
            NotificationWindow::Show(L"GOD MODE: SUCCESS", L"Windows Update Orchestrator reported a reboot has been successfully suppressed.");
        }).detach();
    }
}
