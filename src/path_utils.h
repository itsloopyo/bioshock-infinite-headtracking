#pragma once

#include <string>

namespace BioShockInfiniteHeadTracking {

// Directory this DLL was loaded from, with a trailing separator, or an empty
// string when the module path could not be resolved. Wide throughout: the real
// path is UTF-16, and GetModuleFileNameA would replace anything outside the
// active ANSI codepage with '?' before we ever saw it.
std::wstring GetModuleDirectoryW();

// Wide path to a file beside this DLL. Empty when the directory is unknown.
std::wstring GetModulePathW(const char* filename);

// A folder, with its trailing separator, as the pre-canonical builds handed it to their
// INI reader: in the ANSI code page, or as its 8.3 alias when the code page cannot spell
// it. Empty when neither works, where those builds read no file at all.
std::string AnsiFolderPath(const std::wstring& folder);

}  // namespace BioShockInfiniteHeadTracking
