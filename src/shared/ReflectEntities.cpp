#include "ReflectEntities.h"

namespace sc4::shared {

LotMetadata LotMetadata::CreateSample()
{
    return LotMetadata{
        42,
        "Downtown Elevation",
        0.73f,
    };
}

LotNamedTuple LotMetadata::AsTuple() const
{
    return LotNamedTuple(
        LotIdField(lotId),
        LotNameField(lotName),
        LotDensityField(buildDensity)
    );
}

LotMetadata LotMetadata::FromTuple(const LotNamedTuple& tuple)
{
    return LotMetadata{
        rfl::get<LotIdField>(tuple),
        rfl::get<LotNameField>(tuple),
        rfl::get<LotDensityField>(tuple),
    };
}

std::array<std::string_view, 3> LotMetadata::FieldNames()
{
    return {
        LotIdField::name(),
        LotNameField::name(),
        LotDensityField::name(),
    };
}

} // namespace sc4::shared
