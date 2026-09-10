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

// Narrow form of the same path, for the core APIs that take std::string. Empty
// when the directory is unknown, or when the path does not survive the ANSI
// codepage - callers must not fall back to a bare filename, which
// GetPrivateProfileString would resolve against the Windows directory.
std::string GetModulePath(const char* filename);

}  // namespace BioShockInfiniteHeadTracking
