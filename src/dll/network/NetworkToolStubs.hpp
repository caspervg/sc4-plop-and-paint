#pragma once
#include <cstddef>
#include <cstdint>

class cISC4ViewInputControl;

// Parts of the game's own network tools that Plop and Paint uses, in Windows SimCity 4.exe 1.1.641. They are
// game objects, so read them only while the tool is the view's current input control: once it is gone, it
// may be freed.

// The tool that places a puzzle piece, which the network menus start for their piece buttons.
class cSC4ViewInputControlNetworkIntxTool
{
public:
    // The tool behind this input control, or null when it is a different control.
    static cSC4ViewInputControlNetworkIntxTool* From(const cISC4ViewInputControl* control) {
        auto* const tool = reinterpret_cast<cSC4ViewInputControlNetworkIntxTool*>(const_cast<cISC4ViewInputControl*>(control));
        return tool && tool->vtable == kVtable ? tool : nullptr;
    }

    static inline void* const kVtable = reinterpret_cast<void*>(0x00AAAF20);

    void* vtable;
    uint8_t RESERVED[0x4C - 0x4];
    uint32_t currentPiece;  // RUL0 ID of the piece being placed. TAB, Home and End change it.
};
static_assert(offsetof(cSC4ViewInputControlNetworkIntxTool, currentPiece) == 0x4C);
