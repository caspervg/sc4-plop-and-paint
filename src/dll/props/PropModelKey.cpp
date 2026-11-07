#include "PropModelKey.hpp"

#include <algorithm>
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

    std::span<const uint32_t> Values(const cISCPropertyHolder* holder, const uint32_t id) {
        const cISCProperty* property = holder->GetProperty(id);
        const cIGZVariant* value = property ? property->GetPropertyValue() : nullptr;
        if (!value || (value->GetType() & ~cIGZVariant::TypeArray) != cIGZVariant::Uint32 || !value->RefUint32()) {
            return {};
        }
        return {value->RefUint32(), value->GetCount()};
    }

    PropModel Model(const uint32_t type, const uint32_t group, const bool perView) {
        PropModel model;
        model.type = type != 0 ? type : kTypeS3D;
        model.group = group;
        model.perView = perView;
        return model;
    }

    // One model for every zoom and rotation.
    PropModel Single(const uint32_t type, const uint32_t group, const uint32_t instance) {
        PropModel model = Model(type, group, false);
        model.instances.fill(instance);
        return model;
    }

    // base + zoom * 0x100 + rotation * 0x10
    PropModel Inferred(const uint32_t type, const uint32_t group, const uint32_t base) {
        PropModel model = Model(type, group, true);
        for (uint32_t i = 0; i < model.instances.size(); ++i) {
            model.instances[i] = base + (i / 4) * 0x100 + (i % 4) * 0x10;
        }
        return model;
    }

    std::optional<PropModel> FromHolder(const cISCPropertyHolder* holder) {
        if (const auto v = Values(holder, kRkt0); v.size() >= 3) {
            return Single(v[0], v[1], v[2]);
        }
        if (const auto v = Values(holder, kRkt1); v.size() >= 3) {
            return Inferred(v[0], v[1], v[2]);
        }
        if (const auto v = Values(holder, kRkt2); v.size() >= 22) {
            PropModel model = Model(v[0], v[1], true);
            std::copy_n(v.begin() + 2, 20, model.instances.begin());
            return model;
        }
        if (const auto v = Values(holder, kRkt3); v.size() >= 7) {
            PropModel model = Model(v[0], v[1], false);
            for (size_t i = 0; i < model.instances.size(); ++i) {
                model.instances[i] = v[2 + i / 4];
            }
            return model;
        }
        // Records of 8: state, X, Y, Z, resource key type, T, G, I. Timed props may only have a
        // model in state 1.
        if (const auto v = Values(holder, kRkt4); !v.empty()) {
            for (const uint32_t state : {0u, 1u}) {
                for (size_t i = 0; i + 8 <= v.size(); i += 8) {
                    if (v[i] != state || (v[i + 6] == 0 && v[i + 7] == 0)) {
                        continue;
                    }
                    return v[i + 4] == kRkt0 ? Single(v[i + 5], v[i + 6], v[i + 7])
                                             : Inferred(v[i + 5], v[i + 6], v[i + 7]);
                }
            }
        }
        if (const auto v = Values(holder, kRkt5); v.size() >= 3) {
            return Inferred(v[0], v[1], v[2]); // state 0
        }
        return std::nullopt;
    }
}

std::optional<PropModel> FindPropModel(const uint32_t exemplarGroup, const uint32_t exemplarInstance) {
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
    return holder ? FromHolder(holder) : std::nullopt;
}
