
#include "openwow/input/input_manager.h"
#include "openwow/foundation/math/float_compare.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/core/storm_error.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/minimap_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/ui/cursor_frame_depth.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/script_object_lookup.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/ui_pixel_snap.h"
#include "openwow/ui/widgets/simple_button.h"
#include "openwow/ui/widgets/simple_check_button.h"
#include "openwow/ui/widgets/simple_edit_box.h"
#include "openwow/ui/widgets/simple_slider.h"
#include "openwow/ui/widgets/simple_scroll_frame.h"
#include "openwow/ui/widgets/simple_scrolling_message_frame.h"
#include "openwow/ui/widgets/simple_message_frame.h"
#include "openwow/ui/widgets/simple_html_frame.h"
#include "openwow/ui/widgets/simple_color_select.h"
#include "openwow/ui/widgets/simple_model.h"
#include "openwow/ui/widgets/simple_minimap.h"
#include "openwow/ui/widgets/simple_game_tooltip.h"
#include "openwow/ui/widgets/simple_cooldown.h"
#include "openwow/ui/widgets/simple_movie_frame.h"
#include "openwow/ui/widgets/simple_quest_poi_frame.h"
#include "openwow/ui/widgets/simple_world_frame.h"
#include "openwow/ui/widgets/simple_texture.h"
#include "openwow/ui/widgets/status_bar.h"
#include "openwow/ui/widgets/widget_xml_helpers.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_tree.h"
#include "openwow/ui/xml/xml_value_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>

namespace openwow::ui::widgets {

namespace {
struct StrataEntry {
  FrameStrata strata;
  const char *name;
};
constexpr std::array kStrataNamesArr = {
    StrataEntry{FrameStrata::World, "WORLD"},
    StrataEntry{FrameStrata::Background, "BACKGROUND"},
    StrataEntry{FrameStrata::Low, "LOW"},
    StrataEntry{FrameStrata::Medium, "MEDIUM"},
    StrataEntry{FrameStrata::High, "HIGH"},
    StrataEntry{FrameStrata::Dialog, "DIALOG"},
    StrataEntry{FrameStrata::Fullscreen, "FULLSCREEN"},
    StrataEntry{FrameStrata::FullscreenDialog, "FULLSCREEN_DIALOG"},
    StrataEntry{FrameStrata::Tooltip, "TOOLTIP"},
};

constexpr float kEpsilon = 0.00000023841858f;
constexpr float kMinEpsilon = 0.00000011920929f;
constexpr float kMinDepth = 0.2f;
constexpr float kMinimapStatusAnchorOffset = 0.001f;
constexpr uint32_t kLayoutResolvedBit = 0x100u;
constexpr uint32_t kLayoutQueuedBit = 0x400u;
constexpr uint32_t kLayoutResolveFailedBit = 0x800u;
constexpr uint32_t kLayoutClampedBit = 0x1000u;
constexpr uint32_t kRenderRetryBit = 0x20u;
constexpr uint8_t kLayoutRetryBudget = 6;

struct FrameBounds {
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
  float bottom{0.0f};
};

struct FrameStackingRegistry {
  std::vector<CSimpleFrame *> frames;
  CSimpleFrame *activeMoveFrame{nullptr};
  int activeMoveSizingMode{0};
  bool toplevelOverlapStateDirty{true};
};

FrameStackingRegistry &GetFrameStackingRegistry() {
  static FrameStackingRegistry registry;
  return registry;
}

std::string NormalizeFrameNameKey(const std::string &name) {
  std::string key;
  key.reserve(name.size());
  for (const unsigned char ch : name) {
    key.push_back(static_cast<char>(std::tolower(ch)));
  }
  return key;
}

std::unordered_map<std::string, CSimpleFrame *> &GetNamedFrameRegistry() {
  static std::unordered_map<std::string, CSimpleFrame *> registry;
  return registry;
}

template <typename ChildFn, typename RegionFn>
void ForEachChildFrameThenRegion(CSimpleFrame &frame, ChildFn &&childFn,
                                 RegionFn &&regionFn) {
  for (auto *child : frame.GetChildren()) {
    if (child != nullptr) {
      childFn(*child);
    }
  }

  for (auto *region : frame.GetRegions()) {
    if (region != nullptr) {
      regionFn(*region);
    }
  }
}

[[nodiscard]] ScriptObjectType FrameTypeFromXmlName(std::string_view frame_type) {
  const std::string requested_type(frame_type);
  for (int raw_type = static_cast<int>(ScriptObjectType::Object);
       raw_type < static_cast<int>(ScriptObjectType::COUNT_); ++raw_type) {
    const auto type = static_cast<ScriptObjectType>(raw_type);
    if (openwow::text::EqualsIgnoreCaseAscii(requested_type, ScriptObjectTypeName(type)) &&
        IsScriptTypeKindOf(type, ScriptObjectType::Frame)) {
      return type;
    }
  }
  return ScriptObjectType::COUNT_;
}

[[nodiscard]] std::unique_ptr<CSimpleFrame> CreateFrameWidgetByType(
    ScriptObjectType type) {
  switch (type) {
    case ScriptObjectType::Frame:
      return std::make_unique<CSimpleFrame>();
    case ScriptObjectType::Button:
      return std::make_unique<CSimpleButton>();
    case ScriptObjectType::CheckButton:
      return std::make_unique<CSimpleCheckButton>();
    case ScriptObjectType::ColorSelect:
      return std::make_unique<CSimpleColorSelect>();
    case ScriptObjectType::Cooldown:
      return std::make_unique<CSimpleCooldown>();
    case ScriptObjectType::DressUpModel:
      return std::make_unique<CSimpleDressUpModel>();
    case ScriptObjectType::EditBox:
      return std::make_unique<CSimpleEditBox>();
    case ScriptObjectType::GameTooltip:
      return std::make_unique<CSimpleGameTooltip>();
    case ScriptObjectType::MessageFrame:
      return std::make_unique<CSimpleMessageFrame>();
    case ScriptObjectType::Minimap:
      return std::make_unique<CSimpleMinimap>();
    case ScriptObjectType::Model:
      return std::make_unique<CSimpleModel>();
    case ScriptObjectType::MovieFrame:
      return std::make_unique<CSimpleMovieFrame>();
    case ScriptObjectType::PlayerModel:
      return std::make_unique<CSimplePlayerModel>();
    case ScriptObjectType::QuestPOIFrame:
      return std::make_unique<CSimpleQuestPOIFrame>();
    case ScriptObjectType::ScrollFrame:
      return std::make_unique<CSimpleScrollFrame>();
    case ScriptObjectType::ScrollingMessageFrame:
      return std::make_unique<CSimpleScrollingMessageFrame>();
    case ScriptObjectType::SimpleHTML:
      return std::make_unique<CSimpleHTMLFrame>();
    case ScriptObjectType::Slider:
      return std::make_unique<CSimpleSlider>();
    case ScriptObjectType::StatusBar:
      return std::make_unique<StatusBar>();
    case ScriptObjectType::TabardModel:
      return std::make_unique<CSimpleTabardModel>();
    case ScriptObjectType::WorldFrame:
      return std::make_unique<CSimpleWorldFrame>();
    default:
      return nullptr;
  }
}

void CreateXmlChildFrame(const openwow::ui::xml::XMLFrameDef &child_def,
                         CSimpleFrame *parent,
                         openwow::ui::xml::ErrorContext *error_handler) {
  if (parent == nullptr) {
    return;
  }

  auto child = CreateFrameWidget(child_def.type);
  if (!child) {
    if (error_handler != nullptr) {
      error_handler->ReportError("Unknown frame type: %s", child_def.type.c_str());
    }
    return;
  }

  child->LoadNameAndId(&child_def, error_handler);
  child->LoadXML(&child_def, error_handler);
  child->PostLoadProcess(&child_def, error_handler);
  parent->AdoptOwnedChild(std::move(child));
}

thread_local std::vector<std::string> g_inherited_frame_stack;

bool IsTemplateActive(const std::string &template_name) {
  const std::string normalized = NormalizeFrameNameKey(template_name);
  for (const auto &active_name : g_inherited_frame_stack) {
    if (NormalizeFrameNameKey(active_name) == normalized) {
      return true;
    }
  }
  return false;
}

class ScopedInheritedTemplate {
public:
  explicit ScopedInheritedTemplate(std::string template_name)
      : template_name_(std::move(template_name)) {
    g_inherited_frame_stack.push_back(template_name_);
  }

  ~ScopedInheritedTemplate() {
    if (!g_inherited_frame_stack.empty()) {
      g_inherited_frame_stack.pop_back();
    }
  }

private:
  std::string template_name_;
};

const char *FindNodeAttributeValueNoCase(const openwow::ui::xml::XMLNode &node,
                                         const char *name) {
  if (name == nullptr || *name == '\0') {
    return nullptr;
  }

  const std::string key(name);
  const auto it = node.attributes.find(key);
  if (it != node.attributes.end()) {
    return it->second.empty() ? nullptr : it->second.c_str();
  }

  const auto lower_key = openwow::text::ToLowerAscii(key);
  for (const auto &[candidate, value] : node.attributes) {
    if (openwow::text::ToLowerAscii(candidate) == lower_key) {
      return value.empty() ? nullptr : value.c_str();
    }
  }

  return nullptr;
}

const openwow::ui::xml::XMLNode *FindDirectChildByNameNoCase(
    const openwow::ui::xml::XMLNode &node, std::string_view tag_name) {
  for (const auto &child : node.children) {
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, std::string(tag_name))) {
      return &child;
    }
  }
  return nullptr;
}

const char *FrameXmlDisplayName(const CSimpleFrame &frame) {
  return frame.GetDisplayName();
}

void LoadFrameResizeBoundsFromXmlNode(CSimpleFrame &frame,
                                      const openwow::ui::xml::XMLNode &node,
                                      openwow::ui::xml::ErrorContext *error_handler) {
  float min_width = 0.0f;
  float min_height = 0.0f;
  float max_width = 0.0f;
  float max_height = 0.0f;

  if (const auto *min_resize = FindDirectChildByNameNoCase(node, "minResize");
      min_resize != nullptr) {
    (void)openwow::ui::xml::RelDimension_ref(min_resize, &min_width, &min_height, error_handler);
  }
  if (const auto *max_resize = FindDirectChildByNameNoCase(node, "maxResize");
      max_resize != nullptr) {
    (void)openwow::ui::xml::RelDimension_ref(max_resize, &max_width, &max_height, error_handler);
  }

  frame.SetMinResize(min_width, min_height);
  frame.SetMaxResize(max_width, max_height);
}

void LoadFrameBackdropFromXmlNode(CSimpleFrame &frame, const openwow::ui::xml::XMLNode &node,
                                  openwow::ui::xml::ErrorContext *error_handler) {
  const auto read_color_attr = [](const openwow::ui::xml::XMLNode &color_node, const char *name,
                                  const float default_value) {
    if (const char *value = FindNodeAttributeValueNoCase(color_node, name);
        value != nullptr && *value != '\0') {
      return static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(value));
    }
    return default_value;
  };

  BackdropInfo backdrop;
  backdrop.edgeSize = openwow::ui::framexml::detail::BackdropCtorDefaultEdgeSizePixels();

  if (const char *bg_file = FindNodeAttributeValueNoCase(node, "bgFile"); bg_file != nullptr) {
    backdrop.bgFile = bg_file;
  }
  if (const char *edge_file = FindNodeAttributeValueNoCase(node, "edgeFile"); edge_file != nullptr) {
    backdrop.edgeFile = edge_file;
  }
  if (const char *alpha_mode = FindNodeAttributeValueNoCase(node, "alphaMode");
      alpha_mode != nullptr) {
    backdrop.alphaMode = alpha_mode;
  }
  if (const char *tile = FindNodeAttributeValueNoCase(node, "tile");
      tile != nullptr && *tile != '\0') {
    backdrop.tile = ScriptParseBoolStringOrDefault(tile, false);
  }
  if (const char *tile_size = FindNodeAttributeValueNoCase(node, "tileSize");
      tile_size != nullptr && *tile_size != '\0') {
    backdrop.tileSize =
        static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(tile_size));
  }
  if (const char *edge_size = FindNodeAttributeValueNoCase(node, "edgeSize");
      edge_size != nullptr && *edge_size != '\0') {
    backdrop.edgeSize =
        static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(edge_size));
  }

  if (const auto *tile_size_node = FindDirectChildByNameNoCase(node, "TileSize");
      tile_size_node != nullptr) {
    float tile_size_value = backdrop.tileSize;
    if (openwow::ui::xml::RelValue_ref(tile_size_node, &tile_size_value, error_handler) != 0) {
      backdrop.tileSize = tile_size_value;
    }
  }
  if (const auto *edge_size_node = FindDirectChildByNameNoCase(node, "EdgeSize");
      edge_size_node != nullptr) {
    float edge_size_value = backdrop.edgeSize;
    if (openwow::ui::xml::RelValue_ref(edge_size_node, &edge_size_value, error_handler) != 0) {
      backdrop.edgeSize = edge_size_value;
    }
  }
  if (const auto *insets_node = FindDirectChildByNameNoCase(node, "BackgroundInsets");
      insets_node != nullptr) {
    (void)openwow::ui::xml::RelInset_ref(
        insets_node, &backdrop.insetLeft, &backdrop.insetRight, &backdrop.insetTop,
        &backdrop.insetBottom, error_handler);
  }

  frame.SetBackdrop(backdrop);
  frame.SetBackdropColor(1.0f, 1.0f, 1.0f, 1.0f);
  frame.SetBackdropBorderColor(1.0f, 1.0f, 1.0f, 1.0f);

  if (const auto *color_node = FindDirectChildByNameNoCase(node, "Color"); color_node != nullptr) {
    frame.SetBackdropColor(read_color_attr(*color_node, "r", 1.0f),
                           read_color_attr(*color_node, "g", 1.0f),
                           read_color_attr(*color_node, "b", 1.0f),
                           read_color_attr(*color_node, "a", 1.0f));
  }
  if (const auto *border_color_node = FindDirectChildByNameNoCase(node, "BorderColor");
      border_color_node != nullptr) {
    frame.SetBackdropBorderColor(read_color_attr(*border_color_node, "r", 1.0f),
                                 read_color_attr(*border_color_node, "g", 1.0f),
                                 read_color_attr(*border_color_node, "b", 1.0f),
                                 read_color_attr(*border_color_node, "a", 1.0f));
  }
}

std::unique_ptr<CScriptRegion> CreateFrameTitleRegionFromXmlNode(
    CSimpleFrame &frame, const openwow::ui::xml::XMLNode &node,
    openwow::ui::xml::ErrorContext *error_handler) {
  auto title_region = std::make_unique<CScriptRegion>(ScriptObjectType::Region);
  title_region->SetParent(&frame);

  openwow::ui::xml::XMLFrameDef region_def;
  region_def.type = node.tag;
  region_def.attributes = node.attributes;
  region_def.raw_node = node;
  LoadRegionLayoutFromXML(*title_region, region_def, error_handler);
  return title_region;
}

bool ExtractCachedLayoutBounds(const CSimpleFrame &frame, FrameBounds &out) {
  ScreenRect rect;
  if (!frame.TryGetCachedLayoutRect(&rect)) {
    return false;
  }

  out.left = rect.left;
  out.top = rect.top;
  out.right = rect.right;
  out.bottom = rect.bottom;
  return true;
}

bool ExpandBoundsWithRect(float *bounds, const ScreenRect &rect) noexcept {
  if (bounds == nullptr || !(rect.bottom > rect.top) || !(rect.right > rect.left)) {
    return false;
  }

  if (rect.top < bounds[0]) {
    bounds[0] = rect.top;
  }
  if (rect.left < bounds[1]) {
    bounds[1] = rect.left;
  }
  if (rect.bottom > bounds[2]) {
    bounds[2] = rect.bottom;
  }
  if (rect.right > bounds[3]) {
    bounds[3] = rect.right;
  }
  return true;
}

bool BoundsIntersect(const FrameBounds &lhs, const FrameBounds &rhs) {
  const float left = std::max(lhs.left, rhs.left);
  const float top = std::max(lhs.top, rhs.top);
  const float right = std::min(lhs.right, rhs.right);
  const float bottom = std::min(lhs.bottom, rhs.bottom);
  return right > left && bottom > top;
}

constexpr std::array<const char *, 31> kMouseButtonNames = {
    "LeftButton", "MiddleButton", "RightButton", "Button4",  "Button5",  "Button6",  "Button7",
    "Button8",    "Button9",      "Button10",    "Button11", "Button12", "Button13", "Button14",
    "Button15",   "Button16",     "Button17",    "Button18", "Button19", "Button20", "Button21",
    "Button22",   "Button23",     "Button24",    "Button25", "Button26", "Button27", "Button28",
    "Button29",   "Button30",     "Button31",
};

bool EqualsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const unsigned char lhs_ch = static_cast<unsigned char>(lhs[i]);
    const unsigned char rhs_ch = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(lhs_ch) != std::tolower(rhs_ch)) {
      return false;
    }
  }
  return true;
}

constexpr std::uint32_t DrawLayerDirtyBit(DrawLayer layer) noexcept {
  return 1u << static_cast<std::uint32_t>(layer);
}

struct ProtectedFrameTraversalState {
  std::unordered_map<const CSimpleFrame *, bool> frame_cache;
  std::unordered_set<const CSimpleFrame *> active_frames;
  std::unordered_set<const CScriptRegion *> active_regions;
};

bool IsProtectedLayoutRegionRecursive(const CScriptRegion *region,
                                      ProtectedFrameTraversalState *state);

bool CheckProtectedChildrenRecursive(const CSimpleFrame *frame,
                                     ProtectedFrameTraversalState *state) {
  if (frame == nullptr || state == nullptr) {
    return false;
  }

  for (const auto *child : frame->GetChildren()) {
    if (child == nullptr) {
      continue;
    }
    if (IsProtectedLayoutRegionRecursive(child, state)) {
      return true;
    }
  }

  return false;
}

bool IsProtectedLayoutRegionRecursive(const CScriptRegion *region,
                                      ProtectedFrameTraversalState *state) {
  if (region == nullptr || state == nullptr) {
    return false;
  }
  if (!state->active_regions.insert(region).second) {
    return false;
  }

  bool is_protected = false;
  const auto *frame = dynamic_cast<const CSimpleFrame *>(region);
  if (frame != nullptr) {
    if (const auto cached = state->frame_cache.find(frame); cached != state->frame_cache.end()) {
      is_protected = cached->second;
    } else if (state->active_frames.insert(frame).second) {
      is_protected = frame->IsExplicitlyProtected() ||
                     CheckProtectedChildrenRecursive(frame, state);
      if (!is_protected) {
        if (const auto *parent = frame->GetParent(); parent != nullptr) {
          is_protected = IsProtectedLayoutRegionRecursive(parent, state);
        }
      }
      if (!is_protected) {
        for (int point_index = 0; point_index < static_cast<int>(FramePoint::COUNT_);
             ++point_index) {
          const auto *anchor = frame->GetPoint(static_cast<FramePoint>(point_index));
          if (anchor == nullptr || anchor->relativeTo == nullptr ||
              anchor->relativeTo == frame) {
            continue;
          }
          if (IsProtectedLayoutRegionRecursive(anchor->relativeTo, state)) {
            is_protected = true;
            break;
          }
        }
      }
      state->active_frames.erase(frame);
      state->frame_cache.emplace(frame, is_protected);
    }
  }

  if (!is_protected && frame == nullptr) {
    if (const auto *parent = region->GetParent(); parent != nullptr) {
      is_protected = IsProtectedLayoutRegionRecursive(parent, state);
    }
  }
  if (!is_protected && frame == nullptr) {
    for (int point_index = 0; point_index < static_cast<int>(FramePoint::COUNT_);
         ++point_index) {
      const auto *anchor = region->GetPoint(static_cast<FramePoint>(point_index));
      if (anchor == nullptr || anchor->relativeTo == nullptr ||
          anchor->relativeTo == region) {
        continue;
      }
      if (IsProtectedLayoutRegionRecursive(anchor->relativeTo, state)) {
        is_protected = true;
        break;
      }
    }
  }

  state->active_regions.erase(region);
  return is_protected;
}
}

std::unique_ptr<CSimpleFrame> CreateFrameWidget(
    const std::string_view frame_type) {
  if (openwow::text::EqualsIgnoreCaseAscii(frame_type, "ModelFFX")) {
    return std::make_unique<CSimpleModel>();
  }
  return CreateFrameWidgetByType(FrameTypeFromXmlName(frame_type));
}

const char *FrameStrataName(FrameStrata strata) noexcept {
  for (const auto &e : kStrataNamesArr)
    if (e.strata == strata)
      return e.name;
  return "MEDIUM";
}

void CSimpleFrame::SetFrameStrata(const FrameStrata strata) noexcept {
  if (strata_ == strata) {
    return;
  }

  strata_ = strata;
  MarkToplevelOverlapStateDirty();
  for (auto *child : children_) {
    if (!child) {
      continue;
    }
    child->SetFrameStrata(strata);
  }
}

void CSimpleFrame::SetFrameLevel(const int32_t level) noexcept {
  const int32_t requested = std::max(level, 0);
  if (requested == level_) {
    return;
  }

  int32_t delta = requested - level_;
  if (delta > 128) {
    delta = 128;
  }

  level_ += delta;
  MarkToplevelOverlapStateDirty();
  const FrameStrata parent_strata = strata_;
  for (auto *child : children_) {
    if (!child || child->GetFrameStrata() != parent_strata) {
      continue;
    }
    child->SetFrameLevel(child->GetFrameLevel() + delta);
  }
}

void CSimpleFrame::SetToplevel(const bool t) noexcept {
  if (toplevel_ == t) {
    return;
  }

  toplevel_ = t;
  hasIntersectingVisiblePeer_ = false;
  MarkToplevelOverlapStateDirty();
}

void CSimpleFrame::SetRect(const ScreenRect &rect) noexcept {
  CScriptRegion::SetRect(rect);
  liveRectInitialized_ = true;
  UpdateCachedLayoutRect(rect);
  MarkToplevelOverlapStateDirty();
}

FrameStrata FrameStrataFromName(const std::string &name) noexcept {
  for (const auto &e : kStrataNamesArr)
    if (name == e.name)
      return e.strata;
  return FrameStrata::Medium;
}

void CSimpleFrame::SetFrameName(const std::string &name) {
  if (!GetName().empty()) {
    auto &registry = GetNamedFrameRegistry();
    const auto existing = registry.find(NormalizeFrameNameKey(GetName()));
    if (existing != registry.end() && existing->second == this) {
      registry.erase(existing);
    }
  }

  SetName(name);

  if (!name.empty()) {
    GetNamedFrameRegistry()[NormalizeFrameNameKey(name)] = this;
  }
}

CSimpleFrame *CSimpleFrame::FindNamedFrame(const std::string &name) {
  if (name.empty()) {
    return nullptr;
  }

  auto &registry = GetNamedFrameRegistry();
  const auto it = registry.find(NormalizeFrameNameKey(name));
  return it != registry.end() ? it->second : nullptr;
}

void CSimpleFrame::AdoptOwnedChild(std::unique_ptr<CSimpleFrame> child) {
  if (!child) {
    return;
  }

  CSimpleFrame *raw_child = child.get();
  if (std::find(children_.begin(), children_.end(), raw_child) == children_.end()) {
    AddChild(raw_child);
  }
  ownedChildren_.push_back(std::move(child));
}

void CSimpleFrame::AddRegion(CScriptRegion *region) {
  if (!region) {
    return;
  }

  region->SetParent(this);
}

void CSimpleFrame::LinkRegionFromParentChange(CScriptRegion *region) noexcept {
  if (!region) {
    return;
  }

  regions_.erase(std::remove(regions_.begin(), regions_.end(), region), regions_.end());
  regions_.insert(regions_.begin(), region);
}

void CSimpleFrame::UnlinkRegionFromParentChange(CScriptRegion *region) noexcept {
  if (!region) {
    return;
  }

  regions_.erase(std::remove(regions_.begin(), regions_.end(), region), regions_.end());
}

void CSimpleFrame::SetDrawLayerStateInternal(DrawLayer layer, bool enabled,
                                             bool queueUpdate) noexcept {
  const auto index = static_cast<size_t>(layer);
  if (index >= drawLayerEnabled_.size()) {
    return;
  }

  const bool changed = drawLayerEnabled_[index] != enabled;
  drawLayerEnabled_[index] = enabled;
  if (layer == DrawLayer::Highlight) {
    highlightActive_ = enabled;
  }

  if (changed) {
    RefreshLayerVisibility(layer);
  }

  if (queueUpdate) {
    QueueDrawLayerStateUpdate(layer);
  }
}

void CSimpleFrame::QueueDrawLayerStateUpdate(DrawLayer layer) noexcept {
  const auto index = static_cast<size_t>(layer);
  if (index >= drawLayerEnabled_.size()) {
    return;
  }

  pendingAnimSlots_ |= DrawLayerDirtyBit(layer);
  NotifyOwningScrollFrameOfContentChange();
}

void CSimpleFrame::QueueRenderRetryStateUpdate() noexcept {
  pendingAnimSlots_ |= kRenderRetryBit;
}

void CSimpleFrame::SetHoveredChildFrame(CSimpleFrame *child) noexcept {
  if (child && child->GetParent() != this) {
    return;
  }

  hoveredChildFrame_ = child;
}

void CSimpleFrame::ClearHoveredChildFrame(CSimpleFrame *child) noexcept {
  if (hoveredChildFrame_ == child) {
    hoveredChildFrame_ = nullptr;
  }
}

bool CSimpleFrame::IsTypeOf(const char *typeName) const noexcept {
  if (StrCaseEq(typeName, "Frame"))
    return true;
  return CScriptRegion::IsTypeOf(typeName);
}

bool CSimpleFrame::IsProtected() const noexcept {
  ProtectedFrameTraversalState state;
  return IsProtectedLayoutRegionRecursive(this, &state);
}

bool CSimpleFrame::HasAllowedAttributeChanges() const noexcept {

  if (allowAttributeChanges_) {
    return true;
  }
  for (const auto *ancestor = dynamic_cast<const CSimpleFrame *>(GetParent());
       ancestor != nullptr;
       ancestor = dynamic_cast<const CSimpleFrame *>(ancestor->GetParent())) {
    if (ancestor->allowAttributeChanges_) {
      return true;
    }
  }
  return false;
}

const char *MouseButtonName(uint32_t buttonFlag) noexcept {
  if (buttonFlag == 0 || (buttonFlag & (buttonFlag - 1)) != 0) {
    return "UNKNOWN";
  }

  uint32_t bit_index = 0;
  while ((buttonFlag >> bit_index) > 1u) {
    ++bit_index;
  }
  if (bit_index >= kMouseButtonNames.size()) {
    return "UNKNOWN";
  }
  return kMouseButtonNames[bit_index];
}

uint32_t MouseButtonScriptOrdinalToFlag(const int buttonOrdinal) noexcept {
  switch (buttonOrdinal) {
    case 1:
      return 1u;
    case 2:
      return 4u;
    case 3:
      return 2u;
    default:
      break;
  }

  if (buttonOrdinal < 4 || buttonOrdinal > 31) {
    return 0;
  }

  return 1u << (buttonOrdinal - 1);
}

int MouseButtonFlagToScriptOrdinal(const uint32_t buttonFlag) noexcept {
  if (buttonFlag == 0 || (buttonFlag & (buttonFlag - 1)) != 0) {
    return 0;
  }

  switch (buttonFlag) {
    case 1u:
      return 1;
    case 4u:
      return 2;
    case 2u:
      return 3;
    default:
      break;
  }

  int button_ordinal = 4;
  for (uint32_t bit = 8u; bit != 0 && bit < (1u << 31); bit <<= 1, ++button_ordinal) {
    if (buttonFlag == bit) {
      return button_ordinal;
    }
  }

  return 0;
}

uint32_t FirstMouseButtonFlag(uint32_t buttonFlags) noexcept {
  for (uint32_t bit = 1; bit != 0 && bit < (1u << 31); bit <<= 1) {
    if ((buttonFlags & bit) != 0) {
      return bit;
    }
  }
  return 0;
}

uint32_t MouseButtonFlag(const char *name) noexcept {
  if (!name || !*name)
    return 0;

  const std::string_view button_name{name};
  if (EqualsIgnoreAsciiCase(button_name, "LeftButton"))
    return 1;
  if (EqualsIgnoreAsciiCase(button_name, "MiddleButton"))
    return 2;
  if (EqualsIgnoreAsciiCase(button_name, "RightButton"))
    return 4;
  if (button_name.size() < 7 || !EqualsIgnoreAsciiCase(button_name.substr(0, 6), "Button")) {
    return 0;
  }

  unsigned button_index = 0;
  const auto *first = button_name.data() + 6;
  const auto *last = button_name.data() + button_name.size();
  const auto parse_result = std::from_chars(first, last, button_index);
  if (parse_result.ec != std::errc{} || parse_result.ptr != last || button_index < 4 ||
      button_index > 31) {
    return 0;
  }
  return 1u << (button_index - 1);
}

void CSimpleFrame::UpdateCachedLayoutRect(const ScreenRect &rect) noexcept {
  cachedLayoutRect_[0] = rect.left;
  cachedLayoutRect_[1] = rect.top;
  cachedLayoutRect_[2] = rect.right;
  cachedLayoutRect_[3] = rect.bottom;
  layoutFlags_ &= ~kLayoutResolveFailedBit;
  layoutFlags_ |= kLayoutResolvedBit;
}

void CSimpleFrame::SetClampedToScreen(bool clamped) noexcept {
  if (clamped) {
    layoutFlags_ |= kLayoutClampedBit;
  } else {
    layoutFlags_ &= ~kLayoutClampedBit;
  }

  QueueLayoutInvalidation();
}

bool CSimpleFrame::IsClampedToScreen() const noexcept {
  return (layoutFlags_ & kLayoutClampedBit) != 0;
}

void CSimpleFrame::HandleLayoutInvalidation(bool attemptResolve) noexcept {
  QueueLayoutInvalidation(attemptResolve);
}

void CSimpleFrame::OnRelativeAnchorTargetDestroyed() noexcept {
  layoutFlags_ &= ~kLayoutResolveFailedBit;
}

bool CSimpleFrame::TryResolveCachedLayoutRectFromCurrentRect() noexcept {
  if ((layoutFlags_ & kLayoutResolveFailedBit) != 0u) {
    return false;
  }

  if (!liveRectInitialized_ || !std::isfinite(rect_.left) || !std::isfinite(rect_.top) ||
      !std::isfinite(rect_.right) || !std::isfinite(rect_.bottom)) {
    layoutFlags_ &= ~kLayoutResolvedBit;
    layoutFlags_ |= kLayoutResolveFailedBit;
    return false;
  }

  UpdateCachedLayoutRect(rect_);
  return true;
}

bool CSimpleFrame::SetScale(float scale, bool force) {
  if ((!force && std::fabs(scale - scale_) < kEpsilon) || scale == 0.0f) {
    return false;
  }

  scale_ = scale;
  return RefreshScaleCascade(force);
}

bool CSimpleFrame::SetDepth(const float depth, const bool force) {
  if (!force &&
      openwow::math::float_compare::WithinClientEpsilon(depth, depth_)) {
    return false;
  }

  depth_ = depth;
  return RefreshDepthCascade(force);
}

void CSimpleFrame::QueueLayoutInvalidation(bool attemptResolve) noexcept {
  if (attemptResolve && TryResolveCachedLayoutRectFromCurrentRect()) {
    layoutQueued_ = false;
    layoutRetryCount_ = 0;
    return;
  }

  layoutFlags_ |= kLayoutQueuedBit;
  layoutQueued_ = true;
  layoutRetryCount_ = kLayoutRetryBudget;
  layoutDirty_ = true;
}

bool CSimpleFrame::SetLayoutScale(float scale, bool force) {
  if (scale == 0.0f) {
    openwow::core::SErrSetLastError(87);
    return false;
  }
  if (!force && std::fabs(scale - layoutScale_) < kEpsilon) {
    return false;
  }
  if (scale <= kMinEpsilon) {
    return false;
  }
  layoutScale_ = scale;
  cachedLayoutRect_[0] = 0.0f;
  cachedLayoutRect_[1] = 0.0f;
  cachedLayoutRect_[2] = 0.0f;
  cachedLayoutRect_[3] = 0.0f;
  layoutFlags_ &= ~kLayoutResolvedBit;
  InvalidateLayout(false);
  return true;
}

bool CSimpleFrame::SetLayoutDepth(float depth, bool force) {
  if (!force && std::fabs(depth - layoutDepth_) < kEpsilon) {
    return false;
  }
  if (depth < kMinDepth) {
    depth = kMinDepth;
  }
  layoutFlags_ &= ~kLayoutResolvedBit;
  layoutDepth_ = depth;
  InvalidateLayout(false);
  return true;
}

void CSimpleFrame::SetLayoutWidth(float w) {
  layoutFlags_ &= ~kLayoutResolveFailedBit;
  layoutWidth_ = w;
  InvalidateLayout(false);
}

void CSimpleFrame::SetLayoutHeight(float h) {
  layoutFlags_ &= ~kLayoutResolveFailedBit;
  layoutHeight_ = h;
  InvalidateLayout(false);
}

void CSimpleFrame::SetLayoutWidthAndHeight(float w, float h) {
  layoutFlags_ &= ~kLayoutResolveFailedBit;
  layoutWidth_ = w;
  layoutHeight_ = h;
  InvalidateLayout(false);
}

bool CSimpleFrame::TryGetCachedLayoutRect(ScreenRect *outRect) const noexcept {
  if (!outRect || (layoutFlags_ & kLayoutResolvedBit) == 0u) {
    return false;
  }

  outRect->left = cachedLayoutRect_[0];
  outRect->top = cachedLayoutRect_[1];
  outRect->right = cachedLayoutRect_[2];
  outRect->bottom = cachedLayoutRect_[3];
  return true;
}

void CSimpleFrame::GetDimensionsWithFallback(float *outWidth, float *outHeight,
                                             bool allowFallback) {
  *outWidth = GetLayoutWidth();
  *outHeight = GetLayoutHeight();

  if (allowFallback)
    return;
  if (*outHeight != 0.0f && *outWidth != 0.0f)
    return;

  if ((layoutFlags_ & kLayoutQueuedBit) != 0u) {
    QueueLayoutInvalidation(true);
  }

  if ((layoutFlags_ & kLayoutResolvedBit) != 0) {

    if (layoutScale_ > kMinEpsilon) {
      *outWidth = (cachedLayoutRect_[2] - cachedLayoutRect_[0]) / layoutScale_;
      *outHeight = (cachedLayoutRect_[3] - cachedLayoutRect_[1]) / layoutScale_;
    }
  }
}

void CSimpleFrame::InvalidateRelativeDependentLayouts() {
  InvalidateRelativeDependents();
}

void CSimpleFrame::LoadAnchorPointsFromXML(const void *xmlNode, void *errorHandler) {
  const auto *frame_def = static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  auto *error_handler = static_cast<openwow::ui::xml::ErrorContext *>(errorHandler);
  if (frame_def == nullptr) {
    return;
  }

  LoadRegionLayoutFromXML(*this, *frame_def, error_handler);
}

void CSimpleFrame::ProcessLayerElements(const void *xmlNode, void *errorHandler) {
  const auto *frame_def = static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  auto *error_handler = static_cast<openwow::ui::xml::ErrorContext *>(errorHandler);
  if (frame_def == nullptr) {
    return;
  }

  const char *frame_name = GetDisplayName();
  if (const auto *layers_node = frame_def->raw_node.FindChild("Layers");
      layers_node != nullptr && error_handler != nullptr) {
    for (const auto &child : layers_node->children) {
      if (!openwow::text::EqualsIgnoreCaseAscii(child.tag, "Layer")) {
        error_handler->ReportError(
            "Frame %s: Unknown child node in %s element: %s",
            frame_name, "Layers", child.tag.c_str());
      }
    }
  }

  for (const auto &layer_def : frame_def->layers) {
    const DrawLayer draw_layer =
        layer_def.level.empty() ? DrawLayer::Artwork
                                : DrawLayerFromName(layer_def.level);

    for (const auto &item_def : layer_def.items) {
      if (openwow::text::EqualsIgnoreCaseAscii(item_def.type, "Texture")) {
        auto texture = std::make_unique<CSimpleTexture>();
        texture->SetDrawLayer(draw_layer);
        texture->SetParent(this);
        if (!item_def.name.empty()) {
          texture->SetName(item_def.name);
        }
        texture->LoadXML(item_def, error_handler);
        ownedRegions_.emplace_back(std::move(texture));
        continue;
      }

      if (openwow::text::EqualsIgnoreCaseAscii(item_def.type, "FontString")) {

        auto font_string = std::make_unique<CSimpleFontString>();
        font_string->SetDrawLayer(draw_layer);
        font_string->SetParent(this);
        if (!item_def.name.empty()) {
          font_string->SetName(item_def.name);
        }
        font_string->LoadXML(item_def, error_handler, nullptr);
        ownedRegions_.emplace_back(std::move(font_string));
        continue;
      }

      if (error_handler != nullptr) {
        error_handler->ReportError(
            "Frame %s: Unknown child node in %s element: %s",
            frame_name, "Layer", item_def.type.c_str());
      }
    }
  }
}

void CSimpleFrame::ProcessInheritsAndFrames(const void *xmlNode, void *errorHandler) {
  const auto *frame_def = static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  if (!frame_def) {
    return;
  }

  for (const auto &inherited_name : openwow::ui::framexml::SplitTemplateList(
           frame_def->inherits, openwow::ui::framexml::TemplateListSyntax::kCommaSeparated)) {
    const auto *inherited_def = openwow::ui::xml::FrameXMLParser::GetTemplate(inherited_name);
    if (!inherited_def || IsTemplateActive(inherited_name)) {
      continue;
    }

    ScopedInheritedTemplate inherited_scope(inherited_name);
    ProcessInheritsAndFrames(inherited_def, errorHandler);
  }

  for (const auto &child_def : frame_def->children) {
    CreateXmlChildFrame(child_def, this,
                        static_cast<openwow::ui::xml::ErrorContext *>(errorHandler));
  }
}

void CSimpleFrame::LoadScriptElements(const void * , void * ) {

}

void CSimpleFrame::LinkRegion(CScriptRegion *region) {
  if (!region) {
    openwow::core::SErrSetLastError(87);
    return;
  }

  AddRegion(region);
}

void CSimpleFrame::UnlinkRegion(CScriptRegion *region) {
  if (!region) {
    openwow::core::SErrSetLastError(87);
    return;
  }

  if (region->GetParent() == this) {
    region->SetParent(nullptr);
    return;
  }

  UnlinkRegionFromParentChange(region);
}

void CSimpleFrame::DetachDestroyedRegion(CScriptRegion *region) noexcept {
  UnlinkRegionFromParentChange(region);
}

bool CSimpleFrame::FireOnKeyDown(const std::string &key) {
  if (!IsVisible() || !keyboardEnabled_)
    return false;
  const bool has_key_down = HasScript("OnKeyDown");
  const bool has_key_up = HasScript("OnKeyUp");
  if (!has_key_down && !has_key_up)
    return false;
  lastKeyDown_ = key;
  if (has_key_down) {
    RunScript("OnKeyDown");
  }
  return true;
}

bool CSimpleFrame::FireOnKeyUp(const std::string &key) {
  if (!IsVisible() || !keyboardEnabled_)
    return false;
  if (!HasScript("OnKeyUp"))
    return false;
  lastKeyUp_ = key;
  RunScript("OnKeyUp");
  return true;
}

bool CSimpleFrame::FireOnChar(const std::string &ch) {
  if (!IsVisible() || !keyboardEnabled_)
    return false;
  if (!HasScript("OnChar"))
    return false;
  lastChar_ = ch;
  RunScript("OnChar");
  return true;
}

bool CSimpleFrame::FireOnMouseDown(uint32_t buttonFlag, float x, float y,
                                   const char* buttonName) {
  const char *resolvedName = buttonName;
  if (!resolvedName) {
    if ((registeredClicksMask_ & buttonFlag) != 0) {
      dragState_.dragging = true;
      dragState_.dragMoved = false;
      dragState_.buttonFlags = buttonFlag;
      dragState_.startX = x;
      dragState_.startY = y;
    }
    resolvedName = MouseButtonName(buttonFlag);
  }

  lastMouseButton_ = resolvedName;
  if (HasScript("OnMouseDown")) {
    RunScript("OnMouseDown");
  }
  return false;

}

bool CSimpleFrame::FireOnMouseUp(uint32_t buttonFlag, float , float ,
                                 const char* buttonName) {
  const char *resolvedName = buttonName;
  if (!resolvedName) {
    if ((registeredClicksMask_ & buttonFlag) != 0) {
      dragState_.dragging = false;
      if (dragState_.dragMoved) {
        FireOnDragStop();
        if (auto *parentFrame = dynamic_cast<CSimpleFrame *>(parent_)) {
          parentFrame->FireOnReceiveDrag();
        }
        dragState_.dragMoved = false;
        return true;
      }
    }
    resolvedName = MouseButtonName(buttonFlag);
  }

  lastMouseButton_ = resolvedName;
  if (HasScript("OnMouseUp")) {
    RunScript("OnMouseUp");
  }
  return false;
}

bool CSimpleFrame::FireOnMouseWheel(int32_t delta) {
  if (!IsVisible())
    return false;
  if (!HasScript("OnMouseWheel"))
    return false;
  lastWheelDelta_ = (delta >= 0) ? 1 : -1;
  RunScript("OnMouseWheel");
  return true;
}

void CSimpleFrame::FireOnDragStart(uint32_t buttonFlags) {
  if (HasScript("OnDragStart")) {
    lastMouseButton_ = MouseButtonName(FirstMouseButtonFlag(buttonFlags));
    RunScript("OnDragStart");
  }
}

void CSimpleFrame::FireOnDragStop() {
  if (HasScript("OnDragStop")) {
    RunScript("OnDragStop");
  }
}

void CSimpleFrame::FireOnReceiveDrag() {
  if (HasScript("OnReceiveDrag")) {
    RunScript("OnReceiveDrag");
  }
}

void CSimpleFrame::FireOnEnter(bool motion) {
  if (auto *parentFrame = dynamic_cast<CSimpleFrame *>(parent_)) {
    parentFrame->SetHoveredChildFrame(this);
  }

  if (focusFrame_ == nullptr) {
    SetDrawLayerStateInternal(DrawLayer::Highlight, true, true);
  }

  if (HasScript("OnEnter")) {
    lastMotion_ = motion;
    RunScript("OnEnter");
  }
}

void CSimpleFrame::FireOnLeave(bool motion, bool clearDragState) {
  if (auto *parentFrame = dynamic_cast<CSimpleFrame *>(parent_)) {
    parentFrame->ClearHoveredChildFrame(this);
  }

  if (focusFrame_ == nullptr) {
    SetDrawLayerStateInternal(DrawLayer::Highlight, false, true);
  }

  if (clearDragState) {
    dragState_.dragging = false;
    dragState_.dragMoved = false;
  }

  if (HasScript("OnLeave")) {
    lastMotion_ = motion;
    RunScript("OnLeave");
  }
}

void CSimpleMinimap::FireOnLeave(bool motion, bool clearDragState) {
  auto &tooltipSys = openwow::ui::game::TooltipSystem::Get();
  tooltipSys.Hide();

  tooltipSys.HideLiveGameTooltipFrame();
  tooltipPinHitMask_ = 0;
  tooltipCategoryHitMasks_.fill(0);
  tooltipVisible_ = false;
  CSimpleFrame::FireOnLeave(motion, clearDragState);
}

bool CSimpleMinimap::OnMouseMove(const void *inputEvent) {
  const bool hit = CSimpleFrame::OnMouseMove(inputEvent);

  auto &tooltipSys = openwow::ui::game::TooltipSystem::Get();
  auto &minimapSys = *minimap_state_;

  if (!hit) {

    const bool wasVisible = tooltipVisible_;
    const bool hadPinHits = (tooltipPinHitMask_ != 0);
    bool hadCategoryHits = false;
    for (const auto mask : tooltipCategoryHitMasks_) {
      if (mask != 0) {
        hadCategoryHits = true;
        break;
      }
    }

    tooltipPinHitMask_ = 0;
    tooltipCategoryHitMasks_.fill(0);
    tooltipVisible_ = false;

    if (wasVisible || hadPinHits || hadCategoryHits) {
      tooltipSys.Hide();
      tooltipSys.HideLiveGameTooltipFrame();
    }
    return false;
  }

  if (!inputEvent) {
    return true;
  }

  const auto &event =
      *static_cast<const openwow::input::InputEvent *>(inputEvent);
  const float cursorX = static_cast<float>(event.mouseX);
  const float cursorY = static_cast<float>(event.mouseY);

  const auto &rect = GetRect();
  const float frameW = rect.Width();
  const float frameH = rect.Height();

  if (frameW <= 0.0f || frameH <= 0.0f) {
    return true;
  }

  const float blipHalfExtent = frameH * 0.0125f * 1.6666666f * 0.5f;

  const float visibleRadius = minimapSys.GetVisibleRadius();
  const float playerX = minimapSys.GetPlayerX();
  const float playerY = minimapSys.GetPlayerY();

  const auto &pins = minimapSys.GetPins();
  std::uint32_t newPinHitMask = 0;
  const std::uint32_t maxPinBits =
      std::min<std::uint32_t>(static_cast<std::uint32_t>(pins.size()), 32u);

  for (std::uint32_t i = 0; i < maxPinBits; ++i) {
    const auto &pin = pins[i];

    const float halfSize = frameW * 0.5f;
    const float invRange =
        (visibleRadius > 0.0f) ? (1.0f / visibleRadius) : 0.0f;
    const float minimapX =
        halfSize - (pin.y - playerY) * halfSize * invRange;
    const float minimapY =
        halfSize + (pin.x - playerX) * halfSize * invRange;

    float screenX = rect.left + minimapX;
    float screenY = rect.top + minimapY;
    if (minimapSys.GetMode() == openwow::ui::MinimapSystem::Mode::Rotate) {
      const float centerX = rect.left + frameW * 0.5f;
      const float centerY = rect.top + frameH * 0.5f;
      const float offsetX = screenX - centerX;
      const float offsetY = screenY - centerY;
      const float angle = -minimapSys.GetPlayerOrientation();
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);
      screenX = centerX + offsetX * cosine - offsetY * sine;
      screenY = centerY + offsetX * sine + offsetY * cosine;
    }

    if (std::fabs(cursorX - screenX) < blipHalfExtent &&
        std::fabs(cursorY - screenY) < blipHalfExtent) {
      newPinHitMask |= (1u << i);
    }
  }

  std::array<std::uint32_t, 23> newCategoryHitMasks{};
  auto &content = *minimap_content_;
  for (std::uint32_t category = 0;
       category < openwow::game::MinimapSystem::kObjectInfoCategoryCount;
       ++category) {
    const std::uint32_t count = std::min<std::uint32_t>(
        content.GetObjectInfoCategoryCount(category), 32u);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto *slot = content.GetObjectInfoCategorySlot(category, index);
      if (slot != nullptr &&
          std::fabs(cursorX - slot->GetProjectedX()) < blipHalfExtent &&
          std::fabs(cursorY - slot->GetProjectedY()) < blipHalfExtent) {
        newCategoryHitMasks[category] |= (1u << index);
      }
    }
  }

  bool masksChanged = (newPinHitMask != tooltipPinHitMask_);
  if (!masksChanged) {
    for (std::uint32_t c = 0; c < 23; ++c) {
      if (newCategoryHitMasks[c] != tooltipCategoryHitMasks_[c]) {
        masksChanged = true;
        break;
      }
    }
  }

  if (!masksChanged) {

    if (tooltipVisible_) {
      tooltipSys.PublishToLiveGameTooltipFrame();
    }
    return true;
  }

  tooltipPinHitMask_ = newPinHitMask;
  tooltipCategoryHitMasks_ = newCategoryHitMasks;

  bool anyHit = (newPinHitMask != 0);
  if (!anyHit) {
    for (const auto mask : newCategoryHitMasks) {
      if (mask != 0) {
        anyHit = true;
        break;
      }
    }
  }

  if (!anyHit) {
    if (tooltipVisible_) {
      tooltipSys.Hide();
      tooltipSys.HideLiveGameTooltipFrame();
      tooltipVisible_ = false;
    }
    return true;
  }

  const bool colorblindMode =
      openwow::ui::game::CVarSystem::Instance().GetCVarBool("colorblindMode");

  std::string tooltipText;
  tooltipText.reserve(512);

  for (std::uint32_t i = 0; i < maxPinBits; ++i) {
    if ((newPinHitMask & (1u << i)) == 0) {
      continue;
    }

    const auto &pin = pins[i];
    if (pin.tooltip.empty()) {
      continue;
    }

    if (!tooltipText.empty()) {
      tooltipText += '\n';
    }
    tooltipText += pin.tooltip;

    (void)colorblindMode;
  }

  const auto* const ui_manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua();
  const auto* const session = ui_manager != nullptr ? ui_manager->world_session() : nullptr;
  const auto* const objects = session != nullptr ? &session->objects() : nullptr;
  for (std::uint32_t category = 0;
       category < openwow::game::MinimapSystem::kObjectInfoCategoryCount;
       ++category) {
    const std::uint32_t mask = newCategoryHitMasks[category];
    if (mask == 0) {
      continue;
    }
    for (std::uint32_t index = 0; index < 32; ++index) {
      if ((mask & (1u << index)) == 0) {
        continue;
      }
      const auto *slot = content.GetObjectInfoCategorySlot(category, index);
      const auto *object =
          slot != nullptr && objects != nullptr ? objects->Get(slot->GetGuid()) : nullptr;
      if (object == nullptr || object->GetName().empty()) {
        continue;
      }
      if (!tooltipText.empty()) {
        tooltipText += '\n';
      }
      if (slot->HasTransportLayerMismatch()) {
        tooltipText += "|cffb0b0b0";
      }
      tooltipText += object->GetName();
      if (slot->HasTransportLayerMismatch()) {
        tooltipText += "|r";
      }
    }
  }

  if (tooltipText.empty()) {
    if (tooltipVisible_) {
      tooltipSys.Hide();
      tooltipSys.HideLiveGameTooltipFrame();
      tooltipVisible_ = false;
    }
    return true;
  }

  tooltipSys.SetOwner("Minimap", "ANCHOR_CURSOR");
  tooltipSys.ClearLines();
  tooltipSys.AddLine(tooltipText);
  tooltipSys.Show();
  tooltipSys.PublishToLiveGameTooltipFrame();
  tooltipVisible_ = true;

  return true;
}

void CSimpleWorldFrame::FireOnLeave(bool motion, bool clearDragState) {
  CSimpleFrame::FireOnLeave(motion, clearDragState);

  if (world_frame_ == nullptr) {
    return;
  }
  auto& wf = *world_frame_;
  if (!wf.GetMouseoverGuid().IsEmpty()) {
    wf.SetMouseoverGuid(openwow::game::ObjectGuid{});
  }

  if (auto* cursor = openwow::game::GetActiveCursorSurface()) {
    cursor->RestoreBaseCursor();
  }

  wf.SetCursorMode(openwow::render::WorldFrame::CursorMode::kCombat);
}

CSimpleGameTooltip::~CSimpleGameTooltip() {
  if (owns_default_tooltip_state_) {
    openwow::ui::game::TooltipSystem::ResetDefault();
  }
}

bool CSimpleMinimap::RefreshScaleCascade(bool force) {
  if (!CSimpleFrame::RefreshScaleCascade(force)) {
    return false;
  }

  minimap_state_->MarkExplorationOverlayDirty();
  return true;
}

void CSimpleMinimap::RegisterLayerRenderCallbacks(
    SimpleRenderBatchSink& sink, int layerIndex) {
  CSimpleFrame::RegisterLayerRenderCallbacks(sink, layerIndex);

  if (layerIndex == static_cast<int>(DrawLayer::Background)) {
    sink.AddDeferredRenderCallback([content = minimap_content_]() {

      content->RenderMinimapContent();
    });
  }
}

void CSimpleMinimap::FireOnUpdate(float elapsed) {
  CSimpleFrame::FireOnUpdate(elapsed);

  const float facing =
      minimap_state_->GetPlayerOrientation();
  SetPlayerDirection(facing);
}

void CSimpleMinimap::SetPlayerDirection(float facing) noexcept {
  playerDirection_ = facing;

  if (playerTexture_) {
    const float arrow_angle =
        (minimap_state_->GetMode() ==
         openwow::ui::MinimapSystem::Mode::Rotate)
            ? 0.0f
            : facing;

    playerTexture_->SetRotation(arrow_angle);
  }

  if (compassTexture_) {
    const float compass_angle =
        (minimap_state_->GetMode() ==
         openwow::ui::MinimapSystem::Mode::Rotate)
            ? -facing
            : 0.0f;
    compassTexture_->SetRotation(compass_angle);
  }
}

CSimpleMinimap* CSimpleMinimap::s_primaryInstance_ = nullptr;

CSimpleMinimap::CSimpleMinimap() : CSimpleFrame(ScriptObjectType::Minimap) {
  if (s_primaryInstance_ == nullptr) {
    s_primaryInstance_ = this;
  }

  playerTexture_ = std::make_unique<CSimpleTexture>();
  AddRegion(playerTexture_.get());
  {
    RegionAnchor player_arrow_anchor;
    player_arrow_anchor.point = FramePoint::Center;
    player_arrow_anchor.relativeTo = this;
    player_arrow_anchor.relativePoint = FramePoint::Center;
    player_arrow_anchor.offsetX = 0.0f;
    player_arrow_anchor.offsetY = 0.0f;
    playerTexture_->SetPoint(player_arrow_anchor);
  }

  InitializeStatusFontString();
}

CSimpleMinimap::~CSimpleMinimap() {
  if (s_primaryInstance_ == this) {
    s_primaryInstance_ = nullptr;
  }
}

void CSimpleMinimap::PostLoadProcess(const void* xmlNode, void* errorHandler) {
  CSimpleFrame::PostLoadProcess(xmlNode, errorHandler);

  static constexpr const char* kDefaultPlayerTexturePath =
      "Interface\\Minimap\\MinimapArrow.tga";

  const char* player_texture_path = nullptr;
  if (xmlNode != nullptr) {
    player_texture_path = openwow::ui::xml::XMLNode_GetAttributeValue(
        static_cast<const openwow::ui::xml::CXMLNode*>(xmlNode),
        "minimapPlayerTexture");
  }
  if (player_texture_path == nullptr || *player_texture_path == '\0') {
    player_texture_path = kDefaultPlayerTexturePath;
  }

  if (playerTexture_ && !playerTexture_->SetTexture(player_texture_path)) {
    auto* err = static_cast<openwow::ui::xml::ErrorContext*>(errorHandler);
    if (err != nullptr) {
      err->ReportError("Invalid minimapPlayerTexture in Minimap.xml");
    }
  }

  auto* region = static_cast<CSimpleTexture*>(
      openwow::ui::Script_FindNamedObjectByTypeTag(
          "MinimapCompassTexture", ScriptObjectType::Region));
  compassTexture_ = region;
}

void CSimpleMinimap::InitializeStatusFontString() {
  if (statusFontString_) {
    return;
  }

  auto status_font_string = std::make_unique<CSimpleFontString>();
  status_font_string->SetDrawLayer(DrawLayer::Artwork);

  RegionAnchor anchor;
  anchor.point = FramePoint::Center;
  anchor.relativeTo = this;
  anchor.relativePoint = FramePoint::Center;
  anchor.offsetX = kMinimapStatusAnchorOffset;
  anchor.offsetY = kMinimapStatusAnchorOffset;
  status_font_string->SetPoint(anchor);

  AddRegion(status_font_string.get());
  statusFontString_ = std::move(status_font_string);
}

void CSimpleFrame::FireOnUpdate(float elapsed) {
  if (HasScript("OnUpdate")) {
    lastElapsed_ = elapsed;
    RunScript("OnUpdate");
  }

  ProcessFrameUpdatePass();

  for (auto *region : regions_) {
    if (region) {
      region->ProcessFrameUpdatePass();
    }
  }

  ProcessFramePostUpdatePass(elapsed);

  for (auto *region : regions_) {
    if (region) {
      region->ProcessFramePostUpdatePass(elapsed);
    }
  }
}

void CSimpleFrame::FireOnSizeChanged(float height, float width) {
  if (HasScript("OnSizeChanged")) {
    lastOnSizeChangedHeight_ = height;
    lastOnSizeChangedWidth_ = width;
    RunScript("OnSizeChanged");
  }
  NotifyOwningScrollFrameOfContentChange();
}

void CSimpleFrame::FireOnShow() {
  if (HasScript("OnShow") && !loading_) {
    RunScript("OnShow");
  }
  NotifyOwningScrollFrameOfContentChange();
}

void CSimpleFrame::FireOnHide() {
  if (HasScript("OnHide") && !loading_) {
    RunScript("OnHide");
  }
  NotifyOwningScrollFrameOfContentChange();
}

bool CSimpleFrame::OnMouseMove(const void *inputEvent) {
  if (!inputEvent) {
    return false;
  }

  const auto &event = *static_cast<const openwow::input::InputEvent *>(inputEvent);
  const float cursorX = static_cast<float>(event.mouseX);
  const float cursorY = static_cast<float>(event.mouseY);

  if (dragState_.dragging && !dragState_.dragMoved) {
    const float dx = cursorX - dragState_.startX;
    const float dy = cursorY - dragState_.startY;
    if (dx * dx + dy * dy >= kDragThresholdSq) {
      dragState_.dragMoved = true;
      FireOnDragStart(dragState_.buttonFlags);
      dragState_.startX = 0.0f;
      dragState_.startY = 0.0f;
    }
  }

  if (!IsPointInFrame(cursorX, cursorY)) {
    return false;
  }

  if (!ignoreDepth_) {
    if (openwow::ui::IsCursorFrameDepthActive()) {
      openwow::ui::SetCursorFrameDepth(layoutDepth_);
    }
  }

  return true;
}

void CSimpleFrame::OnResize(const float *newRect) {
  if (!newRect)
    return;

  float oldW = rect_.right - rect_.left;
  float oldH = rect_.bottom - rect_.top;
  float newW = newRect[2] - newRect[0];
  float newH = newRect[3] - newRect[1];
  InvalidateRelativeDependentLayouts();
  SetRect(ScreenRect{
      .left = newRect[0],
      .top = newRect[1],
      .right = newRect[2],
      .bottom = newRect[3],
  });

  if (std::fabs(newW - oldW) >= kEpsilon || std::fabs(newH - oldH) >= kEpsilon) {
    const float inverse_layout_scale = 1.0f / layoutScale_;
    FireOnSizeChanged(newH * inverse_layout_scale, newW * inverse_layout_scale);
  }

  NotifyResolvedRectChanged(*this);
}

void CSimpleFrame::LoadNameAndId(const void *xmlNode, void * ) {
  const auto *frame_def = static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  if (frame_def && !frame_def->name.empty()) {
    SetFrameName(frame_def->name);
  }
  if (frame_def) {
    const auto id_it = frame_def->attributes.find("id");
    if (id_it != frame_def->attributes.end() && !id_it->second.empty()) {
      const auto parsed_id = static_cast<std::int32_t>(
          openwow::core::ParseSignedDecimalLikeSub76F0D0(id_it->second));
      if (parsed_id >= 0) {
        frameId_ = static_cast<uint32_t>(parsed_id);
      }
    }
  }
  loading_ = true;
  MarkLayoutDirty();

  if (auto *parent_frame = dynamic_cast<CSimpleFrame *>(parent_); parent_frame != nullptr) {
    parent_frame->MarkLayoutDirty();
  }

  for (auto *child : children_) {
    if (child != nullptr) {
      child->MarkLayoutDirty();
    }
  }
}

void CSimpleFrame::PostLoadProcess(const void *xmlNode, void *errorHandler) {
  ProcessInheritsAndFrames(xmlNode, errorHandler);
  loading_ = false;

  if (visible_) {
    bool ancestorHasAnimationGroups = false;
    bool ancestorHasAlphaAnimation = false;
    for (const CScriptRegion *region = GetParent(); region != nullptr; region = region->GetParent()) {
      ancestorHasAnimationGroups |= region->GetAttachedAnimationGroupCount() != 0;
      ancestorHasAlphaAnimation |=
          region->HasAttachedAnimationKind(openwow::ui::anim::AnimKind::Alpha);
    }

    const auto primeAnimationState = [&](auto &&self, CSimpleFrame *frame,
                                         const bool inheritedAnimationGroups,
                                         const bool inheritedAlphaAnimation) -> void {
      const bool hasAnimationGroups =
          inheritedAnimationGroups || frame->GetAttachedAnimationGroupCount() != 0;
      const bool hasAlphaAnimation =
          inheritedAlphaAnimation ||
          frame->HasAttachedAnimationKind(openwow::ui::anim::AnimKind::Alpha);

      if (hasAnimationGroups) {
        frame->computedAlpha_ = static_cast<uint8_t>(
            static_cast<uint32_t>(frame->currentAlpha_) * frame->inheritedAlpha_ / 255u);
        frame->protectionBits_ = static_cast<uint8_t>(
            (frame->protectionBits_ & ~0x2u) | (hasAlphaAnimation ? 0x2u : 0u));
      }

      for (auto *child : frame->children_) {
        if (child != nullptr) {
          self(self, child, hasAnimationGroups, hasAlphaAnimation);
        }
      }
    };

    primeAnimationState(primeAnimationState, this, ancestorHasAnimationGroups,
                        ancestorHasAlphaAnimation);
  }

  if (HasScript("OnLoad")) {
    RunScript("OnLoad");
  }

  if (visible_ && HasScript("OnShow") && !loading_) {
    RunScript("OnShow");
  }
}

void CSimpleFrame::CompileScriptHandlers(void * , void * ,
                                         void * , bool recursive) {

  if (recursive) {

    for (auto *child : children_) {
      if (child) {
        child->CompileScriptHandlers(nullptr, nullptr, nullptr, true);
      }
    }
  }
}

void CSimpleFrame::RegisterLayerRenderCallbacks(SimpleRenderBatchSink &sink, int layerIndex) {
  const auto layer_index = static_cast<size_t>(layerIndex);
  if (layer_index >= drawLayerEnabled_.size()) {
    return;
  }

  if (!drawLayerEnabled_[layer_index]) {
    return;
  }

  const auto layer = static_cast<DrawLayer>(layer_index);
  for (auto *region : regions_) {
    if (!region || region->GetDrawLayer() != layer) {
      continue;
    }

    if (!region->IsVisible()) {
      continue;
    }

    region->RegisterRenderCallbacks(sink);
  }
}

void CSimpleFrame::RefreshLayerVisibility(DrawLayer layer) {
  for (auto *region : regions_) {
    if (!region || region->GetDrawLayer() != layer) {
      continue;
    }

    region->RefreshVisibilityFromHierarchy();
  }
}

void CSimpleFrame::SetDrawLayerEnabled(DrawLayer layer, bool enabled) noexcept {
  SetDrawLayerStateInternal(layer, enabled, true);
}

bool CSimpleFrame::IsDrawLayerEnabled(DrawLayer layer) const noexcept {
  const auto index = static_cast<size_t>(layer);
  if (index >= drawLayerEnabled_.size()) {
    return true;
  }

  return drawLayerEnabled_[index];
}

void CSimpleFrame::StopAllRegionAnimations() {
  for (auto *region : regions_) {
    if (region) {
      region->StopAnimations();
    }
  }
}

void CSimpleFrame::SetFocusFrame(CSimpleFrame *focus) {
  if (focus == focusFrame_)
    return;

  focusFrame_ = focus;

  if (focus) {
    SetDrawLayerStateInternal(DrawLayer::Highlight, true, true);
    return;
  }

  auto *parentFrame = dynamic_cast<CSimpleFrame *>(parent_);
  if (!parentFrame || parentFrame->hoveredChildFrame_ != this) {
    SetDrawLayerStateInternal(DrawLayer::Highlight, false, true);
  }
}

void CSimpleFrame::AddChildToList(CSimpleFrame *child) {
  if (!child || std::find(children_.begin(), children_.end(), child) != children_.end()) {
    return;
  }

  children_.push_back(child);
}

void CSimpleFrame::RemoveChildFromList(CSimpleFrame *child) {
  if (!child)
    return;
  if (focusFrame_ == child) {
    focusFrame_ = nullptr;
  }
  ClearHoveredChildFrame(child);
  children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
}

void CSimpleFrame::RefreshVisibilityFromHierarchy() {
  if (shown_ && (!parent_ || parent_->IsVisible())) {
    ShowVisible();
  } else {
    HideVisible();
  }
}

void CSimpleFrame::ShowVisible() {
  if (visible_ || !shown_ || (parent_ && !parent_->IsVisible()))
    return;
  ApplyVisibilityCascade(true);
}

void CSimpleFrame::HideVisible() {
  if (!visible_)
    return;

  ApplyVisibilityCascade(false);
}

void CSimpleFrame::ApplyVisibilityCascade(const bool root_visible) {
  struct WorkItem {
    CSimpleFrame* frame{nullptr};
    CSimpleFrame* expectedParent{nullptr};
    bool visible{false};
    bool exiting{false};
    bool validateParent{false};
    std::uint64_t generation{0};
  };

  std::vector<WorkItem> work;
  work.reserve(64);
  work.push_back({this, nullptr, root_visible, false, false, 0});

  while (!work.empty()) {
    const WorkItem item = work.back();
    work.pop_back();
    CSimpleFrame* frame = item.frame;
    if (frame == nullptr) {
      continue;
    }

    if (item.exiting) {
      if (frame->visibilityGeneration_ != item.generation ||
          frame->visible_ != item.visible) {
        continue;
      }
      if (item.visible && frame->toplevel_) {
        (void)frame->RaiseNearestToplevelFrameIfIntersecting(true);
      }
      if (item.visible) {
        frame->FireOnShow();
      } else {
        frame->FireOnHide();
      }
      continue;
    }

    if ((item.validateParent && frame->parent_ != item.expectedParent) ||
        frame->visible_ == item.visible ||
        (item.visible &&
         (!frame->shown_ || (frame->parent_ && !frame->parent_->IsVisible())))) {
      continue;
    }

    frame->visible_ = item.visible;
    const std::uint64_t generation = ++frame->visibilityGeneration_;
    if (!item.visible) {
      frame->hasIntersectingVisiblePeer_ = false;
      auto& registry = GetFrameStackingRegistry();
      if (registry.activeMoveFrame == frame) {
        registry.activeMoveFrame = nullptr;
        registry.activeMoveSizingMode = 0;
      }
    }

    frame->MarkToplevelOverlapStateDirty();
    for (CScriptRegion* region : frame->regions_) {
      if (region != nullptr) {
        region->RefreshVisibilityFromHierarchy();
      }
    }

    work.push_back(
        {frame, item.expectedParent, item.visible, true,
         item.validateParent, generation});
    for (auto child = frame->children_.rbegin(); child != frame->children_.rend(); ++child) {
      if (*child != nullptr) {
        work.push_back({*child, frame, item.visible, false, true, 0});
      }
    }
  }
}

void CSimpleFrame::SetScrollChildHierarchyState(bool in_scroll_child_hierarchy,
                                                ScrollChildRootOverride root_override) {
  const bool was_visible = visible_;
  if (was_visible) {
    HideVisible();
  }

  inScrollChildHierarchy_ = in_scroll_child_hierarchy;
  switch (root_override) {
  case ScrollChildRootOverride::Keep:
    break;
  case ScrollChildRootOverride::Clear:
    isScrollChildRoot_ = false;
    break;
  case ScrollChildRootOverride::Set:
    isScrollChildRoot_ = true;
    break;
  }

  if (in_scroll_child_hierarchy) {
    pendingAnimSlots_ |= 0x1Fu;
  }

  for (auto *child : children_) {
    if (!child || child->isScrollChildRoot_) {
      continue;
    }

    child->SetScrollChildHierarchyState(in_scroll_child_hierarchy, ScrollChildRootOverride::Keep);
  }

  if (was_visible) {
    ShowVisible();
  }
}

void CSimpleFrame::NotifyOwningScrollFrameOfContentChange() {
  if (!inScrollChildHierarchy_ || !visible_) {
    return;
  }

  CSimpleFrame *scroll_child_root = this;
  while (scroll_child_root && !scroll_child_root->isScrollChildRoot_) {
    scroll_child_root = dynamic_cast<CSimpleFrame *>(scroll_child_root->GetParent());
  }
  if (!scroll_child_root) {
    return;
  }

  auto *scroll_frame = dynamic_cast<CSimpleScrollFrame *>(scroll_child_root->GetParent());
  if (!scroll_frame) {
    return;
  }

  scroll_frame->MarkScrollChildRectDirty();
}

CSimpleFrame::~CSimpleFrame() {
  const auto attached_regions = regions_;
  for (auto *region : attached_regions) {
    if (region != nullptr && region->GetParent() == this) {
      region->SetParent(nullptr);
    }
  }

  const auto child_frames = children_;
  for (auto *child : child_frames) {
    if (child != nullptr && child->GetParent() == this) {
      child->SetParentFrame(nullptr);
    }
  }

  if (auto *parent_frame = dynamic_cast<CSimpleFrame *>(parent_); parent_frame != nullptr) {
    parent_frame->RemoveChildFromList(this);
  }
  parent_ = nullptr;
  focusFrame_ = nullptr;
  hoveredChildFrame_ = nullptr;

  if (!GetName().empty()) {
    auto &registry = GetNamedFrameRegistry();
    const auto existing = registry.find(NormalizeFrameNameKey(GetName()));
    if (existing != registry.end() && existing->second == this) {
      registry.erase(existing);
    }
  }
  UnregisterFromStacking();
}

bool CSimpleFrame::Raise() {
  return RaiseNearestToplevelFrameIfIntersecting(true);
}

void CSimpleFrame::RegisterForStacking() {
  auto &registry = GetFrameStackingRegistry();
  if (std::find(registry.frames.begin(), registry.frames.end(), this) == registry.frames.end()) {
    registry.frames.push_back(this);
    registry.toplevelOverlapStateDirty = true;
  }
}

void CSimpleFrame::UnregisterFromStacking() noexcept {
  auto &registry = GetFrameStackingRegistry();
  registry.frames.erase(std::remove(registry.frames.begin(), registry.frames.end(), this),
                        registry.frames.end());
  registry.toplevelOverlapStateDirty = true;
  if (registry.activeMoveFrame == this) {
    registry.activeMoveFrame = nullptr;
    registry.activeMoveSizingMode = 0;
  }
}

void CSimpleFrame::MarkToplevelOverlapStateDirty() noexcept {
  GetFrameStackingRegistry().toplevelOverlapStateDirty = true;
}

void CSimpleFrame::RefreshCachedToplevelOverlapFlags() {
  auto &registry = GetFrameStackingRegistry();
  for (auto *frame : registry.frames) {
    if (!frame) {
      continue;
    }
    frame->hasIntersectingVisiblePeer_ =
        frame->visible_ && frame->IsToplevel() && frame->OverlapsVisiblePeerInStrata();
  }
  registry.toplevelOverlapStateDirty = false;
}

void CSimpleFrame::RefreshCachedToplevelOverlapFlagsIfDirty() {
  if (!GetFrameStackingRegistry().toplevelOverlapStateDirty) {
    return;
  }

  RefreshCachedToplevelOverlapFlags();
}

void CSimpleFrame::RefreshRaiseOverlapState() {
  if ((layoutFlags_ & kLayoutResolvedBit) == 0u || (layoutFlags_ & kLayoutQueuedBit) != 0u) {
    QueueLayoutInvalidation(true);
  }
  hasIntersectingVisiblePeer_ = OverlapsVisiblePeerInStrata();
}

void CSimpleFrame::NotifyResolvedRectChanged(const CSimpleFrame & ) {
  MarkToplevelOverlapStateDirty();
  auto &registry = GetFrameStackingRegistry();
  if (!registry.activeMoveFrame) {
    return;
  }

  (void)registry.activeMoveFrame->RaiseNearestToplevelFrameIfIntersecting(false);
}

void CSimpleFrame::SetFrameLevelCascade(const int32_t level) {
  const int32_t requested = std::max(level, 0);
  if (requested == level_) {
    return;
  }

  int32_t delta = requested - level_;
  if (delta > 128) {
    delta = 128;
  }

  level_ += delta;
  MarkToplevelOverlapStateDirty();

  for (auto *child : children_) {
    if (!child || child->GetFrameStrata() != strata_) {
      continue;
    }
    child->SetFrameLevelCascade(child->GetFrameLevel() + delta);
  }
}

CSimpleFrame *CSimpleFrame::FindNearestToplevelAncestor() noexcept {
  for (auto *current = this; current != nullptr;) {
    if (current->IsToplevel()) {
      return current;
    }
    current = dynamic_cast<CSimpleFrame *>(current->GetParent());
  }
  return nullptr;
}

const CSimpleFrame *CSimpleFrame::FindNearestToplevelAncestor() const noexcept {
  return const_cast<CSimpleFrame *>(this)->FindNearestToplevelAncestor();
}

bool CSimpleFrame::IsDescendantOf(const CSimpleFrame *ancestor) const noexcept {
  for (auto *current = dynamic_cast<CSimpleFrame *>(GetParent()); current != nullptr;) {
    if (current == ancestor) {
      return true;
    }
    current = dynamic_cast<CSimpleFrame *>(current->GetParent());
  }
  return false;
}

bool CSimpleFrame::OverlapsVisiblePeerInStrata() const {
  if (!visible_) {
    return false;
  }

  FrameBounds self_bounds;
  if (!ExtractCachedLayoutBounds(*this, self_bounds)) {
    return false;
  }

  for (auto *other : GetFrameStackingRegistry().frames) {
    if (!other || other == this || !other->visible_) {
      continue;
    }
    if (other->GetFrameStrata() != strata_ || other->GetFrameLevel() < level_) {
      continue;
    }
    if (other->IsDescendantOf(this)) {
      continue;
    }

    FrameBounds other_bounds;
    if (!ExtractCachedLayoutBounds(*other, other_bounds)) {
      continue;
    }
    if (BoundsIntersect(self_bounds, other_bounds)) {
      return true;
    }
  }

  return false;
}

bool CSimpleFrame::RaiseNearestToplevelFrameIfIntersecting(const bool refresh_overlap) {
  auto *target = FindNearestToplevelAncestor();
  if (!target) {
    return false;
  }

  if (refresh_overlap) {
    target->RefreshRaiseOverlapState();
  } else {
    RefreshCachedToplevelOverlapFlagsIfDirty();
  }

  if (!target->hasIntersectingVisiblePeer_) {
    return true;
  }

  CompactVisibleLevels(target->GetFrameStrata());
  target->SetFrameLevelCascade(CountOccupiedVisibleLevels(target->GetFrameStrata()));
  return true;
}

void CSimpleFrame::CompactVisibleLevels(const FrameStrata strata) {
  MarkToplevelOverlapStateDirty();
  std::vector<int32_t> occupied_levels;
  occupied_levels.reserve(GetFrameStackingRegistry().frames.size());

  for (auto *frame : GetFrameStackingRegistry().frames) {
    if (!frame || !frame->visible_ || frame->GetFrameStrata() != strata) {
      continue;
    }
    occupied_levels.push_back(frame->GetFrameLevel());
  }

  if (occupied_levels.empty()) {
    return;
  }

  std::sort(occupied_levels.begin(), occupied_levels.end());
  occupied_levels.erase(std::unique(occupied_levels.begin(), occupied_levels.end()),
                        occupied_levels.end());

  for (auto *frame : GetFrameStackingRegistry().frames) {
    if (!frame || !frame->visible_ || frame->GetFrameStrata() != strata) {
      continue;
    }

    const auto it =
        std::lower_bound(occupied_levels.begin(), occupied_levels.end(), frame->GetFrameLevel());
    if (it != occupied_levels.end() && *it == frame->GetFrameLevel()) {
      frame->level_ = static_cast<int32_t>(it - occupied_levels.begin());
    }
  }
}

int32_t CSimpleFrame::CountOccupiedVisibleLevels(const FrameStrata strata) {
  std::vector<int32_t> occupied_levels;
  occupied_levels.reserve(GetFrameStackingRegistry().frames.size());

  for (auto *frame : GetFrameStackingRegistry().frames) {
    if (!frame || !frame->visible_ || frame->GetFrameStrata() != strata) {
      continue;
    }
    occupied_levels.push_back(frame->GetFrameLevel());
  }

  std::sort(occupied_levels.begin(), occupied_levels.end());
  occupied_levels.erase(std::unique(occupied_levels.begin(), occupied_levels.end()),
                        occupied_levels.end());
  return static_cast<int32_t>(occupied_levels.size());
}

void CSimpleFrame::Show() {
  shown_ = true;
  RefreshVisibilityFromHierarchy();
}

void CSimpleFrame::Hide() {
  shown_ = false;
  HideVisible();
}

bool CSimpleFrame::IsDragMovedInHierarchy() const noexcept {
  if (dragState_.dragMoved) {
    return true;
  }
  return CScriptRegion::IsDragMovedInHierarchy();
}

bool CSimpleFrame::AllowsChildDrawLayer(DrawLayer layer) const noexcept {
  return IsDrawLayerEnabled(layer);
}

void CSimpleFrame::UpdateAlpha() {
  if ((protectionBits_ & 2) == 0)
    return;

  if (computedAlpha_ != currentAlpha_) {
    currentAlpha_ = computedAlpha_;
    alpha_ = static_cast<float>(currentAlpha_) / 255.0f;
    OnAlphaChanged();
  }
}

void CSimpleFrame::ProcessFrameUpdatePass() {
  UpdateAlpha();
}

uint8_t CSimpleFrame::ApplyAlphaFadeStep(int16_t step) {
  const uint8_t oldAlpha = currentAlpha_;

  const auto effective = static_cast<uint8_t>(
      static_cast<uint32_t>(oldAlpha) * static_cast<uint32_t>(inheritedAlpha_) / 255u);
  const int16_t newVal = step + static_cast<int16_t>(effective);

  int result;
  if (newVal > 255) {
    result = 255;
  } else if (newVal < 0) {
    result = 0;
  } else {
    result = static_cast<uint8_t>(newVal);
  }

  if (static_cast<uint8_t>(result) != oldAlpha) {
    currentAlpha_ = static_cast<uint8_t>(result);
    alpha_ = static_cast<float>(result) / 255.0f;
    OnAlphaChanged();
  }

  return static_cast<uint8_t>(result);
}

void CSimpleFrame::SetParentFrame(CSimpleFrame *newParent) {
  CSimpleFrame *oldParent = dynamic_cast<CSimpleFrame *>(parent_);
  if (newParent == oldParent)
    return;

  MarkToplevelOverlapStateDirty();

  const bool wasVisible = visible_;

  if (oldParent) {
    oldParent->RemoveChildFromList(this);
    oldParent->MarkLayoutDirty();
  }

  if (wasVisible) {
    HideVisible();
  }

  parent_ = newParent;

  if (newParent) {
    SetFrameStrata(newParent->GetFrameStrata());
    SetFrameLevel(newParent->GetFrameLevel() + 1);

    RefreshScaleCascade(false);
    inheritedDepth_ = newParent->GetEffectiveDepth();
    RefreshDepthCascade(false);
    const uint8_t inheritedAlpha = newParent->GetCascadedAlphaByte();
    if (inheritedAlpha != inheritedAlpha_) {
      inheritedAlpha_ = inheritedAlpha;
      OnAlphaChanged();
    }
    SetScrollChildHierarchyState(newParent->IsInScrollChildHierarchy(),
                                 ScrollChildRootOverride::Keep);
    newParent->AddChildToList(this);
    newParent->MarkLayoutDirty();
  } else {
    SetFrameStrata(FrameStrata::Medium);
    SetFrameLevel(0);
    RefreshScaleCascade(false);
    inheritedDepth_ = 1.0f;
    RefreshDepthCascade(false);
    if (inheritedAlpha_ != 0xFF) {
      inheritedAlpha_ = 0xFF;
      OnAlphaChanged();
    }
    SetScrollChildHierarchyState(false, ScrollChildRootOverride::Keep);
  }

  RefreshVisibilityFromHierarchy();
}

bool CSimpleFrame::CheckProtectedChildren(bool *outHasPending) {
  if (outHasPending != nullptr) {
    *outHasPending = false;
  }

  ProtectedFrameTraversalState state;
  state.active_frames.insert(this);
  state.active_regions.insert(this);
  for (const auto *child : children_) {
    if (child == nullptr) {
      continue;
    }
    if (IsProtectedLayoutRegionRecursive(child, &state)) {
      return true;
    }
  }

  return false;
}

bool CSimpleFrame::GetBoundsRect(float *bounds) {
  if (!bounds) {
    return false;
  }

  if ((layoutFlags_ & kLayoutQueuedBit) != 0u) {
    QueueLayoutInvalidation(true);
  }

  ScreenRect rect{};
  if (TryGetCachedLayoutRect(&rect)) {
    ExpandBoundsWithRect(bounds, rect);
  }

  for (auto *region : regions_) {
    if (region == nullptr || !region->IsShown()) {
      continue;
    }

    region->ResolvePendingLayoutRect();
    if (!region->TryGetCachedLayoutRect(&rect)) {
      continue;
    }

    ExpandBoundsWithRect(bounds, rect);
  }

  for (auto *child : children_) {
    if (child == nullptr || !child->IsShown()) {
      continue;
    }

    child->GetBoundsRect(bounds);
  }

  return bounds[2] > bounds[0] && bounds[3] > bounds[1];
}

bool CSimpleFrame::RefreshScaleCascade(bool force) {
  float effectiveScale = scale_;
  CSimpleFrame *parentFrame = dynamic_cast<CSimpleFrame *>(parent_);
  if (parentFrame) {
    effectiveScale *= parentFrame->GetEffectiveScale();
  }

  if ((!force && std::fabs(effectiveScale - layoutScale_) < kEpsilon) || effectiveScale == 0.0f) {
    return false;
  }

  SetLayoutScale(effectiveScale, force);

  for (auto *region : regions_) {
    if (region) {
      region->OnFrameScaleChanged(effectiveScale, force);
    }
  }

  for (auto *child : children_) {
    if (child) {
      child->RefreshScaleCascade(false);
    }
  }

  return true;
}

void CSimpleFrame::OnAlphaChanged() {
  for (auto *region : regions_) {
    if (region) {
      region->OnParentAlphaChanged(false);
    }
  }

  uint8_t effAlpha = static_cast<uint8_t>(static_cast<uint32_t>(currentAlpha_) * inheritedAlpha_ / 255);

  for (auto *child : children_) {
    if (!child)
      continue;
    if (effAlpha != child->inheritedAlpha_) {
      child->inheritedAlpha_ = effAlpha;
      child->OnAlphaChanged();
    }
  }
}

bool CSimpleFrame::RefreshDepthCascade(bool force) {
  const float effectiveDepth = inheritedDepth_ + depth_;

  if (!force && std::fabs(effectiveDepth - layoutDepth_) < kEpsilon) {
    return false;
  }

  SetLayoutDepth(effectiveDepth, force);

  for (auto *region : regions_) {
    if (region) {
      region->OnFrameDepthChanged(effectiveDepth, force);
    }
  }

  for (auto *child : children_) {
    if (child &&
        (force || std::fabs(effectiveDepth - child->inheritedDepth_) >= kEpsilon)) {
      child->inheritedDepth_ = effectiveDepth;
      child->RefreshDepthCascade(force);
    }
  }

  return true;
}

void CSimpleFrame::OnLayout() {
  StopAllAttachedAnimationGroups();

  ClearLayoutDirty();
  layoutFlags_ &= ~kLayoutQueuedBit;
  layoutQueued_ = false;
  layoutRetryCount_ = 0;

  for (auto *region : regions_) {
    if (!region)
      continue;
    region->ClearLayoutDirty();
    region->OnLayout();
  }

  for (auto *child : children_) {
    if (!child)
      continue;
    child->OnLayout();
  }
}

void CSimpleFrame::PreRender(const int arg1, const bool runProtectionTransition,
                             const bool primeAlphaState) {
  if (runProtectionTransition && GetAttachedAnimationGroupCount() == 0) {
    EnableProtection();
  }

  if (primeAlphaState && (protectionBits_ & 0x2u) == 0) {
    protectionBits_ |= 0x2u;
    computedAlpha_ =
        static_cast<uint8_t>(static_cast<uint32_t>(currentAlpha_) * inheritedAlpha_ / 255u);
  }

  ForEachChildFrameThenRegion(
      *this,
      [&](CSimpleFrame &child) { child.PreRender(arg1, runProtectionTransition, primeAlphaState); },
      [&](CScriptRegion &region) {
        region.PreRender(arg1, runProtectionTransition, primeAlphaState);
      });
}

void CSimpleFrame::PostRender(const int arg1, const bool runProtectionTransition) {
  if ((protectionBits_ & 0x2u) != 0) {
    const bool hasActiveAlphaAnimation =
        HasAttachedAnimationKindInSelfOrAncestorHierarchy(openwow::ui::anim::AnimKind::Alpha);
    protectionBits_ = static_cast<uint8_t>((protectionBits_ & ~0x2u) |
                                           (hasActiveAlphaAnimation ? 0x2u : 0u));
    if (!hasActiveAlphaAnimation && computedAlpha_ != currentAlpha_) {
      currentAlpha_ = computedAlpha_;
      alpha_ = static_cast<float>(currentAlpha_) / 255.0f;
      OnAlphaChanged();
    }
  }

  if (runProtectionTransition && GetAttachedAnimationGroupCount() == 0) {
    DisableProtection();
  }

  ForEachChildFrameThenRegion(
      *this, [&](CSimpleFrame &child) { child.PostRender(arg1, runProtectionTransition); },
      [&](CScriptRegion &region) { region.PostRender(arg1, runProtectionTransition); });
}

void CSimpleFrame::OnUpdateCascade(float elapsed) {
  ForEachChildFrameThenRegion(
      *this, [&](CSimpleFrame &child) { child.OnUpdateCascade(elapsed); },
      [&](CScriptRegion &region) { region.OnUpdateCascade(elapsed); });
}

void CSimpleFrame::ApplyAnimRotation(FramePoint anchorPoint,
                                     const float* originOffset,
                                     float radians) {
  for (auto *child : children_) {
    if (!child)
      continue;
    child->ApplyAnimRotation(anchorPoint, originOffset, radians);
  }

  for (auto *region : regions_) {
    if (!region)
      continue;
    region->ApplyAnimRotation(anchorPoint, originOffset, radians);
  }
}

void CSimpleFrame::ApplyAnimScale(std::uint32_t originPoint,
                                  const float* originOffset,
                                  const float* scaleDelta) {
  for (auto *child : children_) {
    if (!child)
      continue;
    child->ApplyAnimScale(originPoint, originOffset, scaleDelta);
  }

  for (auto *region : regions_) {
    if (!region)
      continue;
    region->ApplyAnimScale(originPoint, originOffset, scaleDelta);
  }
}

void CSimpleFrame::OnEventCascade(int eventId, int arg1, int arg2) {
  ForEachChildFrameThenRegion(
      *this, [&](CSimpleFrame &child) { child.OnEventCascade(eventId, arg1, arg2); },
      [&](CScriptRegion &region) { region.OnEventCascade(eventId, arg1, arg2); });
}

void CSimpleFrame::RunAnimationSlots() {
  const uint32_t pending = pendingAnimSlots_;
  pendingAnimSlots_ = 0;

  for (uint32_t i = 0; i < kNumAnimSlots; ++i) {
    if (((1u << i) & pending) != 0) {
      auto &slot_batch = animationSlotBatches_[i];
      slot_batch.Reset();

      class AnimationSlotBatchSink final : public SimpleRenderBatchSink {
      public:
        AnimationSlotBatchSink(DeferredRenderCallbackList &deferred_callbacks,
                               uint32_t &text_count, uint32_t &texture_count,
                               uint32_t &line_count) noexcept
            : deferred_callbacks_(deferred_callbacks),
              text_count_(text_count),
              texture_count_(texture_count),
              line_count_(line_count) {}

        [[nodiscard]] const SimpleRenderBatchClipRect &GetClipRect() const noexcept override {
          static const SimpleRenderBatchClipRect kFullClipRect{0.0f, 0.0f, 1.0f, 1.0f};
          return kFullClipRect;
        }

        void AddText(const CSimpleRender &, std::string_view) override {
          ++text_count_;
        }

        void AddEmbeddedTexture(const CSimpleTexture &) override {
          ++texture_count_;
        }

        void AddLine(const CSimpleLine &) override {
          ++line_count_;
        }

        void AddDeferredRenderCallback(DeferredRenderCallback callback) override {
          deferred_callbacks_.Add(std::move(callback));
        }

      private:
        DeferredRenderCallbackList &deferred_callbacks_;
        uint32_t &text_count_;
        uint32_t &texture_count_;
        uint32_t &line_count_;
      };

      AnimationSlotBatchSink sink(slot_batch.deferredCallbacks, slot_batch.textCount,
                                  slot_batch.textureCount, slot_batch.lineCount);
      RegisterLayerRenderCallbacks(sink, static_cast<int>(i));
    }
  }

  uint32_t remaining = pendingAnimSlots_;
  if ((remaining & kRenderRetryBit) != 0) {
    remaining |= 0x1Fu;
  }
  pendingAnimSlots_ = remaining;

  for (const auto &slot_batch : animationSlotBatches_) {
    slot_batch.Submit();
  }

  for (auto *child : children_) {
    if (!child)
      continue;
    if (child->visible_ && !child->isScrollChildRoot_) {
      child->RunAnimationSlots();
    }
  }
}

bool CSimpleFrame::IsPointInFrame(float x, float y) const noexcept {
  if ((layoutFlags_ & kLayoutResolvedBit) == 0u) {
    return false;
  }

  if (!inScrollChildHierarchy_) {
    return openwow::ui::RectContainsPointInclusive(
        rect_.left, rect_.top, rect_.right, rect_.bottom, x, y);
  }

  const CSimpleFrame* ancestor = nullptr;
  {
    auto* p = parent_;
    if (!p) {
      return false;
    }

    while (p && p->IsKindOf(ScriptObjectType::Frame)) {
      auto* frame = static_cast<const CSimpleFrame*>(p);
      if (!frame->inScrollChildHierarchy_) {
        ancestor = frame;
        break;
      }
      p = frame->parent_;
    }
  }

  if (!ancestor) {
    return false;
  }

  const auto clipped = openwow::ui::IntersectRectsLTRB(
      ancestor->rect_.left, ancestor->rect_.top,
      ancestor->rect_.right, ancestor->rect_.bottom,
      rect_.left, rect_.top, rect_.right, rect_.bottom);

  return openwow::ui::RectContainsPointInclusive(
      clipped.left, clipped.top, clipped.right, clipped.bottom, x, y);
}

void CSimpleFrame::StopMovingOrSizing() {
  auto &registry = GetFrameStackingRegistry();
  if (registry.activeMoveFrame != this) {
    return;
  }

  registry.activeMoveSizingMode = 0;
  registry.activeMoveFrame = nullptr;
}

float CSimpleFrame::PixelSnap(float value) {
  return openwow::ui::LegacyPixelSnapUiHorizontalCoordinate(value);
}

void CSimpleFrame::LoadAttributesXML(const void * , void * ) {

}

void CSimpleFrame::LoadXML(const void *xmlNode, void *errorHandler) {
  const auto *frame_def = static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  auto *error_handler = static_cast<openwow::ui::xml::ErrorContext *>(errorHandler);
  if (frame_def == nullptr) {
    return;
  }

  for (const auto &inherited_name : openwow::ui::framexml::SplitTemplateList(
           frame_def->inherits, openwow::ui::framexml::TemplateListSyntax::kCommaSeparated)) {
    const auto *inherited_def = openwow::ui::xml::FrameXMLParser::GetTemplate(inherited_name);
    if (inherited_def == nullptr) {
      if (error_handler != nullptr) {
        error_handler->ReportError("Couldn't find inherited node: %s", inherited_name.c_str());
      }
      continue;
    }

    if (IsTemplateActive(inherited_name)) {
      if (error_handler != nullptr) {
        error_handler->ReportError("Recursively inherited node: %s", inherited_name.c_str());
      }
      continue;
    }

    ScopedInheritedTemplate inherited_scope(inherited_name);
    LoadXML(inherited_def, errorHandler);
  }

  LoadAnchorPointsFromXML(frame_def, errorHandler);

  if (const char *hidden = FindAttributeValue(*frame_def, "hidden");
      hidden != nullptr && *hidden != '\0') {
    if (ScriptParseBoolStringOrDefault(hidden, false)) {
      Hide();
    } else {
      Show();
    }
  }

  if (const char *toplevel = FindAttributeValue(*frame_def, "toplevel");
      toplevel != nullptr && *toplevel != '\0') {
    SetToplevel(ScriptParseBoolStringOrDefault(toplevel, false));
  }

  if (const char *movable = FindAttributeValue(*frame_def, "movable");
      movable != nullptr && *movable != '\0') {
    SetMovable(ScriptParseBoolStringOrDefault(movable, false));
  }

  if (const char *dont_save_position = FindAttributeValue(*frame_def, "dontSavePosition");
      dont_save_position != nullptr && *dont_save_position != '\0') {
    SetDontSavePosition(ScriptParseBoolStringOrDefault(dont_save_position, false));
  }

  if (const char *resizable = FindAttributeValue(*frame_def, "resizable");
      resizable != nullptr && *resizable != '\0') {
    SetResizable(ScriptParseBoolStringOrDefault(resizable, false));
  }

  if (const char *frame_strata = FindAttributeValue(*frame_def, "frameStrata");
      frame_strata != nullptr && *frame_strata != '\0') {
    int parsed_strata = 0;
    if (openwow::ui::StringToScriptFrameStrata(frame_strata, &parsed_strata) != 0) {
      SetFrameStrata(static_cast<FrameStrata>(parsed_strata));
    } else if (error_handler != nullptr) {
      error_handler->ReportError("Frame %s: Unknown frame strata: %s",
                                 FrameXmlDisplayName(*this), frame_strata);
    }
  }

  if (const char *frame_level = FindAttributeValue(*frame_def, "frameLevel");
      frame_level != nullptr && *frame_level != '\0') {
    const auto parsed_level = static_cast<std::int32_t>(
        openwow::core::ParseSignedDecimalLikeSub76F0D0(frame_level));
    if (parsed_level > 0) {
      SetFrameLevel(parsed_level);
    } else if (error_handler != nullptr) {
      error_handler->ReportError("Frame %s: Unknown frame level: %s",
                                 FrameXmlDisplayName(*this), frame_level);
    }
  }

  if (const char *alpha = FindAttributeValue(*frame_def, "alpha");
      alpha != nullptr && *alpha != '\0') {
    const auto parsed_alpha = static_cast<float>(std::clamp(
        openwow::core::ParseFloatLikeSub76FB80(alpha), 0.0, 1.0));
    const auto alpha_byte = static_cast<std::uint8_t>(parsed_alpha * 255.0f);
    if (alpha_byte != currentAlpha_) {
      currentAlpha_ = alpha_byte;
      alpha_ = static_cast<float>(currentAlpha_) / 255.0f;
      OnAlphaChanged();
    }
  }

  if (const char *enable_mouse = FindAttributeValue(*frame_def, "enableMouse");
      enable_mouse != nullptr && *enable_mouse != '\0'
      && ScriptParseBoolStringOrDefault(enable_mouse, false)) {
    EnableMouse(true);
  }

  if (const char *enable_keyboard = FindAttributeValue(*frame_def, "enableKeyboard");
      enable_keyboard != nullptr && *enable_keyboard != '\0'
      && ScriptParseBoolStringOrDefault(enable_keyboard, false)) {
    EnableKeyboard(true);
  }

  if (const char *clamped_to_screen = FindAttributeValue(*frame_def, "clampedToScreen");
      clamped_to_screen != nullptr && *clamped_to_screen != '\0') {
    SetClampedToScreen(ScriptParseBoolStringOrDefault(clamped_to_screen, false));
  }

  if (const char *is_protected = FindAttributeValue(*frame_def, "protected");
      is_protected != nullptr && *is_protected != '\0'
      && ScriptParseBoolStringOrDefault(is_protected, false)) {
    EnableProtection();
  }

  if (const char *depth = FindAttributeValue(*frame_def, "depth");
      depth != nullptr && *depth != '\0') {
    SetDepth(static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(depth)), false);
  }

  for (const auto &child : frame_def->raw_node.children) {
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "TitleRegion")) {
      titleRegion_ = CreateFrameTitleRegionFromXmlNode(*this, child, error_handler);
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "ResizeBounds")) {
      LoadFrameResizeBoundsFromXmlNode(*this, child, error_handler);
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "Backdrop")) {
      LoadFrameBackdropFromXmlNode(*this, child, error_handler);
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "HitRectInsets")) {
      float left = 0.0f;
      float right = 0.0f;
      float top = 0.0f;
      float bottom = 0.0f;
      if (openwow::ui::xml::RelInset_ref(&child, &left, &right, &top, &bottom, error_handler) != 0) {
        SetHitRectInsets(left, right, top, bottom);
      }
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "Layers")) {
      ProcessLayerElements(frame_def, errorHandler);
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "Attributes")) {
      LoadAttributesXML(&child, errorHandler);
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "Scripts")) {
      LoadScriptElements(&child, errorHandler);
      continue;
    }
  }
}

void CSimpleFrame::StartMoving(int moveSizingMode) {
  auto &registry = GetFrameStackingRegistry();
  registry.activeMoveFrame = this;
  registry.activeMoveSizingMode = moveSizingMode;
}

}
