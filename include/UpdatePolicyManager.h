#pragma once

#include <string>

// Windows 11 25H2: pause via Settings UX registry only. No GPO/policy keys
// (those block Download & install and show "your organisation").

namespace UpdatePolicyManager {

enum class PolicyResult {
    Ok,
    AccessDenied,
    RegistryError,
};

struct PolicyState {
    bool pauseEnabled = false;
    std::wstring summary;
};

PolicyState QueryState();

bool IsPauseEnabled();
bool IsPauseEffectivelyActive();

PolicyResult EnablePause();
PolicyResult DisablePause();

// Removes all policy keys, GPCache, UX pause, legacy WinRT state hooks.
PolicyResult RecoverWindowsUpdateControl();

std::wstring GetStatusLine();

} // namespace UpdatePolicyManager
