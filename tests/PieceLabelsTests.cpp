// Checks for the tile labels of the Networks tab: run PieceLabelsTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "PieceLabels.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    using PieceLabels::Orientation;

    std::vector<std::string> Labels(const std::vector<std::string>& names,
                                    const std::vector<Orientation>& orientations = {}) {
        std::vector<PieceLabels::Piece> pieces;
        for (size_t i = 0; i < names.size(); ++i) {
            pieces.push_back({names[i], i < orientations.size() ? orientations[i] : Orientation::Unknown});
        }
        return PieceLabels::Make(pieces);
    }

    void Print(const std::vector<std::string>& labels) {
        for (const std::string& label : labels) {
            std::fprintf(stderr, "  [%s]\n", label.c_str());
        }
    }

#define CHECK_LABELS(actual, ...)                                                                                  \
    do {                                                                                                           \
        const auto got = (actual);                                                                                 \
        const std::vector<std::string> want = __VA_ARGS__;                                                         \
        if (got != want) {                                                                                         \
            Print(got);                                                                                            \
        }                                                                                                          \
        CHECK(got == want);                                                                                        \
    } while (false)
}

int main() {
    using PieceLabels::Normalize;

    // Filler, sizes, brackets and usage notes go; networks get their NAM names.
    CHECK(Normalize("Tram-in-Road - 90 deg curve") == "Tram-in-Road - 90\xC2\xB0 curve");
    CHECK(Normalize("One Way Road (Real Expressway) OWR-2 Flex Ramp (A1 Orthogonal) Overrides for OWR 1 come out "
                    "the applicable end.") == "OWR REW OWR-2 Flex Ramp A1 Ortho");
    CHECK(Normalize("Rural Road - FAR straight section (2x3)") == "Rural Road - FAR straight section");
    CHECK(Normalize("RHW-6S Cosmetic Piece - Block Markings (lane 1) -Exit Arrow") ==
          "RHW-6S Cosmetic - Block Markings lane 1 - Exit Arrow");
    CHECK(Normalize("Level 2 (15m) One-Way Road Viaduct Starter Piece") == "L2 15m OWR Viaduct Starter");
    CHECK(Normalize("Ground Light Rail Straight Puzzle Piece") == "GLR Straight");
    CHECK(Normalize("Orthagonal Onslope Transition") == "Ortho Onslope Trans");
    // A word written twice in a row, here by "Real Highway (RHW)", is written once.
    CHECK(Normalize("Real Highway (RHW) Flex Multi Radius Curve (R4) Can be overridden to other curves") ==
          "RHW Flex Multi Radius Curve R4");
    CHECK(Normalize("Tram-Avenue - Straight piece") == "Tram-in-Ave - Straight");
    // Hyphens inside a word stay.
    CHECK(Normalize("Tram-in-Road S-curve") == "Tram-in-Road S-curve");

    // The words most pieces of the button share go, also when a few pieces do not start with them.
    CHECK_LABELS(Labels({"Tram-in-Road - straight piece", "Tram-in-Road - 90 deg curve", "Urban Tram-in-Road S-curve",
                         "Tram-in-Road - Street intersection", "Tram-in-Road - Road junction",
                         "Tram-in-Road - Curve with one Road junction",
                         "Tram-in-Road - 4way with Tram-in-Road and Pedmall", "Tram-in-Road under elevated Highway"}),
                 {"Straight", "90\xC2\xB0 curve", "Urban S-curve", "Street Int", "Road Jct", "Curve with one Road Jct",
                  "4way with Tram-in-Road and Pedmall", "Under ElHwy"});

    // The word after a relation stays, and a label keeps a word of its own.
    CHECK_LABELS(Labels({"Rail under Road - Straight", "Straight Rail under Road Corner",
                         "Straight Rail under Road T-intersection A", "Straight Rail under Road T-intersection B",
                         "Rail under Road - Diagonal", "Curved Rail under Straight Road"}),
                 {"Under Road Straight", "Under Road Corner", "Under Road A", "Under Road B", "Under Road Diag",
                  "Curved under Straight"});

    // Shared words at the end go too.
    CHECK_LABELS(Labels({"Raiser Piece (15m), rotate for Digger Piece", "Raiser Piece (7.5m), rotate for Digger Piece",
                         "Raiser Piece (0.5m), rotate for Digger Piece"}),
                 {"15m", "7.5m", "0.5m"});

    // The same name: apart by orientation when that differs, else by count.
    CHECK_LABELS(Labels({"Road over Road Puzzle Piece", "Road over Road Puzzle Piece", "Road over Road Puzzle Piece",
                         "Road over Rail Puzzle Piece", "Road over Rail Puzzle Piece", "Road Corner"},
                        {Orientation::Orthogonal, Orientation::Diagonal, Orientation::Diagonal,
                         Orientation::Orthogonal, Orientation::Orthogonal, Orientation::Unknown}),
                 {"Over Road Ortho", "Over Road Diag", "Over Road Diag #2", "Over Rail", "Over Rail #2", "Corner"});

    // One piece keeps its name, without the filler.
    CHECK_LABELS(Labels({"Network Eraser (erases 1x1 section of draggable/FLEX network)"}),
                 {"Network Eraser erases 1x1 section of draggable/FLEX network"});
    CHECK(Labels({}).empty());

    // Button names: buttons that share a name get the words typical of their pieces.
    {
        const std::vector<PieceLabels::Button> buttons = {
            {"Underground Rail", {"Rail under Road - Straight", "Straight Rail under Road Corner",
                                  "Curved Rail under Straight Road"}},
            {"Avenue_Roundabouts", {"Roundabout Avenue 4x4"}},
            {"Underground Rail", {"Straight Rail under Ped Mall - Cobblestone", "Curved Rail under Ped Mall - Grass",
                                  "Diagonal Rail under Ped Mall - Paved"}},
            {"RealRailway FLEX Curve Pieces.", {"Macro Curve"}},
        };
        const auto labels = PieceLabels::ButtonLabels(buttons);
        CHECK_LABELS(labels, {"Underground Rail \xC2\xB7 Road", "Avenue Roundabouts",
                              "Underground Rail \xC2\xB7 Pedmall", "RRW FLEX Curve Pieces"});
    }
    // Each NAM project by its short name, and "Pieces" for any kind of pieces.
    {
        const std::vector<PieceLabels::Button> buttons = {
            {"RealHighway (RHW) Starter Pieces", {}},
            {"Network Widening Mod (NWM) Curve Pieces", {}},
            {"Flexible Underpasses (FLUPs) under Road", {}},
            {"Tram-FLUPs - Flexible UnderPasses", {}},
            {"Rail Puzzle Pieces", {}},
            {"ElevatedRail Dual/Double-Decker Network Pieces", {}},
            {"Wide Radius Curves (for Road, One-Way Road, Avenue, and Street) (Puzzle Pieces)", {}},
            {"Draggable Ground Light Rail (Tram)--Additional Styles", {}},
            {"High Speed Rail (HSR)", {}},
            {"Traffic Calming: Narrowing", {}},
            {"Fractionally Angled RealHighway Ramp Interfaces", {}},
            {"Fractionally-Angled Networks (for Road, One-Way Road, and Avenue)", {}},
            {"RealRailway FLEX Fractional Angled (FA) Pieces.", {}},
            {"Canal Addon Mod", {}},
            {"Tram-Avenue (GroundLightRail / Avenue) - Dual Network Pieces", {}},
        };
        CHECK_LABELS(PieceLabels::ButtonLabels(buttons),
                     {"RHW Starter Pieces", "NWM Curve Pieces", "FLUPs under Road", "Tram FLUPs", "Rail Pieces",
                      "El-Rail Dual/Double-Decker Pieces", "Wide Radius Curves (for Road, One-Way Road, Avenue, and Street)",
                      "Draggable GLR (Tram) - Additional Styles", "HSR", "Traffic Calming: Narrowing",
                      "FA RHW Ramp Interfaces", "FA Networks (for Road, One-Way Road, and Avenue)",
                      "RRW FLEX FA Pieces", "CAN-AM", "Tram-in-Avenue"});
    }

    std::puts("ok");
    return 0;
}
