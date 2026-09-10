#include "path_utils.h"

#include <windows.h>

#include <vector>

namespace BioShockInfiniteHeadTracking {

namespace {

void DummyAddress() {}

// Narrows a path for the core APIs that take std::string.
//
// CP_UTF8 (and CP_UTF7) reject a non-null lpUsedDefaultChar and any dwFlags outright
// with ERROR_INVALID_PARAMETER, so asking for lossiness reporting on a system with the
// UTF-8 ANSI codepage - which Windows 10 1903 lets a user turn on globally - fails the
// conversion of an ordinary ASCII path and would leave the mod dormant on it.
bool NarrowPath(const std::wstring& wide, std::string* out) {
    const UINT acp = GetACP();
    const bool reportsLossy = acp != CP_UTF8 && acp != CP_UTF7;
    const DWORD flags = reportsLossy ? WC_NO_BEST_FIT_CHARS : 0;
    BOOL usedDefault = FALSE;
    BOOL* usedDefaultOut = reportsLossy ? &usedDefault : nullptr;

    const int len = WideCharToMultiByte(acp, flags, wide.c_str(), -1, nullptr, 0, nullptr,
                                        usedDefaultOut);
    if (len <= 1 || usedDefault) {
        return false;
    }
    std::string narrow(static_cast<size_t>(len), '\0');
    if (WideCharToMultiByte(acp, flags, wide.c_str(), -1, &narrow[0], len, nullptr,
                            usedDefaultOut) == 0 || usedDefault) {
        return false;
    }
    narrow.pop_back();
    *out = narrow;
    return true;
}

}  // namespace

std::wstring GetModuleDirectoryW() {
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DummyAddress), &hModule) || hModule == nullptr) {
        return {};
    }

    // A game installed deep enough to pass MAX_PATH truncates silently otherwise:
    // GetModuleFileNameW fills the buffer, returns its size and sets
    // ERROR_INSUFFICIENT_BUFFER, so the short read looks like a complete path.
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written = GetModuleFileNameW(hModule, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            break;
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }

    std::wstring path(buffer.data());
    const size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, lastSlash + 1);
}

std::wstring GetModulePathW(const char* filename) {
    const std::wstring dir = GetModuleDirectoryW();
    if (dir.empty()) {
        return {};
    }
    std::wstring wide(dir);
    while (*filename) {
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*filename++)));
    }
    return wide;
}

std::string GetModulePath(const char* filename) {
    const std::wstring dir = GetModuleDirectoryW();
    if (dir.empty()) {
        return {};
    }

    std::string narrowDir;
    if (!NarrowPath(dir, &narrowDir)) {
        // The install path has characters the ANSI codepage cannot spell, so the narrow
        // form would name a different directory - one containing '?'. The 8.3 alias is
        // the same path written in ASCII, which is why it is tried rather than treated
        // as a failure: a Cyrillic or CJK Steam library is a normal install.
        //
        // Aliased on the DIRECTORY, which always exists. GetShortPathNameW resolves each
        // component on disk, so pointing it at the file would fail with
        // ERROR_FILE_NOT_FOUND on the very first launch - before the INI has been
        // written - and the INI is only ever written after this returns. That is a
        // fallback that could never once have fired.
        std::vector<wchar_t> shortDir(MAX_PATH);
        for (;;) {
            const DWORD written = GetShortPathNameW(dir.c_str(), shortDir.data(),
                                                    static_cast<DWORD>(shortDir.size()));
            if (written == 0) {
                return {};
            }
            if (written < shortDir.size()) {
                break;
            }
            shortDir.resize(written + 1);
        }
        if (!NarrowPath(std::wstring(shortDir.data()), &narrowDir)) {
            // 8.3 alias generation is off for this volume, so the path has no ASCII
            // spelling. Returning the bare filename instead would send
            // GetPrivateProfileString to the Windows directory, to read someone else's
            // file as this mod's config.
            return {};
        }
    }

    // The filename is an ASCII literal from this file, so appending it after the
    // conversion keeps it out of the codepage question entirely.
    return narrowDir + filename;
}

}  // namespace BioShockInfiniteHeadTracking
