#include "SC4UserDirFix.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <string_view>

namespace {
    constexpr std::wstring_view kUserDirArgumentName = L"UserDir";
    constexpr std::wstring_view kCfgFileName = L"SimCity 4.cfg";

    struct InlineHook {
        uint16_t version;
        uintptr_t rva;
        std::array<uint8_t, 5> expected;
        void* destination;
        const char* name;
    };

    struct FixState {
        std::wstring logPath;
        std::string userDirCfgPath;
        SRWLOCK logLock = SRWLOCK_INIT;
    };

    FixState g_fixState;

    [[nodiscard]] std::string WideToUtf8(const std::wstring_view value) {
        if (value.empty()) {
            return {};
        }

        const int requiredSize = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (requiredSize <= 0) {
            return {};
        }

        std::string converted(static_cast<size_t>(requiredSize), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            requiredSize,
            nullptr,
            nullptr
        );
        return converted;
    }

    void AppendLogLine(const std::string_view message) noexcept {
        if (g_fixState.logPath.empty()) {
            return;
        }

        AcquireSRWLockExclusive(&g_fixState.logLock);

        const HANDLE file = CreateFileW(
            g_fixState.logPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (file != INVALID_HANDLE_VALUE) {
            SYSTEMTIME localTime{};
            GetLocalTime(&localTime);

            char prefix[64]{};
            const int prefixLength = std::snprintf(
                prefix,
                sizeof(prefix),
                "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
                static_cast<unsigned>(localTime.wYear),
                static_cast<unsigned>(localTime.wMonth),
                static_cast<unsigned>(localTime.wDay),
                static_cast<unsigned>(localTime.wHour),
                static_cast<unsigned>(localTime.wMinute),
                static_cast<unsigned>(localTime.wSecond),
                static_cast<unsigned>(localTime.wMilliseconds)
            );

            DWORD written = 0;
            if (prefixLength > 0) {
                WriteFile(file, prefix, static_cast<DWORD>(prefixLength), &written, nullptr);
            }

            if (!message.empty()) {
                WriteFile(file, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
            }

            static constexpr char kLineEnding[] = "\r\n";
            WriteFile(file, kLineEnding, static_cast<DWORD>(sizeof(kLineEnding) - 1), &written, nullptr);
            CloseHandle(file);
        }

        ReleaseSRWLockExclusive(&g_fixState.logLock);
    }

    void InitializeLogPath(HINSTANCE moduleInstance) noexcept {
        wchar_t modulePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(moduleInstance, modulePath, static_cast<DWORD>(std::size(modulePath)));
        if (length == 0 || length >= std::size(modulePath)) {
            return;
        }

        std::wstring logPath(modulePath, modulePath + length);
        const size_t separatorIndex = logPath.find_last_of(L"\\/");
        if (separatorIndex != std::wstring::npos) {
            logPath.resize(separatorIndex + 1);
        }
        else {
            logPath.clear();
        }

        logPath += L"SC4UserDirFix.log";
        g_fixState.logPath = std::move(logPath);
    }

    [[nodiscard]] bool EqualsIgnoreCase(const std::wstring_view lhs, const std::wstring_view rhs) noexcept {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (std::towlower(lhs[i]) != std::towlower(rhs[i])) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool TryReadNextCommandLineToken(
        const std::wstring_view commandLine,
        size_t& offset,
        std::wstring& token
    ) {
        while (offset < commandLine.size() && std::iswspace(commandLine[offset])) {
            ++offset;
        }

        if (offset >= commandLine.size()) {
            return false;
        }

        token.clear();
        bool inQuotes = false;

        while (offset < commandLine.size()) {
            const wchar_t ch = commandLine[offset];
            if (ch == L'"') {
                inQuotes = !inQuotes;
                ++offset;
                continue;
            }

            if (!inQuotes && std::iswspace(ch)) {
                break;
            }

            token.push_back(ch);
            ++offset;
        }

        return true;
    }

    [[nodiscard]] std::wstring TryGetUserDirArgument() {
        const wchar_t* const rawCommandLine = GetCommandLineW();
        if (!rawCommandLine || !rawCommandLine[0]) {
            AppendLogLine("GetCommandLineW returned an empty command line");
            return {};
        }

        const std::wstring_view commandLine(rawCommandLine);
        AppendLogLine("Command line: " + WideToUtf8(commandLine));

        size_t offset = 0;
        std::wstring token;

        while (TryReadNextCommandLineToken(commandLine, offset, token)) {
            if (token.size() < 2 || (token[0] != L'-' && token[0] != L'/')) {
                continue;
            }

            const std::wstring_view option(token.data() + 1, token.size() - 1);
            if (EqualsIgnoreCase(option, kUserDirArgumentName)) {
                std::wstring value;
                if (TryReadNextCommandLineToken(commandLine, offset, value)) {
                    return value;
                }
                return {};
            }

            if (option.size() <= kUserDirArgumentName.size() + 1) {
                continue;
            }

            if (!EqualsIgnoreCase(option.substr(0, kUserDirArgumentName.size()), kUserDirArgumentName)) {
                continue;
            }

            const wchar_t delimiter = option[kUserDirArgumentName.size()];
            if (delimiter != L':' && delimiter != L'=') {
                continue;
            }

            return std::wstring(option.substr(kUserDirArgumentName.size() + 1));
        }

        return {};
    }

    [[nodiscard]] std::string BuildCfgPathFromUserDir(const std::wstring_view userDir) {
        if (userDir.empty()) {
            return {};
        }

        std::wstring normalized(userDir);
        for (wchar_t& ch : normalized) {
            if (ch == L'/') {
                ch = L'\\';
            }
        }

        if (normalized.back() != L'\\') {
            normalized.push_back(L'\\');
        }

        normalized.append(kCfgFileName);

        const int requiredSize = WideCharToMultiByte(
            CP_ACP,
            0,
            normalized.c_str(),
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (requiredSize <= 1) {
            return {};
        }

        std::string converted(static_cast<size_t>(requiredSize), '\0');
        const int convertedSize = WideCharToMultiByte(
            CP_ACP,
            0,
            normalized.c_str(),
            -1,
            converted.data(),
            requiredSize,
            nullptr,
            nullptr
        );
        if (convertedSize <= 1) {
            return {};
        }

        converted.resize(static_cast<size_t>(convertedSize - 1));
        return converted;
    }

    using AssignStringFn = void(__thiscall*)(void* thisPtr, const char* data, int length);

    void AssignCfgPath(void* outString, const char* hookName) noexcept {
        if (!outString) {
            AppendLogLine(std::string(hookName) + ": output string pointer was null");
            return;
        }

        if (g_fixState.userDirCfgPath.empty()) {
            AppendLogLine(std::string(hookName) + ": cfg path was not initialized");
            return;
        }

        auto** const vtable = *reinterpret_cast<void***>(outString);
        auto* const fn = reinterpret_cast<AssignStringFn>(vtable[0x0c / sizeof(void*)]);
        fn(outString, g_fixState.userDirCfgPath.c_str(), static_cast<int>(g_fixState.userDirCfgPath.size()));

        AppendLogLine(std::string(hookName) + ": returning \"" + g_fixState.userDirCfgPath + "\"");
    }

    void __fastcall HookGetAppPreferencesFilePath(int*, void*, void* outString) {
        AssignCfgPath(outString, "GetAppPreferencesFilePath");
    }

    void __fastcall HookResolveAppPreferencesFilePath(int*, void*, void* outString) {
        AssignCfgPath(outString, "ResolveAppPreferencesFilePath");
    }

    const InlineHook kGetAppPreferencesFilePathHook641{
        641,
        0x0004C090,
        {0x83, 0xEC, 0x14, 0x53, 0x56},
        reinterpret_cast<void*>(&HookGetAppPreferencesFilePath),
        "GetAppPreferencesFilePath",
    };

    const InlineHook kResolveAppPreferencesFilePathHook641{
        641,
        0x0004D570,
        {0x83, 0xEC, 0x34, 0x53, 0x55},
        reinterpret_cast<void*>(&HookResolveAppPreferencesFilePath),
        "ResolveAppPreferencesFilePath",
    };

    [[nodiscard]] bool IsInlineJumpInstalled(const uint8_t* target) noexcept {
        return target[0] == 0xE9;
    }

    [[nodiscard]] SC4UserDirFixStatus ApplyHook(const InlineHook& hook) noexcept {
        auto* const moduleBase = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        if (!moduleBase) {
            AppendLogLine(std::string(hook.name) + ": GetModuleHandleW(nullptr) failed");
            return SC4UserDirFixStatus::ModuleNotFound;
        }

        auto* const target = moduleBase + hook.rva;
        if (IsInlineJumpInstalled(target)) {
            AppendLogLine(std::string(hook.name) + ": hook already present");
            return SC4UserDirFixStatus::AlreadyApplied;
        }

        if (std::memcmp(target, hook.expected.data(), hook.expected.size()) != 0) {
            char buffer[128]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s: target bytes did not match the expected 641 signature at RVA 0x%08lX",
                hook.name,
                static_cast<unsigned long>(hook.rva)
            );
            AppendLogLine(buffer);
            return SC4UserDirFixStatus::UnsupportedVersion;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(target, hook.expected.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            char buffer[128]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s: VirtualProtect failed at RVA 0x%08lX",
                hook.name,
                static_cast<unsigned long>(hook.rva)
            );
            AppendLogLine(buffer);
            return SC4UserDirFixStatus::ProtectFailed;
        }

        const auto relativeOffset = static_cast<int32_t>(
            reinterpret_cast<uint8_t*>(hook.destination) - (target + hook.expected.size())
        );

        target[0] = 0xE9;
        std::memcpy(target + 1, &relativeOffset, sizeof(relativeOffset));
        FlushInstructionCache(GetCurrentProcess(), target, hook.expected.size());

        DWORD restoredProtect = 0;
        VirtualProtect(target, hook.expected.size(), oldProtect, &restoredProtect);

        char buffer[160]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s: installed jump hook at RVA 0x%08lX",
            hook.name,
            static_cast<unsigned long>(hook.rva)
        );
        AppendLogLine(buffer);

        return SC4UserDirFixStatus::Applied;
    }
}

SC4UserDirFixStatus ApplySC4UserDirFix(HINSTANCE moduleInstance) noexcept {
    InitializeLogPath(moduleInstance);
    AppendLogLine("DLL_PROCESS_ATTACH received");

    const std::wstring userDir = TryGetUserDirArgument();
    if (userDir.empty()) {
        AppendLogLine("No -UserDir argument was present");
        return SC4UserDirFixStatus::NoUserDirArgument;
    }

    AppendLogLine("Parsed -UserDir: " + WideToUtf8(userDir));

    g_fixState.userDirCfgPath = BuildCfgPathFromUserDir(userDir);
    if (g_fixState.userDirCfgPath.empty()) {
        AppendLogLine("Failed to convert the parsed -UserDir argument into an ANSI cfg path");
        return SC4UserDirFixStatus::PathConversionFailed;
    }

    AppendLogLine("Resolved cfg path: " + g_fixState.userDirCfgPath);

    const std::array hooks{
        &kGetAppPreferencesFilePathHook641,
        &kResolveAppPreferencesFilePathHook641,
    };

    bool anyApplied = false;

    for (const InlineHook* hook : hooks) {
        const SC4UserDirFixStatus status = ApplyHook(*hook);
        switch (status) {
        case SC4UserDirFixStatus::Applied:
            anyApplied = true;
            break;
        case SC4UserDirFixStatus::AlreadyApplied:
            break;
        default:
            AppendLogLine("Fix application aborted before all hooks were installed");
            return status;
        }
    }

    const SC4UserDirFixStatus finalStatus = anyApplied ? SC4UserDirFixStatus::Applied
                                                       : SC4UserDirFixStatus::AlreadyApplied;
    switch (finalStatus) {
    case SC4UserDirFixStatus::Applied:
        AppendLogLine("SC4 UserDir fix applied successfully");
        break;
    case SC4UserDirFixStatus::AlreadyApplied:
        AppendLogLine("SC4 UserDir fix was already active");
        break;
    default:
        break;
    }

    return finalStatus;
}
