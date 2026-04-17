#pragma once

#include <string>

#include "DecalSidecarPayload.hpp"

class cIGZIStream;

namespace PlopAndPaint::Sidecar
{
    struct ReadResult
    {
        bool ok = false;
        std::string error; // empty on success
    };

    // Deserializes a SidecarDocument from the stream. Tolerant of:
    //   - unknown chunk tags (preserved verbatim in document.unknownChunks)
    //   - unknown field tags (preserved verbatim per-entry)
    //   - extra trailing bytes inside an entry (skipped)
    //   - version mismatches in either direction (logged; data still parsed)
    //
    // Rejects (returns ok=false) for:
    //   - bad magic
    //   - stream errors
    //   - length prefixes that exceed the hard caps in SidecarFormat.hpp
    ReadResult ReadSidecarDocument(cIGZIStream& in, SidecarDocument& out);
}
