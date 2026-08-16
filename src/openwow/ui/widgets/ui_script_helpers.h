
#pragma once

#include <cstdint>

namespace openwow::ui::widgets {

bool SetAlphaGradient(void* font_string, int alpha_start, int alpha_length);

const char* GetFontPath(const void* font_string);

void SetTextHeight(void* font_string, float height);

void SetShadowOffset(void* font_string, const float offset[2]);

}
