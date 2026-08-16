#pragma once

#include <cstdint>

namespace openwow::ui {

const char* FramePointToString(int framePoint);

int StringToFramePoint(const char* str, int* outValue);

int StringToScriptFrameStrata(const char* str, int* outValue);
const char* ScriptFrameStrataToString(int frameStrata);

const char* BlendModeToString(int blendMode);

int StringToBlendMode(const char* str, int* outValue);

int JustifyStringToFlags(const char* str, uint32_t* outFlags);

const char* JustifyFlagsToString(uint32_t flags);

int StringToHorizontalJustify(const char* str, uint32_t* outFlags);
int StringToVerticalJustify(const char* str, uint32_t* outFlags);
const char* HorizontalJustifyFlagsToString(uint32_t flags);
const char* VerticalJustifyFlagsToString(uint32_t flags);
int HorizontalJustifyFlagsToIndex(uint32_t flags);
int VerticalJustifyFlagsToIndex(uint32_t flags);

int SmoothingStringToFloats(const char* str, float* outA, float* outB);

const char* SmoothingFloatsToString(float a, float b);

int ParseLoopTypeString(const char* str, int* outValue);

const char* LoopTypeToString(int loopType);

const char* LoopStateToString(int loopState);

int ParseCurveTypeString(const char* str, int* outValue);

const char* CurveTypeToString(int curveType);

int OrientationStringToEnum(const char* str, int* outValue);

int StringToButtonTextureSlot(const char* str, int* outValue);

const char* ButtonTextureSlotToString(int slot);

int ParseBooleanString(const char* str, int defaultValue);

}
