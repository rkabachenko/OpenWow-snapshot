
#pragma once

#include <cstdint>

namespace openwow::game { class CLCDGfxBase; }

namespace openwow::game {

inline constexpr int kCLCDTextFieldInvert = 6;

inline constexpr int kCLCDTextFieldRenderWidth = 9;

inline constexpr int kCLCDTextFieldRenderHeight = 10;

inline constexpr int kCLCDTextFieldTextBuffer = 16;

inline constexpr int kCLCDTextFieldTextLength = 145;

inline constexpr int kCLCDTextFieldFormatFlags = 146;

inline constexpr int kCLCDTextFieldDirtyFlag = 147;

inline constexpr int kCLCDTextFieldDrawTextParams = 148;

int CLCDText_RenderText(std::uint32_t* self, CLCDGfxBase* gfx) noexcept;

}
