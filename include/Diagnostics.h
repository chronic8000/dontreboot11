#pragma once

#include <string>

namespace Diagnostics {

// Writes a diagnostic report next to dontreboot11.exe.
// Returns the full log path on success, empty string on failure.
std::wstring WriteReport(const wchar_t* reason);

}

