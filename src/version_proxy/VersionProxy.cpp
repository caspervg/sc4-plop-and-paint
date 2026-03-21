#include <Windows.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cstdint>

#include "SC4UserDirFix.h"

namespace {
    struct ProxyState {
        INIT_ONCE initOnce = INIT_ONCE_STATIC_INIT;
        HMODULE module = nullptr;
    };

    ProxyState g_proxyState;

    [[nodiscard]] HMODULE LoadRealVersionModule() noexcept {
        static constexpr wchar_t kVersionDllName[] = L"\\version.dll";

        InitOnceExecuteOnce(
            &g_proxyState.initOnce,
            [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
                std::array<wchar_t, MAX_PATH> path{};
                const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
                if (length == 0 || length >= path.size()) {
                    return FALSE;
                }

                if (length + (std::size(kVersionDllName) - 1) >= path.size()) {
                    return FALSE;
                }

                std::copy(std::begin(kVersionDllName), std::end(kVersionDllName), path.begin() + length);
                g_proxyState.module = LoadLibraryW(path.data());
                return g_proxyState.module != nullptr;
            },
            nullptr,
            nullptr
        );

        return g_proxyState.module;
    }

    template <typename T>
    [[nodiscard]] T ResolveVersionExport(const char* name) noexcept {
        const HMODULE module = LoadRealVersionModule();
        if (!module) {
            return nullptr;
        }

        return reinterpret_cast<T>(GetProcAddress(module, name));
    }
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoSizeA");
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoSizeW");
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoSizeExA");
    return fn ? fn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoSizeExW");
    return fn ? fn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

extern "C" BOOL WINAPI GetFileVersionInfoA(
    LPCSTR lptstrFilename,
    DWORD dwHandle,
    DWORD dwLen,
    LPVOID lpData
) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoA");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoW(
    LPCWSTR lptstrFilename,
    DWORD dwHandle,
    DWORD dwLen,
    LPVOID lpData
) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoW");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(
    DWORD dwFlags,
    LPCSTR lpwstrFilename,
    DWORD dwHandle,
    DWORD dwLen,
    LPVOID lpData
) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoExA");
    return fn ? fn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(
    DWORD dwFlags,
    LPCWSTR lpwstrFilename,
    DWORD dwHandle,
    DWORD dwLen,
    LPVOID lpData
) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoExW");
    return fn ? fn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle(
    DWORD dwFlags,
    HANDLE hFile,
    DWORD dwLen,
    LPVOID lpData
) {
    using Fn = BOOL(WINAPI*)(DWORD, HANDLE, DWORD, LPVOID);
    static const Fn fn = ResolveVersionExport<Fn>("GetFileVersionInfoByHandle");
    return fn ? fn(dwFlags, hFile, dwLen, lpData) : FALSE;
}

extern "C" DWORD WINAPI VerFindFileA(
    DWORD uFlags,
    LPCSTR szFileName,
    LPCSTR szWinDir,
    LPCSTR szAppDir,
    LPSTR szCurDir,
    PUINT lpuCurDirLen,
    LPSTR szDestDir,
    PUINT lpuDestDirLen
) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    static const Fn fn = ResolveVersionExport<Fn>("VerFindFileA");
    return fn ? fn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen) : 0;
}

extern "C" DWORD WINAPI VerFindFileW(
    DWORD uFlags,
    LPCWSTR szFileName,
    LPCWSTR szWinDir,
    LPCWSTR szAppDir,
    LPWSTR szCurDir,
    PUINT lpuCurDirLen,
    LPWSTR szDestDir,
    PUINT lpuDestDirLen
) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    static const Fn fn = ResolveVersionExport<Fn>("VerFindFileW");
    return fn ? fn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen) : 0;
}

extern "C" DWORD WINAPI VerInstallFileA(
    DWORD uFlags,
    LPCSTR szSrcFileName,
    LPCSTR szDestFileName,
    LPCSTR szSrcDir,
    LPCSTR szDestDir,
    LPCSTR szCurDir,
    LPSTR szTmpFile,
    PUINT lpuTmpFileLen
) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    static const Fn fn = ResolveVersionExport<Fn>("VerInstallFileA");
    return fn ? fn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen) : 0;
}

extern "C" DWORD WINAPI VerInstallFileW(
    DWORD uFlags,
    LPCWSTR szSrcFileName,
    LPCWSTR szDestFileName,
    LPCWSTR szSrcDir,
    LPCWSTR szDestDir,
    LPCWSTR szCurDir,
    LPWSTR szTmpFile,
    PUINT lpuTmpFileLen
) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    static const Fn fn = ResolveVersionExport<Fn>("VerInstallFileW");
    return fn ? fn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen) : 0;
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    static const Fn fn = ResolveVersionExport<Fn>("VerLanguageNameA");
    return fn ? fn(wLang, szLang, cchLang) : 0;
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    static const Fn fn = ResolveVersionExport<Fn>("VerLanguageNameW");
    return fn ? fn(wLang, szLang, cchLang) : 0;
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    static const Fn fn = ResolveVersionExport<Fn>("VerQueryValueA");
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    static const Fn fn = ResolveVersionExport<Fn>("VerQueryValueW");
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        (void)ApplySC4UserDirFix(instance);
    }

    return TRUE;
}
