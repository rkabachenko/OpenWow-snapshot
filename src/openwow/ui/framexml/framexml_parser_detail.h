#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::framexml::detail {

std::string NormalizeInherits(const std::string& raw);
std::string SanitizeWidgetIdentifier(std::string value);
bool IsTextureRegionTag(std::string_view tag);
bool IsRuntimeWidgetTag(std::string_view tag);
std::string NormalizeWidgetKind(std::string_view tag);
std::string CanonicalizeScriptEvent(std::string_view authored_name);

inline float BackdropCtorDefaultEdgeSizePixels() {
  return openwow::ui::framexml::BackdropCtorDefaultEdgeSizePixels();
}

using BackdropSpec = openwow::ui::framexml::BackdropSpec;

float EffectiveBackdropTileSize(const BackdropSpec& spec);

void InjectBackdropPieces(const BackdropSpec& spec,
                          UiFrame owner,
                          std::unordered_map<std::string, std::size_t>* index_by_name,
                          std::vector<UiFrame>* out_frames);

std::string ResolveParentToken(std::string value, const std::string& parent_name);

struct TextInsets {
  bool ok{false};
  int left{0};
  int right{0};
  int top{0};
  int bottom{0};
};

}
