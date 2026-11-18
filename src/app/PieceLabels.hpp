#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Short names for the tiles of the Networks tab. A NAM piece name such as "Tram-in-Road - Curve with one
// Road junction" is far longer than a tile, and most pieces of a menu button start with the same words, so
// the tile would show the same "Tram-in-Roa" for all of them. The label keeps what tells the piece apart
// from the other pieces of its button: "Curve with one Road Jct". The button's name is shown above the
// tiles, so the words the pieces share are not lost. The full name stays in the cache for search.
//
// The steps, for the pieces of one button:
// 1. Normalize each name: drop filler ("Puzzle Piece"), the size "(2x3)" that the tile badge shows,
//    brackets, and usage notes at the end ("Can be overridden to ..."). Write networks the NAM way, so
//    "One-Way Road", "OneWayRoad" and "Oneway road" are one word (OWR), and shorten common words
//    (Diagonal: Diag, degrees: °, Intersection: Int).
// 2. Drop the words that at least 30% of the names share, the most common first. A drop that would make two
//    different names the same, or leave a name without text, is skipped.
// 3. Keep relations whole: over, under, x, with, on, to, in are never dropped, nor the word after one, so
//    "Rail over Road" keeps "over Road". A label keeps at least one word of its own; when it would not,
//    only the shared start of the name goes.
// 4. Tidy: no separators inside, no "and" or "for" at either end, no relation word at the end.
// 5. Labels that are still the same get the piece's orientation (Ortho or Diag), then #2, #3.
namespace PieceLabels {
    enum class Orientation { Unknown, Orthogonal, Diagonal };

    struct Piece {
        std::string_view name;
        Orientation orientation = Orientation::Unknown;
    };

    // The labels of the pieces of one menu button, in the same order.
    [[nodiscard]] std::vector<std::string> Make(std::span<const Piece> pieces);

    // Step 1 alone: the name as the labels start from it.
    [[nodiscard]] std::string Normalize(std::string_view name);

    struct Button {
        std::string_view name;
        std::vector<std::string_view> pieceNames;
    };

    // The names of the menu buttons as the button list shows them, in the same order. Buttons that share a
    // name get the words most typical of their pieces against those of their namesakes: the six
    // "Underground Rail" buttons become "Underground Rail · Pedmall", "Underground Rail · DTR, Monorail" and
    // so on. Stray underscores and trailing dots go.
    [[nodiscard]] std::vector<std::string> ButtonLabels(std::span<const Button> buttons);
}
