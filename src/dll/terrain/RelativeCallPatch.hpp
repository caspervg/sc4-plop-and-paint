#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace TerrainDecal
{
    class RelativeCallPatch final
    {
    public:
        RelativeCallPatch() = default;
        RelativeCallPatch(std::string_view name, uintptr_t callSiteAddress, void* hookFn);

        void Configure(std::string_view name, uintptr_t callSiteAddress, void* hookFn);

        [[nodiscard]] bool Install();
        void Uninstall();

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] uintptr_t GetOriginalTarget() const noexcept;
        [[nodiscard]] uintptr_t GetCallSiteAddress() const noexcept;

    private:
        static bool ComputeRelativeCallTarget(uintptr_t callSiteAddress, uintptr_t targetAddress, int32_t& relOut);

    private:
        std::string name_;
        uintptr_t callSiteAddress_ = 0;
        void* hookFn_ = nullptr;
        int32_t originalRel_ = 0;
        uintptr_t originalTarget_ = 0;
        bool installed_ = false;
    };
}

