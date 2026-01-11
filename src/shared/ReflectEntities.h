#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

#include <rfl/Field.hpp>
#include <rfl/NamedTuple.hpp>
#include <rfl/get.hpp>
#include <rfl/make_named_tuple.hpp>
#include <rfl/internal/StringLiteral.hpp>

namespace sc4::shared {
namespace detail {

template <std::size_t N>
consteval auto FieldName(const char (&literal)[N]) {
    return rfl::internal::StringLiteral<N>(literal);
}

} // namespace detail

using LotIdField = rfl::Field<detail::FieldName("lot_id"), int32_t>;
using LotNameField = rfl::Field<detail::FieldName("lot_name"), std::string>;
using LotDensityField =
    rfl::Field<detail::FieldName("build_density"), float>;

using LotNamedTuple =
    rfl::NamedTuple<LotIdField, LotNameField, LotDensityField>;

struct LotMetadata {
    int32_t lotId = 0;
    std::string lotName;
    float buildDensity = 0.0f;

    [[nodiscard]] LotNamedTuple AsTuple() const;
    static LotMetadata FromTuple(const LotNamedTuple& tuple);
    static std::array<std::string_view, 3> FieldNames();
    static LotMetadata CreateSample();
};

} // namespace sc4::shared
