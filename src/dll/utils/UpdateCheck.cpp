#include "UpdateCheck.hpp"

#include "cRZBaseString.h"
#include "utils/VersionDetection.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <shellapi.h>
#include <wininet.h>

#include <array>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

namespace
{
    using Version = std::array<int, 3>;

    std::mutex noticeMutex;
    std::string notice;
    std::string releasesPage;

    std::string WithoutPrefix(std::string text)
    {
        if (text.starts_with('v')) {
            text.erase(0, 1);
        }
        return text;
    }

    std::optional<Version> ParseVersion(const std::string& text)
    {
        Version version{};
        if (sscanf_s(text.c_str(), "%d.%d.%d", &version[0], &version[1], &version[2]) != 3) {
            return std::nullopt;
        }
        return version;
    }

    // The tag of the latest release, or nothing when GitHub cannot be reached.
    std::optional<std::string> LatestReleaseTag(const std::string& repository, const std::string& userAgent)
    {
        HINTERNET session = InternetOpenA(userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!session) {
            return std::nullopt;
        }
        DWORD timeout = 3000;
        InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        const std::string url = "https://api.github.com/repos/" + repository + "/releases/latest";
        std::string body;
        HINTERNET request = InternetOpenUrlA(session, url.c_str(), "Accept: application/vnd.github+json\r\n",
                                             static_cast<DWORD>(-1),
                                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                             INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI | INTERNET_FLAG_SECURE, 0);
        if (request) {
            char buffer[4096];
            DWORD read = 0;
            while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0 && body.size() < 1 << 20) {
                body.append(buffer, read);
            }
            InternetCloseHandle(request);
        }
        InternetCloseHandle(session);

        const size_t key = body.find("\"tag_name\"");
        const size_t open = key == std::string::npos ? key : body.find('"', body.find(':', key));
        const size_t close = open == std::string::npos ? open : body.find('"', open + 1);
        if (close == std::string::npos) {
            return std::nullopt;
        }
        return body.substr(open + 1, close - open - 1);
    }

    void Check(const std::string& repository, const std::string& product, const std::string& installedVersion)
    {
        const std::string current = WithoutPrefix(installedVersion);
        const std::optional<Version> installed = ParseVersion(current);
        if (!installed || *installed == Version{}) {
            LOG_INFO("Update check: skipped for a development build");
            return;
        }
        const std::optional<std::string> tag = LatestReleaseTag(repository, product + "/" + current);
        const std::string newest = tag ? WithoutPrefix(*tag) : std::string();
        const std::optional<Version> latest = ParseVersion(newest);
        if (!latest) {
            LOG_INFO("Update check: GitHub could not be reached");
            return;
        }
        if (*latest <= *installed) {
            LOG_INFO("Update check: v{} is the latest release", current);
            return;
        }
        std::lock_guard lock(noticeMutex);
        notice = product + " v" + newest + " is out.\nYou have v" + current + ".";
        releasesPage = "https://github.com/" + repository + "/releases";
        LOG_INFO("Update check: {} v{} is out, you have v{}", product, newest, current);
    }
}

void UpdateCheck::Start(const std::string& repository, const std::string& product, const std::string& installedVersion)
{
    std::thread(Check, repository, product, installedVersion).detach();
}

void UpdateCheck::ShowNotice()
{
    std::string text, url;
    {
        std::lock_guard lock(noticeMutex);
        text.swap(notice);
        url.swap(releasesPage);
    }
    if (text.empty() || VersionDetection::GetInstance().GetGameVersion() != 641) {
        return;
    }
    // SC4's own two-button dialog. It waits for a click and returns true for the first button.
    using ShowConfirmation = bool(__cdecl*)(const cIGZString& caption, const cIGZString& message,
                                            const cIGZString* confirmLabel, const cIGZString* cancelLabel);
    const cRZBaseString download("Download"), later("Later");
    if (reinterpret_cast<ShowConfirmation>(0x78DFF0)(cRZBaseString("Update available"), cRZBaseString(text.c_str()),
                                                     &download, &later)) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
