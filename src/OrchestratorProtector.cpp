#include "OrchestratorProtector.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Management.Update.h>
#include <winrt/Windows.System.Update.h>
#include <accctrl.h>
#include <aclapi.h>
#include <thread>
#include "NotificationWindow.h"

using namespace winrt;
using namespace Windows::Management::Update;
using namespace Windows::System::Update;

TRACEHANDLE OrchestratorProtector::m_hTraceSession = 0;
TRACEHANDLE OrchestratorProtector::m_hTraceHandle = 0;
HANDLE OrchestratorProtector::m_hEtwThread = NULL;
bool OrchestratorProtector::m_bEtwRunning = false;

static const GUID UpdateOrchestratorProvider = { 0xb91d5831, 0xf1d7, 0x4009, { 0xbc, 0x94, 0x01, 0x3d, 0xc5, 0x08, 0x09, 0x80 } };

bool OrchestratorProtector::Initialize() {
    ReleaseLegacyWinRtState();
    RestoreScheduledTasks();

    m_bEtwRunning = true;
    m_hEtwThread = CreateThread(NULL, 0, EtwThreadProc, NULL, 0, NULL);
    return true;
}

void OrchestratorProtector::Shutdown() {
    m_bEtwRunning = false;
    ReleaseLegacyWinRtState();

    if (m_hTraceSession) {
        ControlTrace(m_hTraceSession, KERNEL_LOGGER_NAME, NULL, EVENT_TRACE_CONTROL_STOP);
    }
    if (m_hEtwThread) {
        WaitForSingleObject(m_hEtwThread, 1000);
        CloseHandle(m_hEtwThread);
        m_hEtwThread = NULL;
    }
}

void OrchestratorProtector::ReleaseLegacyWinRtState() {
    // Both APIs are best-effort: the lock/registration may never have existed on this
    // consumer machine, in which case the call returns an error HRESULT or throws.
    // We fire-and-forget so there is no .get() blocking a non-coroutine thread.

    try {
        // Synchronous, fine to call directly.
        WindowsUpdateAdministrator::UnregisterForAdministration(L"DontReboot11-Interdictor");
    } catch (...) {
        // Expected on consumer SKUs that never had this registered — safe to ignore.
    }

    try {
        WindowsUpdateAdministrator::UnregisterForAdministration(L"Sentinel-Sovereign-Interdictor");
    } catch (...) {
        // Safe to ignore.
    }

    try {
        // Fire-and-forget: do NOT call .get() here; this is not a coroutine context.
        // The async completes on a WinRT thread pool thread without blocking us.
        [[maybe_unused]] auto op =
            SystemUpdateManager::UnblockAutomaticRebootAsync(L"DontReboot11-Lock");
    } catch (...) {
        // Lock was never set, or API not supported — safe to ignore.
    }
}

bool OrchestratorProtector::RestoreScheduledTasks() {
    const wchar_t* taskPath = L"C:\\Windows\\System32\\Tasks\\Microsoft\\Windows\\UpdateOrchestrator\\Reboot";

    EXPLICIT_ACCESS ea[1] = {0};
    PSID pSystemSid = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSystemSid)) {
        ea[0].grfAccessPermissions = GENERIC_ALL;
        ea[0].grfAccessMode = GRANT_ACCESS;
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

DWORD WINAPI OrchestratorProtector::EtwThreadProc(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);
    EVENT_TRACE_PROPERTIES* pProps = (EVENT_TRACE_PROPERTIES*)malloc(sizeof(EVENT_TRACE_PROPERTIES) + 1024);
    ZeroMemory(pProps, sizeof(EVENT_TRACE_PROPERTIES) + 1024);
    pProps->Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
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

    if (eventId == 11 || eventId == 47 || eventId == 28) {
        UNREFERENCED_PARAMETER(eventId);
        // Informational only — do not pop "reboot pending" consent (ETW fires too often on 25H2).
    }
}
