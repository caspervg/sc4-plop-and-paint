#include "PropModelKey.hpp"

#include <span>

#include "GZServPtrs.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "cISCResExemplar.h"
#include "cRZAutoRefCount.h"

namespace {
    constexpr uint32_t kTypeExemplar = 0x6534284A;
    constexpr uint32_t kTypeS3D = 0x5AD0E817;
    constexpr uint32_t kRkt0 = 0x27812820;
    constexpr uint32_t kRkt1 = 0x27812821;
    constexpr uint32_t kRkt2 = 0x27812822;
    constexpr uint32_t kRkt3 = 0x27812823;
    constexpr uint32_t kRkt4 = 0x27812824;
    constexpr uint32_t kRkt5 = 0x27812825;

    constexpr uint32_t kZoom = 5;
    // RKT1 and RKT5 store the zoom 1 south model, the others follow at these steps.
    constexpr uint32_t kZoomOffset = (kZoom - 1) * 0x100;

    std::span<const uint32_t> Values(const cISCPropertyHolder* holder, const uint32_t id) {
        const cISCProperty* property = holder->GetProperty(id);
        const cIGZVariant* value = property ? property->GetPropertyValue() : nullptr;
        if (!value || (value->GetType() & ~cIGZVariant::TypeArray) != cIGZVariant::Uint32 || !value->RefUint32()) {
            return {};
        }
        return {value->RefUint32(), value->GetCount()};
    }

    uint32_t TypeOrS3D(const uint32_t type) {
        return type != 0 ? type : kTypeS3D;
    }

    std::optional<cGZPersistResourceKey> FromHolder(const cISCPropertyHolder* holder, const uint32_t exemplarGroup) {
        if (const auto v = Values(holder, kRkt0); v.size() >= 3) {
            return cGZPersistResourceKey(TypeOrS3D(v[0]), v[1], v[2]);
        }
        if (const auto v = Values(holder, kRkt1); v.size() >= 3) {
            return cGZPersistResourceKey(TypeOrS3D(v[0]), v[1], v[2] + kZoomOffset);
        }
        if (const auto v = Values(holder, kRkt5); v.size() >= 3) {
            return cGZPersistResourceKey(TypeOrS3D(v[0]), v[1], v[2] + kZoomOffset);
        }
        if (const auto v = Values(holder, kRkt3); v.size() > 2 + kZoom - 1) {
            return cGZPersistResourceKey(TypeOrS3D(v[0]), v[1], v[2 + kZoom - 1]);
        }
        if (const auto v = Values(holder, kRkt2); v.size() > 2 + (kZoom - 1) * 4) {
            return cGZPersistResourceKey(kTypeS3D, exemplarGroup, v[2 + (kZoom - 1) * 4]);
        }
        // Blocks of 8: state, 3 unused, resource key type, T, G, I. Timed props may only have a
        // model in state 1.
        if (const auto v = Values(holder, kRkt4); !v.empty()) {
            for (const uint32_t state : {0u, 1u}) {
                for (size_t i = 0; i + 8 <= v.size(); i += 8) {
                    if (v[i] != state || (v[i + 6] == 0 && v[i + 7] == 0)) {
                        continue;
                    }
                    const uint32_t offset = v[i + 4] == 0 || v[i + 4] == kRkt1 ? kZoomOffset : 0;
                    return cGZPersistResourceKey(TypeOrS3D(v[i + 5]), v[i + 6], v[i + 7] + offset);
                }
            }
        }
        return std::nullopt;
    }
}

std::optional<cGZPersistResourceKey> FindPropModelKey(const uint32_t exemplarGroup, const uint32_t exemplarInstance) {
    cIGZPersistResourceManagerPtr resourceManager;
    if (!resourceManager) {
        return std::nullopt;
    }

    cISCResExemplar* exemplar = nullptr;
    const cGZPersistResourceKey key(kTypeExemplar, exemplarGroup, exemplarInstance);
    if (!resourceManager->GetResource(key, GZIID_cISCResExemplar, reinterpret_cast<void**>(&exemplar), 0, nullptr) ||
        !exemplar) {
        return std::nullopt;
    }
    cRZAutoRefCount<cISCResExemplar> exemplarRef(exemplar);

    const cISCPropertyHolder* holder = exemplar->AsISCPropertyHolder();
    return holder ? FromHolder(holder, exemplarGroup) : std::nullopt;
}
