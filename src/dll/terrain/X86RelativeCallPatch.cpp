#include "X86RelativeCallPatch.hpp"

#include <Windows.h>

#include <cstring>
#include <limits>

#include "utils/Logger.h"

namespace
{
    constexpr size_t kCallInstructionLength = 5;
}

namespace TerrainDecal
{
    X86RelativeCallPatch::X86RelativeCallPatch(const std::string_view name,
                                               const uintptr_t callSiteAddress,
                                               void* hookFn)
    {
        Configure(name, callSiteAddress, hookFn);
    }

    void X86RelativeCallPatch::Configure(const std::string_view name, const uintptr_t callSiteAddress, void* hookFn)
    {
        name_ = name;
        callSiteAddress_ = callSiteAddress;
        hookFn_ = hookFn;
        originalRel_ = 0;
        originalTarget_ = 0;
        installed_ = false;
    }

    bool X86RelativeCallPatch::ComputeRelativeCallTarget(const uintptr_t callSiteAddress,
                                                         const uintptr_t targetAddress,
                                                         int32_t& relOut)
    {
        const auto delta = static_cast<int64_t>(targetAddress) -
                           static_cast<int64_t>(callSiteAddress + kCallInstructionLength);
        if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max()) {
            return false;
        }

        relOut = static_cast<int32_t>(delta);
        return true;
    }

    bool X86RelativeCallPatch::Install()
    {
        if (installed_) {
            return true;
        }

        if (callSiteAddress_ == 0 || hookFn_ == nullptr) {
            LOG_ERROR("TerrainDecalHook: incomplete call patch configuration for {}", name_);
            return false;
        }

        auto* const site = reinterpret_cast<uint8_t*>(callSiteAddress_);
        if (site[0] != 0xE8) {
            LOG_ERROR("TerrainDecalHook: expected CALL rel32 at 0x{:08X} for {}",
                      static_cast<uint32_t>(callSiteAddress_), name_);
            return false;
        }

        std::memcpy(&originalRel_, site + 1, sizeof(originalRel_));
        originalTarget_ = callSiteAddress_ + kCallInstructionLength + originalRel_;

        int32_t newRel = 0;
        if (!ComputeRelativeCallTarget(callSiteAddress_, reinterpret_cast<uintptr_t>(hookFn_), newRel)) {
            LOG_ERROR("TerrainDecalHook: rel32 range failure for {}", name_);
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(site + 1, sizeof(newRel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("TerrainDecalHook: VirtualProtect failed for {}", name_);
            return false;
        }

        std::memcpy(site + 1, &newRel, sizeof(newRel));
        FlushInstructionCache(GetCurrentProcess(), site, kCallInstructionLength);
        VirtualProtect(site + 1, sizeof(newRel), oldProtect, &oldProtect);

        installed_ = true;
        return true;
    }

    void X86RelativeCallPatch::Uninstall()
    {
        if (!installed_) {
            return;
        }

        auto* const site = reinterpret_cast<uint8_t*>(callSiteAddress_);
        DWORD oldProtect = 0;
        if (VirtualProtect(site + 1, sizeof(originalRel_), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(site + 1, &originalRel_, sizeof(originalRel_));
            FlushInstructionCache(GetCurrentProcess(), site, kCallInstructionLength);
            VirtualProtect(site + 1, sizeof(originalRel_), oldProtect, &oldProtect);
        }

        installed_ = false;
    }

    bool X86RelativeCallPatch::IsInstalled() const noexcept
    {
        return installed_;
    }

    uintptr_t X86RelativeCallPatch::GetOriginalTarget() const noexcept
    {
        return originalTarget_;
    }

    uintptr_t X86RelativeCallPatch::GetCallSiteAddress() const noexcept
    {
        return callSiteAddress_;
    }
}

