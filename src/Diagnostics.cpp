#include "../include/Diagnostics.h"

#include <windows.h>
#include <string>
#include <vector>

namespace {

std::wstring GetExePath() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return buf;
}

std::wstring GetDirName(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return path.substr(0, pos);
}

std::wstring NowStamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t b[64]{};
    swprintf_s(b, L"%04u-%02u-%02u_%02u-%02u-%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

bool IsAdmin() {
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

void AppendLine(HANDLE h, const std::wstring& s) {
    std::wstring line = s + L"\r\n";
    DWORD written = 0;
    WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &written, nullptr);
}

std::wstring FormatHex(DWORD v) {
    wchar_t b[32]{};
    swprintf_s(b, L"0x%08X", v);
    return b;
}

std::wstring QueryRegValueString(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    wchar_t buf[512]{};
    DWORD type = 0;
    DWORD size = sizeof(buf);
    const LONG r = RegGetValueW(root, subKey, value, RRF_RT_REG_SZ | KEY_WOW64_64KEY, &type, buf, &size);
    if (r != ERROR_SUCCESS) return L"";
    return buf;
}

bool QueryRegValueDword(HKEY root, const wchar_t* subKey, const wchar_t* value, DWORD& out) {
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    const LONG r = RegGetValueW(root, subKey, value, RRF_RT_REG_DWORD | KEY_WOW64_64KEY, &type, &out, &size);
    return r == ERROR_SUCCESS;
}

void DumpKeyValuesShallow(HANDLE h, HKEY root, const wchar_t* subKey) {
    HKEY k = nullptr;
    LONG r = RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &k);
    if (r != ERROR_SUCCESS) {
        AppendLine(h, L"  (missing) " + std::wstring(subKey) + L"  err=" + FormatHex((DWORD)r));
        return;
    }

    DWORD valueCount = 0;
    DWORD maxValueNameLen = 0;
    DWORD maxValueLen = 0;
    RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &valueCount, &maxValueNameLen, &maxValueLen, nullptr, nullptr);

    std::vector<wchar_t> name(maxValueNameLen + 2);
    std::vector<BYTE> data(maxValueLen + 2);

    for (DWORD i = 0; i < valueCount; i++) {
        DWORD nameLen = (DWORD)name.size();
        DWORD type = 0;
        DWORD dataLen = (DWORD)data.size();
        r = RegEnumValueW(k, i, name.data(), &nameLen, nullptr, &type, data.data(), &dataLen);
        if (r != ERROR_SUCCESS) continue;

        std::wstring line = L"  " + std::wstring(name.data(), nameLen) + L" = ";
        if (type == REG_DWORD && dataLen == sizeof(DWORD)) {
            DWORD v = *(DWORD*)data.data();
            line += FormatHex(v) + L" (" + std::to_wstring(v) + L")";
        } else if (type == REG_SZ) {
            line += L"\"" + std::wstring((wchar_t*)data.data()) + L"\"";
        } else {
            line += L"(type " + std::to_wstring(type) + L", " + std::to_wstring(dataLen) + L" bytes)";
        }
        AppendLine(h, line);
    }

    RegCloseKey(k);
}

void DumpKeyExists(HANDLE h, HKEY root, const wchar_t* subKey) {
    HKEY k = nullptr;
    LONG r = RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &k);
    if (r == ERROR_SUCCESS) {
        AppendLine(h, L"  present: " + std::wstring(subKey));
        RegCloseKey(k);
    } else {
        AppendLine(h, L"  missing: " + std::wstring(subKey) + L"  err=" + FormatHex((DWORD)r));
    }
}

void DumpServiceState(HANDLE h, const wchar_t* svcName) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        AppendLine(h, L"  OpenSCManager failed");
        return;
    }
    SC_HANDLE svc = OpenServiceW(scm, svcName, SERVICE_QUERY_STATUS);
    if (!svc) {
        AppendLine(h, L"  " + std::wstring(svcName) + L": not found / no access");
        CloseServiceHandle(scm);
        return;
    }
    SERVICE_STATUS_PROCESS ssp{};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        AppendLine(h, L"  " + std::wstring(svcName) + L": state=" + std::to_wstring(ssp.dwCurrentState) + L" pid=" + std::to_wstring(ssp.dwProcessId));
    } else {
        AppendLine(h, L"  " + std::wstring(svcName) + L": QueryServiceStatusEx failed");
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

} // namespace

namespace Diagnostics {

std::wstring WriteReport(const wchar_t* reason) {
    const std::wstring exe = GetExePath();
    const std::wstring dir = GetDirName(exe);
    if (dir.empty()) return L"";

    const std::wstring logPath = dir + L"\\dontreboot11-diagnostics-" + NowStamp() + L".log";

    HANDLE h = CreateFileW(
        logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";

    // Write UTF-16LE BOM for Notepad friendliness.
    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(h, &bom, sizeof(bom), &written, nullptr);

    AppendLine(h, L"Don't Reboot 11 diagnostics");
    AppendLine(h, L"Reason: " + std::wstring(reason ? reason : L"(none)"));
    AppendLine(h, L"Exe: " + exe);
    AppendLine(h, L"Admin: " + std::wstring(IsAdmin() ? L"true" : L"false"));

    // Prefer RtlGetVersion to avoid GetVersionExW deprecation and manifest/version-lie behavior.
    typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto rtlGetVersion = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW rvi{};
            rvi.dwOSVersionInfoSize = sizeof(rvi);
            if (rtlGetVersion(&rvi) == 0) {
                AppendLine(h, L"OS Version: " + std::to_wstring(rvi.dwMajorVersion) + L"." +
                    std::to_wstring(rvi.dwMinorVersion) + L" build " + std::to_wstring(rvi.dwBuildNumber));
            }
        }
    }

    AppendLine(h, L"");
    AppendLine(h, L"== Windows Update policy keys (should be empty on consumer) ==");
    DumpKeyValuesShallow(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate");
    DumpKeyValuesShallow(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU");

    AppendLine(h, L"");
    AppendLine(h, L"== PolicyManager (CSP/MDM) Update branch (if present) ==");
    DumpKeyExists(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PolicyManager\\current\\device\\Update");
    DumpKeyValuesShallow(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PolicyManager\\current\\device\\Update");

    AppendLine(h, L"");
    AppendLine(h, L"== UpdatePolicy PolicyState (org UI if SetPolicyDriven* = 0xFFFFFFFF) ==");
    DumpKeyValuesShallow(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\PolicyState");
    {
        const wchar_t* driven[] = {
            L"SetPolicyDrivenUpdateSourceForFeatureUpdates",
            L"SetPolicyDrivenUpdateSourceForQualityUpdates",
            L"SetPolicyDrivenUpdateSourceForDriverUpdates",
            L"SetPolicyDrivenUpdateSourceForOtherUpdates",
        };
        bool orgLike = false;
        for (const wchar_t* name : driven) {
            DWORD v = 0;
            if (QueryRegValueDword(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\PolicyState", name, v) &&
                v == 0xFFFFFFFFu) {
                orgLike = true;
                break;
            }
        }
        if (orgLike) {
            AppendLine(h, L"  >>> WARNING: PolicyState marks updates as policy-driven (org scheduling UI).");
            AppendLine(h, L"  >>> Use tray: Give me Windows Update control back, then reopen Settings.");
        } else {
            AppendLine(h, L"  Policy-driven flags: OK (not 0xFFFFFFFF).");
        }
    }

    AppendLine(h, L"");
    AppendLine(h, L"== UpdatePolicy Settings / GPCache ==");
    DumpKeyValuesShallow(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\Settings");
    DumpKeyExists(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\GPCache");
    DumpKeyValuesShallow(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UpdatePolicy\\GPCache");

    AppendLine(h, L"");
    AppendLine(h, L"== UX pause ==");
    const auto pauseExpiry = QueryRegValueString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings", L"PauseUpdatesExpiryTime");
    AppendLine(h, L"  PauseUpdatesExpiryTime = " + (pauseExpiry.empty() ? L"(missing)" : (L"\"" + pauseExpiry + L"\"")));

    AppendLine(h, L"");
    AppendLine(h, L"== Restart-required flags ==");
    DumpKeyExists(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");
    DumpKeyExists(h, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending");

    AppendLine(h, L"");
    AppendLine(h, L"== Windows Update services ==");
    DumpServiceState(h, L"wuauserv");
    DumpServiceState(h, L"UsoSvc");
    DumpServiceState(h, L"WaaSMedicSvc");

    CloseHandle(h);
    return logPath;
}

} // namespace Diagnostics

