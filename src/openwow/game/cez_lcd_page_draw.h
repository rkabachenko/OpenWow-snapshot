
#pragma once

#include <cstdint>

namespace openwow::game { class CLCDGfxBase; }

namespace openwow::game {

constexpr int kDrawFieldWidth           = 1;
constexpr int kDrawFieldHeight          = 2;
constexpr int kDrawFieldPosX            = 3;
constexpr int kDrawFieldPosY            = 4;
constexpr int kDrawFieldShowFlag        = 5;
constexpr int kDrawFieldScrollX         = 7;
constexpr int kDrawFieldScrollY         = 8;
constexpr int kDrawFieldChildListTail   = 18;
constexpr int kDrawFieldHasSubscreen    = 22;
constexpr int kDrawFieldEmbeddedVtable  = 23;
constexpr int kDrawFieldHasBgColor      = 45;
constexpr int kDrawFieldBgColor         = 46;

constexpr int kChildNodeNextOffset = 4;
constexpr int kChildNodeDataOffset = 8;

int CEzLcdPageNode_Draw(void* node, CLCDGfxBase* gfx) noexcept;

}
