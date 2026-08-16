#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::ui::xml {

struct XMLNode;

struct Color {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
  float a{1.0f};
};

struct ErrorContext {
  std::string last_error;

  OPENWOW_PRINTF_FORMAT(2, 3) void ReportError(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    last_error = buf;
    std::fprintf(stderr, "[FrameXML] %s\n", buf);
  }
};

int RelValue_ref(const XMLNode* node, float* out_val, ErrorContext* ctx);

int RelDimension_ref(const XMLNode* node, float* out_x, float* out_y,
                     ErrorContext* ctx);

int RelInset_ref(const XMLNode* node, float* left, float* right, float* top,
                 float* bottom, ErrorContext* ctx);

int MaxColor_ref(const XMLNode* node, uint32_t* orientation, Color* min_color,
                 Color* max_color, ErrorContext* ctx);

int XMLNode_ReadClampedColorToCImVector(const XMLNode* node,
                                        uint32_t* outColor);

int fn_point(const XMLNode* node, uint32_t* out_point, float* out_offset_xy,
             ErrorContext* ctx);

}
