
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/math/float_compare.h"

#include <cctype>
#include <cstring>

namespace openwow::ui {

namespace {

bool StrEqNoCase(const char* a, const char* b) noexcept {
#ifdef _WIN32
  return _stricmp(a, b) == 0;
#else
  return strcasecmp(a, b) == 0;
#endif
}

struct IntStringEntry {
  int value;
  const char* name;
};

constexpr IntStringEntry kFramePoints[] = {
    {0, "TOPLEFT"},
    {1, "TOP"},
    {2, "TOPRIGHT"},
    {3, "LEFT"},
    {4, "CENTER"},
    {5, "RIGHT"},
    {6, "BOTTOMLEFT"},
    {7, "BOTTOM"},
    {8, "BOTTOMRIGHT"},
};
constexpr int kFramePointCount = 9;

constexpr IntStringEntry kScriptFrameStrata[] = {
    {1, "BACKGROUND"},
    {2, "LOW"},
    {3, "MEDIUM"},
    {4, "HIGH"},
    {5, "DIALOG"},
    {6, "FULLSCREEN"},
    {7, "FULLSCREEN_DIALOG"},
    {8, "TOOLTIP"},
};
constexpr int kScriptFrameStrataCount = 8;

constexpr IntStringEntry kBlendModes[] = {
    {0, "DISABLE"},
    {2, "BLEND"},
    {1, "ALPHAKEY"},
    {3, "ADD"},
    {4, "MOD"},
};
constexpr int kBlendModeCount = 5;

struct StringFlagEntry {
  const char* name;
  uint32_t flag;
};

constexpr StringFlagEntry kJustifyFlags[] = {
    {"LEFT",   0x1},
    {"CENTER", 0x2},
    {"RIGHT",  0x4},
    {"TOP",    0x8},
    {"MIDDLE", 0x10},
    {"BOTTOM", 0x20},
};
constexpr int kJustifyFlagCount = 6;
constexpr uint32_t kHorizontalJustifyMask = 0x7;
constexpr uint32_t kVerticalJustifyMask = 0x38;

struct SmoothingEntry {
  const char* name;
  float a;
  float b;
};

constexpr SmoothingEntry kSmoothingTable[] = {
    {"NONE",   0.0f, 0.0f},
    {"IN",     1.0f, 0.0f},
    {"OUT",    0.0f, 1.0f},
    {"IN_OUT", 1.0f, 1.0f},
    {"OUT_IN", 1.0f, 1.0f},
};
constexpr int kSmoothingCount = 5;

constexpr IntStringEntry kLoopTypes[] = {
    {0, "NONE"},
    {1, "REPEAT"},
    {2, "BOUNCE"},
};
constexpr int kLoopTypeCount = 3;

constexpr IntStringEntry kLoopStates[] = {
    {0, "NONE"},
    {1, "FORWARD"},
    {2, "REVERSE"},
};
constexpr int kLoopStateCount = 3;

constexpr IntStringEntry kCurveTypes[] = {
    {0, "NONE"},
    {1, "SMOOTH"},
};
constexpr int kCurveTypeCount = 2;

constexpr IntStringEntry kButtonTextureSlots[] = {
    {0, "DISABLED"},
    {1, "NORMAL"},
    {2, "PUSHED"},
};
constexpr int kButtonTextureSlotCount = 3;

}

const char* FramePointToString(int framePoint) {
  for (int i = 0; i < kFramePointCount; ++i) {
    if (kFramePoints[i].value == framePoint)
      return kFramePoints[i].name;
  }
  return "UNKNOWN";
}

int StringToFramePoint(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  for (int i = 0; i < kFramePointCount; ++i) {
    if (StrEqNoCase(str, kFramePoints[i].name)) {
      *outValue = kFramePoints[i].value;
      return 1;
    }
  }
  return 0;
}

int StringToScriptFrameStrata(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  for (int i = 0; i < kScriptFrameStrataCount; ++i) {
    if (StrEqNoCase(str, kScriptFrameStrata[i].name)) {
      *outValue = kScriptFrameStrata[i].value;
      return 1;
    }
  }
  return 0;
}

const char* ScriptFrameStrataToString(int frameStrata) {
  for (int i = 0; i < kScriptFrameStrataCount; ++i) {
    if (kScriptFrameStrata[i].value == frameStrata) {
      return kScriptFrameStrata[i].name;
    }
  }
  return "UNKNOWN";
}

const char* BlendModeToString(int blendMode) {
  for (int i = 0; i < kBlendModeCount; ++i) {
    if (kBlendModes[i].value == blendMode)
      return kBlendModes[i].name;
  }
  return "UNKNOWN";
}

int StringToBlendMode(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  for (int i = 0; i < kBlendModeCount; ++i) {
    if (StrEqNoCase(str, kBlendModes[i].name)) {
      *outValue = kBlendModes[i].value;
      return 1;
    }
  }
  return 0;
}

int JustifyStringToFlags(const char* str, uint32_t* outFlags) {
  if (!str || !outFlags) return 0;
  for (int i = 0; i < kJustifyFlagCount; ++i) {
    if (StrEqNoCase(str, kJustifyFlags[i].name)) {
      *outFlags = kJustifyFlags[i].flag;
      return 1;
    }
  }
  return 0;
}

const char* JustifyFlagsToString(uint32_t flags) {
  for (int i = 0; i < kJustifyFlagCount; ++i) {
    if (flags & kJustifyFlags[i].flag)
      return kJustifyFlags[i].name;
  }
  return "UNKNOWN";
}

int StringToHorizontalJustify(const char* str, uint32_t* outFlags) {
  uint32_t flags = 0;
  if (!JustifyStringToFlags(str, &flags)) {
    return 0;
  }
  flags &= kHorizontalJustifyMask;
  if (flags == 0) {
    return 0;
  }
  *outFlags = flags;
  return 1;
}

int StringToVerticalJustify(const char* str, uint32_t* outFlags) {
  uint32_t flags = 0;
  if (!JustifyStringToFlags(str, &flags)) {
    return 0;
  }
  flags &= kVerticalJustifyMask;
  if (flags == 0) {
    return 0;
  }
  *outFlags = flags;
  return 1;
}

const char* HorizontalJustifyFlagsToString(uint32_t flags) {
  return JustifyFlagsToString(flags & kHorizontalJustifyMask);
}

const char* VerticalJustifyFlagsToString(uint32_t flags) {
  return JustifyFlagsToString(flags & kVerticalJustifyMask);
}

int HorizontalJustifyFlagsToIndex(uint32_t flags) {
  const uint32_t masked = flags & kHorizontalJustifyMask;
  if (masked & 0x1u) return 0;
  if (masked & 0x2u) return 1;
  if (masked & 0x4u) return 2;
  return 0;
}

int VerticalJustifyFlagsToIndex(uint32_t flags) {
  const uint32_t masked = flags & kVerticalJustifyMask;
  if (masked & 0x8u) return 0;
  if (masked & 0x10u) return 1;
  if (masked & 0x20u) return 2;
  return 0;
}

int SmoothingStringToFloats(const char* str, float* outA, float* outB) {
  if (!str || !outA || !outB) return 0;
  for (int i = 0; i < kSmoothingCount; ++i) {
    if (StrEqNoCase(str, kSmoothingTable[i].name)) {
      *outA = kSmoothingTable[i].a;
      *outB = kSmoothingTable[i].b;
      return 1;
    }
  }
  return 0;
}

const char* SmoothingFloatsToString(float a, float b) {
  for (int i = 0; i < kSmoothingCount; ++i) {
    if (openwow::math::float_compare::WithinTolerance(
            a, kSmoothingTable[i].a, 0.001f) &&
        openwow::math::float_compare::WithinTolerance(
            b, kSmoothingTable[i].b, 0.001f))
      return kSmoothingTable[i].name;
  }
  return "UNKNOWN";
}

int ParseLoopTypeString(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  for (int i = 0; i < kLoopTypeCount; ++i) {
    if (StrEqNoCase(str, kLoopTypes[i].name)) {
      *outValue = kLoopTypes[i].value;
      return 1;
    }
  }
  return 0;
}

const char* LoopTypeToString(int loopType) {
  for (int i = 0; i < kLoopTypeCount; ++i) {
    if (kLoopTypes[i].value == loopType)
      return kLoopTypes[i].name;
  }
  return "UNKNOWN";
}

const char* LoopStateToString(int loopState) {
  for (int i = 0; i < kLoopStateCount; ++i) {
    if (kLoopStates[i].value == loopState)
      return kLoopStates[i].name;
  }
  return "UNKNOWN";
}

int ParseCurveTypeString(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  for (int i = 0; i < kCurveTypeCount; ++i) {
    if (StrEqNoCase(str, kCurveTypes[i].name)) {
      *outValue = kCurveTypes[i].value;
      return 1;
    }
  }
  return 0;
}

const char* CurveTypeToString(int curveType) {
  for (int i = 0; i < kCurveTypeCount; ++i) {
    if (kCurveTypes[i].value == curveType)
      return kCurveTypes[i].name;
  }
  return "UNKNOWN";
}

int OrientationStringToEnum(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  if (StrEqNoCase(str, "HORIZONTAL")) { *outValue = 0; return 1; }
  if (StrEqNoCase(str, "VERTICAL"))   { *outValue = 1; return 1; }
  return 0;
}

int ParseBooleanString(const char* str, int defaultValue) {
  if (!str) return defaultValue;

  char c = str[0];
  if (c == '\0') return defaultValue;

  if (c == '0' || c == 'F' || c == 'f' || c == 'N' || c == 'n')
    return 0;
  if ((c >= '1' && c <= '9') || c == 'T' || c == 't' || c == 'Y' || c == 'y')
    return 1;

  if (StrEqNoCase(str, "off"))      return 0;
  if (StrEqNoCase(str, "disabled")) return 0;
  if (StrEqNoCase(str, "on"))       return 1;
  if (StrEqNoCase(str, "enabled"))  return 1;

  return defaultValue;
}

int StringToButtonTextureSlot(const char* str, int* outValue) {
  if (!str || !outValue) return 0;
  for (int i = 0; i < kButtonTextureSlotCount; ++i) {
    if (StrEqNoCase(str, kButtonTextureSlots[i].name)) {
      *outValue = kButtonTextureSlots[i].value;
      return 1;
    }
  }
  return 0;
}

const char* ButtonTextureSlotToString(int slot) {
  for (int i = 0; i < kButtonTextureSlotCount; ++i) {
    if (kButtonTextureSlots[i].value == slot)
      return kButtonTextureSlots[i].name;
  }
  return "UNKNOWN";
}

}
