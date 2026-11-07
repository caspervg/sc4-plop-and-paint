#pragma once
#include <cstdint>
#include <optional>

#include "cGZPersistResourceKey.h"

// The S3D of a prop at zoom 5, south view, read from its exemplar's resource key properties.
// Follows ExemplarParser::resolveModelTgi_ in the app.
std::optional<cGZPersistResourceKey> FindPropModelKey(uint32_t exemplarGroup, uint32_t exemplarInstance);
