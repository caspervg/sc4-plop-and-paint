#pragma once

#include <cstdint>

namespace PlopAndPaint::Sidecar
{
    // Four-char-code helper. Produces a uint32 whose little-endian byte order
    // matches the literal characters on disk (e.g. 'S','P','4','D' in order).
    // SC4 is always little-endian x86, and cIGZOStream::SetUint32 writes raw LE.
    constexpr uint32_t FourCC(const char a, const char b, const char c, const char d) noexcept
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(a))
             | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
             | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
    }

    // File header magic, on-disk bytes are literally "SP4D".
    constexpr uint32_t kSidecarMagic = FourCC('S', 'P', '4', 'D');

    // Current writer version. Readers tolerate smaller and larger versions and
    // preserve unknown chunks / fields for round-trip.
    constexpr uint16_t kSidecarVersionMajor = 1;
    constexpr uint16_t kSidecarVersionMinor = 0;

    // Hard upper bounds to guard against malformed / corrupted input.
    // Each entry is ~ tens of bytes; 1 million is far beyond any real city.
    constexpr uint32_t kMaxChunks = 1024;
    constexpr uint32_t kMaxEntriesPerChunk = 1'000'000u;
    constexpr uint32_t kMaxFieldsPerEntry = 256;
    constexpr uint32_t kMaxChunkBytes = 64u * 1024u * 1024u;      // 64 MiB
    constexpr uint32_t kMaxEntryBytes = 1u * 1024u * 1024u;       // 1 MiB
    constexpr uint32_t kMaxFieldBytes = 256u * 1024u;             // 256 KiB

    // Chunk tags. New chunk tags can be added at any time; readers that don't
    // recognize a tag preserve the raw bytes and re-emit them verbatim on save.
    constexpr uint32_t kChunkDecalList = FourCC('D', 'E', 'C', 'L');

    // Field tags inside a DecalEntry. Same extensibility rules apply: unknown
    // fields are preserved and re-emitted unchanged. Only add new tags; never
    // re-use an existing tag for a different semantic meaning.
    //
    // Numeric field values are always stored in the canonical type noted here.
    // Length-prefixed blobs carry a u32 byte count followed by raw bytes.
    constexpr uint32_t kFieldWorldPos      = FourCC('W', 'P', 'O', 'S'); // 3 × float32
    constexpr uint32_t kFieldTextureKey    = FourCC('T', 'X', 'K', 'Y'); // 3 × uint32 (T,G,I)
    constexpr uint32_t kFieldUvSubrect     = FourCC('U', 'V', 'S', 'R'); // 4 × float32 + uint32 mode
    constexpr uint32_t kFieldDecalInfo     = FourCC('D', 'I', 'N', 'F'); // 7 × float32
    constexpr uint32_t kFieldOpacity       = FourCC('O', 'P', 'A', 'C'); // 1 × float32
    constexpr uint32_t kFieldUserFlags     = FourCC('U', 'F', 'L', 'G'); // 1 × uint32

    // TGI of the sidecar subfile inside the savegame DBPF. The values below are
    // scoped to this plugin's director ID and are unlikely to collide with any
    // SC4 internal resource type. If another plugin ever wants to interoperate
    // it can read this same TGI.
    constexpr uint32_t kSidecarType     = 0xE5C2B9A7u;  // matches director ID
    constexpr uint32_t kSidecarGroup    = 0x50503464u;  // "PP4d"
    constexpr uint32_t kSidecarInstance = 0x00000001u;  // reserved for future variants
}