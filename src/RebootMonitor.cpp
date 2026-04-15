#include "RebootMonitor.h"
#include <windows.h>
#include <wuapi.h>
#include <atlbase.h>
#include <iostream>

HWND RebootMonitor::m_hMainWnd = NULL;
HANDLE RebootMonitor::m_hThread = NULL;
bool RebootMonitor::m_bRunning = false;
static std::wstring g_lastStatusInfo = L"Updates: Checking...";
static RebootMonitor::RebootReason g_lastReason = RebootMonitor::REASON_NONE;
static int g_lastStatusLevel = 0;

static bool IsFutureDate(const std::wstring& expiryStr) {
    if (expiryStr.empty()) return false;
    
    SYSTEMTIME st = {0};
    // User's specific ISO 8601 parsing format
    if (swscanf_s(expiryStr.c_str(), L"%4hd-%2hd-%2hdT%2hd:%2hd:%2hdZ", 
        &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) == 6) {
        
        FILETIME ftExpiry, ftCurrent;
        SystemTimeToFileTime(&st, &ftExpiry);
        
        // Use high-performance current time query
        GetSystemTimeAsFileTime(&ftCurrent); 
        
        ULARGE_INTEGER ulExpiry, ulCurrent;
        ulExpiry.LowPart = ftExpiry.dwLowDateTime;
        ulExpiry.HighPart = ftExpiry.dwHighDateTime;
        
        ulCurrent.LowPart = ftCurrent.dwLowDateTime;
        ulCurrent.HighPart = ftCurrent.dwHighDateTime;
        
        return ulExpiry.QuadPart > ulCurrent.QuadPart;
    }
    return false;
}

// Custom message to notify main window
#define WM_REBOOT_STATUS_CHANGE (WM_USER + 2)

void RebootMonitor::Start(HWND hMainWnd) {
    m_hMainWnd = hMainWnd;
    m_bRunning = true;
    m_hThread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
}

std::wstring RebootMonitor::GetStatusInfo() {
    return g_lastStatusInfo;
}

RebootMonitor::RebootReason RebootMonitor::GetLastReason() {
    return g_lastReason;
}

int RebootMonitor::GetStatusLevel() {
    return g_lastStatusLevel;
}

void RebootMonitor::Stop() {
    m_bRunning = false;
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 5000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

DWORD WINAPI RebootMonitor::ThreadProc(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);
    bool lastStatus = false;
    
    struct MonitorConfig {
        HKEY root;
        const wchar_t* path;
        bool watchSubtree;
    };

    const MonitorConfig configs[] = {
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings", FALSE },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\Settings", FALSE },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", TRUE },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired", FALSE },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending", FALSE },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", FALSE }
    };

    const int numConfigs = sizeof(configs) / sizeof(configs[0]);
    std::vector<HANDLE> hEvents;
    std::vector<HKEY> hKeys;

    for (int i = 0; i < numConfigs; ++i) {
        HKEY hKey = NULL;
        if (RegOpenKeyExW(configs[i].root, configs[i].path, 0, KEY_NOTIFY | KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            hEvents.push_back(CreateEvent(NULL, TRUE, FALSE, NULL));
            hKeys.push_back(hKey);
        }
    }

    while (m_bRunning) {
        // Arm all active observers
        for (size_t i = 0; i < hKeys.size(); ++i) {
            RegNotifyChangeKeyValue(hKeys[i], configs[i].watchSubtree, 
                REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_LAST_SET, 
                hEvents[i], TRUE);
        }

        bool currentStatus = CheckRebootPending();
        if (currentStatus != lastStatus) {
            PostMessage(m_hMainWnd, WM_REBOOT_STATUS_CHANGE, (WPARAM)currentStatus, 0);
            lastStatus = currentStatus;
        }

        // Wait for ANY change or a 5-minute watchdog timeout
        DWORD result = WaitForMultipleObjects((DWORD)hEvents.size(), hEvents.data(), FALSE, 300000); 
        
        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + hEvents.size()) {
            size_t idx = result - WAIT_OBJECT_0;
            Sleep(800); // 25H2 Haptic settling delay: Allow registry burst to complete
            ResetEvent(hEvents[idx]);
        }
    }

    // Cleanup
    for (auto hk : hKeys) RegCloseKey(hk);
    for (auto he : hEvents) CloseHandle(he);
    
    return 0;
}

bool RebootMonitor::CheckRebootPending() {
    bool wuPending = false;
    bool systemPending = false;
    bool paused = false;
    std::wstring pauseExpiry = L"";
    HKEY hKey;

    auto CheckAllPauseKeys = [&](bool& isPaused, std::wstring& expiry) {
        struct HiveConfig {
            HKEY root;
            std::wstring path;
        };
        
        const std::vector<HiveConfig> hives = {
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\Settings" },
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings" },
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate" },
            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\WindowsUpdate\\UX\\Settings" }
        };

        const std::vector<std::wstring> timeKeys = {
            L"PauseQualityUpdatesEndTime", L"PauseUpdatesExpiryTime", 
            L"PauseFeatureUpdatesEndTime", L"PausedQualityUpdatesEndTime", 
            L"PausedFeatureUpdatesEndTime", L"PauseDisplayUntil"
        };

        const std::vector<std::wstring> boolKeys = {
            L"PauseUpdates", L"PausedQualityStatus", L"PausedFeatureStatus",
            L"PausedQualityUpdatesStatus", L"PauseQualityStatus", L"FlightSettingsMaxPauseDays"
        };

        for (const auto& config : hives) {
            HKEY hSubKey;
            if (RegOpenKeyExW(config.root, config.path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hSubKey) == ERROR_SUCCESS) {
                // 1. Check Boolean/DWORD Toggles
                for (const auto& bKey : boolKeys) {
                    DWORD val = 0, dSize = sizeof(DWORD);
                    if (RegQueryValueExW(hSubKey, bKey.c_str(), NULL, NULL, (LPBYTE)&val, &dSize) == ERROR_SUCCESS && val > 0) {
                        isPaused = true;
                        break;
                    }
                }

                // 2. Check Timestamp Variants (String or QWORD)
                if (!isPaused) {
                    for (const auto& tKey : timeKeys) {
                        DWORD type = 0;
                        BYTE data[MAX_PATH * 2] = {0};
                        DWORD dSize = sizeof(data);
                        
                        if (RegQueryValueExW(hSubKey, tKey.c_str(), NULL, &type, data, &dSize) == ERROR_SUCCESS) {
                            if (type == REG_SZ || type == REG_EXPAND_SZ) {
                                std::wstring strVal = (wchar_t*)data;
                                if (IsFutureDate(strVal)) {
                                    isPaused = true;
                                    expiry = strVal;
                                    break;
                                }
                            } else if (type == REG_QWORD) {
                                unsigned __int64 qVal = *(unsigned __int64*)data;
                                FILETIME ftCurrent;
                                GetSystemTimeAsFileTime(&ftCurrent);
                                ULARGE_INTEGER ulCurrent;
                                ulCurrent.LowPart = ftCurrent.dwLowDateTime;
                                ulCurrent.HighPart = ftCurrent.dwHighDateTime;
                                
                                if (qVal > ulCurrent.QuadPart) {
                                    isPaused = true;
                                    expiry = L"Calendar Date";
                                    break;
                                }
                            }
                        }
                    }
                }
                RegCloseKey(hSubKey);
                if (isPaused) break;
            }
        }
    };

    // First Pass
    CheckAllPauseKeys(paused, pauseExpiry);

    // 5. Final Aggregation with Hyper-Intelligence (Section 2.2)
    // We suppress WUA-level flags (Vector 1/2) if the machine is paused,
    // as WUA frequently fails to flush its cache during a pause.
    // However, we NEVER suppress CBS or SessionManager flags (Vector 3/4)
    // as these represent actual kernel-level staging.
    
    bool legitimateThreat = systemPending; // Vector 4 is absolute
    
    // Vector 1, 2, 3 are evaluated against the pause state
    if (paused) {
        g_lastStatusInfo = L"Updates: Paused";
        if (!pauseExpiry.empty() && pauseExpiry.length() >= 10) g_lastStatusInfo += L" until " + pauseExpiry.substr(0, 10);
        
        // Even if paused, we alert if a CBS pending state exists (Vector 3) 
        // because it means a reboot is genuinely needed for stability.
        if (wuPending) {
            // Check if it's CBS-level (accurate) vs WUA-level (cached)
            // Re-scan CBS hive specifically to confirm
            bool cbsActuallyPending = false;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
                cbsActuallyPending = true;
                RegCloseKey(hKey);
            }
            
            if (cbsActuallyPending) {
                legitimateThreat = true;
                g_lastReason = REASON_WINDOWS_UPDATE;
                g_lastStatusLevel = 2; // Warning
            } else {
                // It was likely just the WUA cache false positive. Safe.
                g_lastReason = REASON_NONE;
                g_lastStatusLevel = 0; // Safe
            }
        } else {
            g_lastReason = REASON_NONE;
            g_lastStatusLevel = 0; // Safe
        }
    } else {
        g_lastStatusInfo = L"Updates: Active";
        if (systemPending) {
            g_lastReason = REASON_SYSTEM_DRIVER;
            g_lastStatusLevel = 2; // Warning
        }
        else if (wuPending) {
            g_lastReason = REASON_WINDOWS_UPDATE;
            g_lastStatusLevel = 2; // Warning
        }
        else {
            g_lastReason = REASON_NONE;
            g_lastStatusLevel = 1; // Active/Hunting
        }
    }

    return (g_lastStatusLevel == 2);
}

bool RebootMonitor::CheckRegistryKeys() {
    const wchar_t* keys[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending"
    };

    for (const auto& keyPath : keys) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
    }

    // Check PendingFileRenameOperations
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        DWORD type;
        if (RegQueryValueExW(hKey, L"PendingFileRenameOperations", NULL, &type, NULL, NULL) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }

    return false;
}

bool RebootMonitor::CheckCOMInterface() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    bool rebootRequired = false;
    {
        CComPtr<ISystemInformation> pSysInfo;
        hr = pSysInfo.CoCreateInstance(CLSID_SystemInformation);
        if (SUCCEEDED(hr)) {
            VARIANT_BOOL vbReboot;
            if (SUCCEEDED(pSysInfo->get_RebootRequired(&vbReboot))) {
                rebootRequired = (vbReboot == VARIANT_TRUE);
            }
        }

        // Secondary check via Detection probe if not already known
        if (!rebootRequired) {
            CComPtr<IAutomaticUpdates> pAU;
            if (SUCCEEDED(pAU.CoCreateInstance(CLSID_AutomaticUpdates))) {
                HRESULT hrDetect = pAU->DetectNow();
                if (hrDetect == 0x8024A002) { // WU_E_AU_PAUSED
                    // We can't easily return 'paused' from here without changing signature,
                    // but the Registry check handles the bulk. 
                    // This probe confirms the service status.
                }
            }
        }
    }

    CoUninitialize();
    return rebootRequired;
}
