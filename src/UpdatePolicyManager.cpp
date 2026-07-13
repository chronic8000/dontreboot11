#include "../include/UpdatePolicyManager.h"
#include <windows.h>
#include <vector>
#include <filesystem>

#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kStateRoot[] = L"SOFTWARE\\DontReboot11";
constexpr wchar_t kStatePauseEnabled[] = L"PauseEnabled";
constexpr wchar_t kBackupRoot[] = L"SOFTWARE\\DontReboot11\\PolicyBackup";

constexpr wchar_t kWuPolicyKey[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate";
constexpr wchar_t kWuAuKey[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
constexpr wchar_t kGpCacheKey[] = L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\GPCache";

struct SavedValue {
    std::wstring name;
    DWORD type = 0;
    std::vector<BYTE> data;
};

// Legacy keys written by older builds — always delete on recover.
const wchar_t* kLegacyRebootWu[] = {
    L"NoAutoRebootWithLoggedOnUsers",
    L"SetAutoRestartDeadline",
    L"SetEDURestart",
};

const wchar_t* kLegacyRebootAu[] = {
    L"NoAutoRebootWithLoggedOnUsers",
    L"AlwaysAutoRebootAtScheduledTime",
    L"AUOptions",
};

// Organization keys — never set again.
const wchar_t* kLegacyOrgWuValues[] = {
    L"PauseFeatureUpdatesStartTime",
    L"PauseQualityUpdatesStartTime",
    L"DeferFeatureUpdates",
    L"DeferFeatureUpdatesPeriodInDays",
    L"DeferQualityUpdates",
    L"DeferQualityUpdatesPeriodInDays",
};

// Compliance / scheduling keys that cause "selected by your organisation" and
// grey out manual download — always removed on disable/recover.
const wchar_t* kBlockingWuValues[] = {
    L"ConfigureDeadlineForFeatureUpdates",
    L"ConfigureDeadlineForQualityUpdates",
    L"ConfigureDeadlineGracePeriod",
    L"ConfigureDeadlineGracePeriodForFeatureUpdates",
    L"ConfigureDeadlineNoAutoRebootForFeatureUpdates",
    L"ConfigureDeadlineNoAutoRebootForQualityUpdates",
    L"SetDisablePauseUXAccess",
    L"SetDisableUXWUAccess",
    L"TargetReleaseVersion",
    L"TargetReleaseVersionInfo",
    L"ProductVersion",
    L"BranchReadinessLevel",
    L"ScheduledInstallDay",
    L"ScheduledInstallTime",
    L"ScheduledInstallEveryWeek",
    L"ScheduledInstallFirstWeek",
    L"ScheduledInstallSecondWeek",
    L"ScheduledInstallThirdWeek",
    L"ScheduledInstallFourthWeek",
    L"DisableWindowsUpdateAccess",
    L"DoNotConnectToWindowsUpdateInternetLocations",
};

const wchar_t* kBlockingAuValues[] = {
    L"ScheduledInstallDay",
    L"ScheduledInstallTime",
    L"ScheduledInstallEveryWeek",
};

std::wstring UtcExpiryIso8601(int daysFromNow) {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += static_cast<ULONGLONG>(daysFromNow) * 24ULL * 60ULL * 60ULL * 10000000ULL;

    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;

    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);
    wchar_t buf[32]{};
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

LONG OpenPolicyKey(const wchar_t* subPath, REGSAM access, HKEY* outKey) {
    return RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        subPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        access | KEY_WOW64_64KEY,
        nullptr,
        outKey,
        nullptr);
}

bool ReadDword(HKEY key, const wchar_t* name, DWORD* value) {
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
        && type == REG_DWORD;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(DWORD)) == ERROR_SUCCESS;
}

bool WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes) == ERROR_SUCCESS;
}

bool DeleteValueIfExists(HKEY key, const wchar_t* name) {
    LONG r = RegDeleteValueW(key, name);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

bool EnumerateValues(HKEY key, std::vector<SavedValue>& out) {
    out.clear();
    DWORD index = 0;
    wchar_t name[256];
    DWORD nameLen = 0;
    DWORD type = 0;
    BYTE data[4096];
    DWORD dataLen = 0;

    while (true) {
        nameLen = static_cast<DWORD>(std::size(name));
        dataLen = static_cast<DWORD>(std::size(data));
        LONG r = RegEnumValueW(key, index++, name, &nameLen, nullptr, &type, data, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) {
            return true;
        }
        if (r != ERROR_SUCCESS) {
            return false;
        }
        SavedValue sv;
        sv.name = name;
        sv.type = type;
        sv.data.assign(data, data + dataLen);
        out.push_back(std::move(sv));
    }
}

bool RestoreValue(HKEY key, const SavedValue& sv) {
    return RegSetValueExW(key, sv.name.c_str(), 0, sv.type, sv.data.data(), static_cast<DWORD>(sv.data.size())) == ERROR_SUCCESS;
}

std::wstring BackupSubKeyFor(const wchar_t* policyKeyPath) {
    if (wcscmp(policyKeyPath, kWuAuKey) == 0) {
        return L"AU";
    }
    return L"WindowsUpdate";
}

bool SaveBackupForKey(const wchar_t* policyKeyPath) {
    HKEY src = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, policyKeyPath, 0, KEY_READ | KEY_WOW64_64KEY, &src) != ERROR_SUCCESS) {
        return true; // nothing to back up
    }

    std::vector<SavedValue> values;
    if (!EnumerateValues(src, values)) {
        RegCloseKey(src);
        return false;
    }
    RegCloseKey(src);

    const std::wstring branch = std::wstring(kBackupRoot) + L"\\" + BackupSubKeyFor(policyKeyPath);

    HKEY backupKey = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, branch.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_WRITE | KEY_WOW64_64KEY, nullptr, &backupKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    // Clear previous backup
    DWORD idx = 0;
    wchar_t child[128];
    DWORD childLen = static_cast<DWORD>(std::size(child));
    while (RegEnumKeyExW(backupKey, idx, child, &childLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegDeleteKeyW(backupKey, child);
        childLen = static_cast<DWORD>(std::size(child));
    }

    for (const auto& sv : values) {
        HKEY valKey = nullptr;
        if (RegCreateKeyExW(backupKey, sv.name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_WRITE | KEY_WOW64_64KEY, nullptr, &valKey, nullptr) != ERROR_SUCCESS) {
            RegCloseKey(backupKey);
            return false;
        }
        WriteDword(valKey, L"Type", sv.type);
        if (!sv.data.empty()) {
            RegSetValueExW(valKey, L"Data", 0, REG_BINARY, sv.data.data(), static_cast<DWORD>(sv.data.size()));
        }
        RegCloseKey(valKey);
    }

    RegCloseKey(backupKey);
    return true;
}

bool RestoreBackupForKey(const wchar_t* policyKeyPath) {
    const std::wstring branch = std::wstring(kBackupRoot) + L"\\" + BackupSubKeyFor(policyKeyPath);

    HKEY backupKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, branch.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &backupKey) != ERROR_SUCCESS) {
        return true;
    }

    HKEY dest = nullptr;
    if (OpenPolicyKey(policyKeyPath, KEY_WRITE, &dest) != ERROR_SUCCESS) {
        RegCloseKey(backupKey);
        return false;
    }

    DWORD idx = 0;
    wchar_t child[128];
    DWORD childLen = static_cast<DWORD>(std::size(child));
    while (RegEnumKeyExW(backupKey, idx, child, &childLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY valKey = nullptr;
        if (RegOpenKeyExW(backupKey, child, 0, KEY_READ | KEY_WOW64_64KEY, &valKey) == ERROR_SUCCESS) {
            DWORD type = 0;
            if (!ReadDword(valKey, L"Type", &type)) {
                RegCloseKey(valKey);
                childLen = static_cast<DWORD>(std::size(child));
                idx++;
                continue;
            }

            DWORD dataSize = 0;
            if (RegQueryValueExW(valKey, L"Data", nullptr, nullptr, nullptr, &dataSize) == ERROR_SUCCESS && dataSize > 0) {
                std::vector<BYTE> data(dataSize);
                if (RegQueryValueExW(valKey, L"Data", nullptr, nullptr, data.data(), &dataSize) == ERROR_SUCCESS) {
                    RegSetValueExW(dest, child, 0, type, data.data(), dataSize);
                }
            } else {
                RegSetValueExW(dest, child, 0, type, nullptr, 0);
            }
            RegCloseKey(valKey);
        }
        childLen = static_cast<DWORD>(std::size(child));
        idx++;
    }

    RegCloseKey(dest);
    RegCloseKey(backupKey);
    return true;
}

void DeleteLegacyAndBlockingValues(HKEY wuKey, HKEY auKey) {
    if (wuKey) {
        for (const wchar_t* n : kLegacyOrgWuValues) {
            DeleteValueIfExists(wuKey, n);
        }
        for (const wchar_t* n : kLegacyRebootWu) {
            DeleteValueIfExists(wuKey, n);
        }
        for (const wchar_t* n : kBlockingWuValues) {
            DeleteValueIfExists(wuKey, n);
        }
    }
    if (auKey) {
        for (const wchar_t* n : kBlockingAuValues) {
            DeleteValueIfExists(auKey, n);
        }
        for (const wchar_t* n : kLegacyRebootAu) {
            DeleteValueIfExists(auKey, n);
        }
    }
}

void SetPauseState(bool pauseEnabled) {
    HKEY state = nullptr;
    if (OpenPolicyKey(kStateRoot, KEY_WRITE, &state) != ERROR_SUCCESS) {
        return;
    }
    WriteDword(state, kStatePauseEnabled, pauseEnabled ? 1u : 0u);
    RegCloseKey(state);
}

bool ValueExists(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    const LONG r = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool DeleteAllValuesInKey(HKEY key) {
    if (!key) {
        return false;
    }
    std::vector<std::wstring> names;
    DWORD index = 0;
    wchar_t name[256];
    DWORD nameLen = static_cast<DWORD>(std::size(name));
    while (RegEnumValueW(key, index, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        names.emplace_back(name);
        nameLen = static_cast<DWORD>(std::size(name));
        index++;
    }
    for (const auto& n : names) {
        RegDeleteValueW(key, n.c_str());
    }
    return true;
}

void DeletePolicyKeyTrees() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kWuAuKey);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kWuPolicyKey);
}

void RestartWindowsUpdateServices() {
    auto run = [](const wchar_t* cmd) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        wchar_t mutableCmd[512]{};
        wcsncpy_s(mutableCmd, cmd, _TRUNCATE);
        if (CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    };
    run(L"net.exe stop wuauserv");
    run(L"net.exe stop UsoSvc");
    run(L"net.exe start UsoSvc");
    run(L"net.exe start wuauserv");
}

// Stale PolicyState (especially SetPolicyDrivenUpdateSource* = 0xFFFFFFFF) makes
// Settings show "selected by your organisation" even when policy keys are gone.
void ResetUpdatePolicyPolicyState() {
    constexpr wchar_t kPolicyState[] = L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\PolicyState";
    const wchar_t* policyDriven[] = {
        L"SetPolicyDrivenUpdateSourceForFeatureUpdates",
        L"SetPolicyDrivenUpdateSourceForQualityUpdates",
        L"SetPolicyDrivenUpdateSourceForDriverUpdates",
        L"SetPolicyDrivenUpdateSourceForOtherUpdates",
    };

    HKEY ps = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kPolicyState, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_WRITE | KEY_WOW64_64KEY, nullptr, &ps, nullptr) != ERROR_SUCCESS) {
        return;
    }

    for (const wchar_t* name : policyDriven) {
        DeleteValueIfExists(ps, name);
        WriteDword(ps, name, 0);
    }
    WriteDword(ps, L"UseUpdateClassPolicySource", 0);
    WriteDword(ps, L"TemporaryEnterpriseFeatureControlState", 0);
    WriteDword(ps, L"IsWUfBConfigured", 0);
    WriteDword(ps, L"IsWUfBDualScanActive", 0);
    WriteDword(ps, L"IsDeferralIsActive", 0);
    WriteDword(ps, L"DeferFeatureUpdates", 0);
    WriteDword(ps, L"DeferQualityUpdates", 0);
    RegCloseKey(ps);

    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kGpCacheKey);
}

void ClearUxPauseState() {
    const wchar_t* uxKey = L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, uxKey, 0, KEY_WRITE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        DeleteValueIfExists(key, L"PauseUpdatesExpiryTime");
        RegCloseKey(key);
    }

    const wchar_t* settingsKey = L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\Settings";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, settingsKey, 0, KEY_WRITE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        WriteDword(key, L"PausedFeatureStatus", 0);
        WriteDword(key, L"PausedQualityStatus", 0);
        DeleteValueIfExists(key, L"PausedFeatureDate");
        DeleteValueIfExists(key, L"PausedQualityDate");
        RegCloseKey(key);
    }
}

bool ReadStateFlag(const wchar_t* name) {
    HKEY state = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kStateRoot, 0, KEY_READ | KEY_WOW64_64KEY, &state) != ERROR_SUCCESS) {
        return false;
    }
    DWORD v = 0;
    const bool ok = ReadDword(state, name, &v) && v != 0;
    RegCloseKey(state);
    return ok;
}

} // namespace

namespace UpdatePolicyManager {

void RefreshPolicyCache() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kGpCacheKey);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    wchar_t gpupdate[] = L"gpupdate.exe /target:computer /force";
    if (CreateProcessW(nullptr, gpupdate, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    wchar_t refresh[] = L"schtasks.exe /Run /TN \\Microsoft\\Windows\\WindowsUpdate\\RefreshGroupPolicyCache";
    if (CreateProcessW(nullptr, refresh, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    wchar_t usoScan[] = L"USOClient.exe StartScan";
    if (CreateProcessW(nullptr, usoScan, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    wchar_t usoRefresh[] = L"USOClient.exe RefreshSettings";
    if (CreateProcessW(nullptr, usoRefresh, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

PolicyState QueryState() {
    PolicyState s;
    s.pauseEnabled = IsPauseEnabled();
    s.summary = GetStatusLine();
    return s;
}

bool IsPauseEnabled() {
    return ReadStateFlag(kStatePauseEnabled);
}

bool IsPauseEffectivelyActive() {
    if (IsPauseEnabled()) {
        return true;
    }
    if (ValueExists(HKEY_LOCAL_MACHINE, kWuPolicyKey, L"PauseFeatureUpdatesStartTime") ||
        ValueExists(HKEY_LOCAL_MACHINE, kWuPolicyKey, L"PauseQualityUpdatesStartTime") ||
        ValueExists(HKEY_LOCAL_MACHINE, kWuPolicyKey, L"DeferFeatureUpdates")) {
        return true;
    }

    wchar_t expiry[64]{};
    DWORD type = 0;
    DWORD size = sizeof(expiry);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings",
            L"PauseUpdatesExpiryTime",
            RRF_RT_REG_SZ | KEY_WOW64_64KEY,
            &type,
            expiry,
            &size) == ERROR_SUCCESS) {
        SYSTEMTIME st{};
        if (swscanf_s(expiry, L"%4hd-%2hd-%2hdT%2hd:%2hd:%2hdZ",
                &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) == 6) {
            FILETIME ftExpiry{}, ftNow{};
            SystemTimeToFileTime(&st, &ftExpiry);
            GetSystemTimeAsFileTime(&ftNow);
            ULARGE_INTEGER a{}, b{};
            a.LowPart = ftExpiry.dwLowDateTime;
            a.HighPart = ftExpiry.dwHighDateTime;
            b.LowPart = ftNow.dwLowDateTime;
            b.HighPart = ftNow.dwHighDateTime;
            if (a.QuadPart > b.QuadPart) {
                return true;
            }
        }
    }
    return false;
}

std::wstring GetStatusLine() {
    if (IsPauseEffectivelyActive()) {
        if (ValueExists(HKEY_LOCAL_MACHINE, kWuPolicyKey, L"PauseFeatureUpdatesStartTime")) {
            return L"Updates: BLOCKED (legacy org policy — use Restore)";
        }
        return IsPauseEnabled() ? L"Updates: Paused (Settings)" : L"Updates: Paused";
    }

    HKEY adminKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\Admininistration", 0, KEY_READ | KEY_WOW64_64KEY, &adminKey) == ERROR_SUCCESS) {
        RegCloseKey(adminKey);
        return L"Updates: LOCKED by administrator (use Restore)";
    }

    HKEY ps = nullptr;
    constexpr wchar_t kPolicyState[] = L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\PolicyState";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kPolicyState, 0, KEY_READ | KEY_WOW64_64KEY, &ps) == ERROR_SUCCESS) {
        const wchar_t* policyDriven[] = {
            L"SetPolicyDrivenUpdateSourceForFeatureUpdates",
            L"SetPolicyDrivenUpdateSourceForQualityUpdates",
            L"SetPolicyDrivenUpdateSourceForDriverUpdates",
            L"SetPolicyDrivenUpdateSourceForOtherUpdates",
        };
        bool locked = false;
        for (const wchar_t* name : policyDriven) {
            DWORD v = 0;
            if (ReadDword(ps, name, &v) && v == 0xFFFFFFFFu) {
                locked = true;
                break;
            }
        }
        RegCloseKey(ps);
        if (locked) {
            return L"Updates: LOCKED by administrator (use Restore)";
        }
    }

    return L"Updates: Ready (no policy locks)";
}

PolicyResult EnablePause() {
    HKEY ux = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings",
            0, KEY_WRITE | KEY_WOW64_64KEY, &ux) != ERROR_SUCCESS) {
        return PolicyResult::RegistryError;
    }

    const std::wstring expiry = UtcExpiryIso8601(35);
    const bool ok = WriteString(ux, L"PauseUpdatesExpiryTime", expiry);
    RegCloseKey(ux);
    if (!ok) {
        return PolicyResult::RegistryError;
    }

    SetPauseState(true);
    RefreshPolicyCache();
    return PolicyResult::Ok;
}

PolicyResult DisablePause() {
    ClearUxPauseState();
    SetPauseState(false);
    RefreshPolicyCache();
    return PolicyResult::Ok;
}

PolicyResult RecoverWindowsUpdateControl() {
    HKEY wu = nullptr;
    HKEY au = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kWuPolicyKey, 0, KEY_WRITE | KEY_WOW64_64KEY, &wu) == ERROR_SUCCESS) {
        DeleteLegacyAndBlockingValues(wu, nullptr);
        DeleteAllValuesInKey(wu);
        RegCloseKey(wu);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kWuAuKey, 0, KEY_WRITE | KEY_WOW64_64KEY, &au) == ERROR_SUCCESS) {
        DeleteLegacyAndBlockingValues(nullptr, au);
        DeleteAllValuesInKey(au);
        RegCloseKey(au);
    }

    DeletePolicyKeyTrees();
    ClearUxPauseState();
    SetPauseState(false);

    // Windows 11 25H2 can continue honoring stale policy snapshots even when the
    // underlying policy keys are gone. Blow away the whole UpdatePolicy tree.
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy");

    // Clear legacy administrator configurations that block updates and display organization notices
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\Admininistration");

    // Clean local Group Policy cache directories to prevent policies from being re-applied
    try {
        std::filesystem::remove_all(L"C:\\Windows\\System32\\GroupPolicy");
        std::filesystem::remove_all(L"C:\\Windows\\System32\\GroupPolicyUsers");
    } catch (...) {
        // Safe to ignore if folders are missing or locked
    }

    HKEY state = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kStateRoot, 0, KEY_WRITE | KEY_WOW64_64KEY, &state) == ERROR_SUCCESS) {
        RegDeleteValueW(state, L"UpdatePolicyEnabled");
        RegDeleteValueW(state, L"UpdatePolicyManaged");
        RegDeleteValueW(state, L"RebootProtectionActive");
        RegCloseKey(state);
    }

    RestartWindowsUpdateServices();
    RefreshPolicyCache();
    ResetUpdatePolicyPolicyState();
    RefreshPolicyCache();
    return PolicyResult::Ok;
}

} // namespace UpdatePolicyManager
