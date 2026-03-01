#include "LotRepository.hpp"

#include <unordered_set>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "Utils.hpp"
#include "rfl/cbor/load.hpp"
#include "spdlog/spdlog.h"

void LotRepository::Load() {
    try {
        const auto pluginsPath = GetPluginsPath_();
        const auto cborPath = pluginsPath / "lot_configs.cbor";

        if (!std::filesystem::exists(cborPath)) {
            spdlog::warn("Lot config CBOR file not found: {}", cborPath.string());
            return;
        }

        auto result = rfl::cbor::load<std::vector<Building>>(cborPath.string());
        if (result) {
            buildings_ = std::move(*result);
            buildingsById_ = std::unordered_map<uint64_t, Building>(buildings_.size());

            size_t lotCount = 0;
            std::unordered_set<uint64_t> lotKeys;
            size_t duplicateLots = 0;
            for (const auto& b : buildings_) {
                buildingsById_.emplace(MakeGIKey(b.groupId.value(), b.instanceId.value()), b);
                for (const auto& lot : b.lots) {
                    ++lotCount;
                    const uint64_t key = MakeGIKey(lot.groupId.value(), lot.instanceId.value());
                    if (!lotKeys.insert(key).second) {
                        ++duplicateLots;
                        spdlog::warn("Duplicate lot in CBOR: group=0x{:08X}, instance=0x{:08X}",
                                     lot.groupId.value(), lot.instanceId.value());
                    }
                    lotsById_.emplace(key, lot);
                }
            }

            spdlog::info("Loaded {} buildings / {} lots from {}", buildings_.size(), lotCount, cborPath.string());
            if (duplicateLots > 0) {
                spdlog::warn("Detected {} duplicate lot IDs in CBOR", duplicateLots);
            }
        }
        else {
            spdlog::error("Failed to load lots from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error loading lots: {}", e.what());
    }
}

std::filesystem::path LotRepository::GetPluginsPath_() {
    try {
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());
        return std::filesystem::path(modulePath.get()).parent_path();
    }
    catch (const wil::ResultException& e) {
        spdlog::error("LotRepository: Failed to get DLL directory: {}", e.what());
        return {};
    }
}
