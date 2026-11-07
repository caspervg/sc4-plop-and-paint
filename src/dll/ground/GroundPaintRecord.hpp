#pragma once

#include <optional>
#include <span>
#include <vector>

class cISC4DBSegment;

// Plop and Paint's ground paint in the city save: a record of its own, next to SC4RenderServices'
// terrain paint record. Layout: magic 'PNPG', u32 version, u32 byte count, then that many bytes
// of the document's payload (CBOR).
namespace Ground::SaveRecord {
    // Writes the payload, or removes the record when there is nothing to keep.
    bool Write(cISC4DBSegment& segment, std::span<const char> payload);
    // The payload, or nothing when the city has no record (or an unreadable one).
    [[nodiscard]] std::optional<std::vector<char>> Read(cISC4DBSegment& segment);
}
