#include "openwow/ui/game/nameplate_frame_manager.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/runtime/frame_materializer.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace openwow::ui::game {
namespace {

using UiFrame = openwow::ui::framexml::UiFrame;

constexpr char kWorldFrameKey[] = "WorldFrame";

constexpr char kThreatFlashTexture[] =
    "Interface\\TargetingFrame\\UI-TargetingFrame-Flash";
constexpr char kBorderTexture[] = "Interface\\Tooltips\\Nameplate-Border";
constexpr char kBarFillTexture[] =
    "Interface\\TargetingFrame\\UI-TargetingFrame-BarFill";
constexpr char kCastShieldTexture[] =
    "Interface\\Tooltips\\Nameplate-CastBar-Shield";
constexpr char kGlowTexture[] = "Interface\\Tooltips\\Nameplate-Glow";
constexpr char kSkullTexture[] =
    "Interface\\TargetingFrame\\UI-TargetingFrame-Skull";
constexpr char kRaidTargetIconsTexture[] =
    "Interface\\TargetingFrame\\UI-RaidTargetingIcons";
constexpr char kEliteIconTexture[] =
    "Interface\\Tooltips\\EliteNameplateIcon";

constexpr char kKeyThreatFlash[] = "owThreatFlash";
constexpr char kKeyBorder[] = "owBorder";
constexpr char kKeyHealthBar[] = "owHealthBar";
constexpr char kKeyCastBorder[] = "owCastBorder";
constexpr char kKeyCastShield[] = "owCastShield";
constexpr char kKeyCastBar[] = "owCastBar";
constexpr char kKeyCastIcon[] = "owCastIcon";
constexpr char kKeyGlow[] = "owGlow";
constexpr char kKeyName[] = "owName";
constexpr char kKeyLevel[] = "owLevel";
constexpr char kKeySkull[] = "owSkull";
constexpr char kKeyRaidIcon[] = "owRaidIcon";
constexpr char kKeyElite[] = "owElite";

constexpr float kFrameStoredWidth = 0.1f;
constexpr float kFrameStoredHeight = 0.025f;
constexpr float kThreatStoredWidth = 0.11f;
constexpr float kThreatStoredHeight = 0.029f;
constexpr float kThreatStoredOffsetX = -0.001f;
constexpr float kThreatStoredOffsetY = -0.0065f;
constexpr float kNameFontStoredHeight = 0.01f;
constexpr float kLevelFontStoredHeight = 0.009f;
constexpr float kNameShadowStoredOffsetX = 0.001f;
constexpr float kNameShadowStoredOffsetY = -0.001f;
constexpr float kStatusBarInsetXRatio = 0.031f;
constexpr float kStatusBarOffsetYRatio = 0.125f;
constexpr float kCastIconAnchorXRatio = 0.092f;
constexpr float kCastIconAnchorYRatio = 0.284f;
constexpr float kCastIconStoredSize = 0.01f;
constexpr float kSkullStoredSize = 0.01f;
constexpr float kEliteStoredWidth = 0.0294f;
constexpr float kEliteStoredHeight = 0.0215f;
constexpr float kEliteStoredOffsetX = 0.003f;
constexpr float kEliteStoredOffsetY = -0.001f;
constexpr float kRaidTargetIconStoredSize = 0.02f;

constexpr float kCastBorderUvLeft = 1.0f;
constexpr float kCastBorderUvRight = 0.0f;

constexpr float kThreatUvLeft = 0.0f;
constexpr float kThreatUvRight = 0.555f;
constexpr float kThreatUvTop = 0.53f;
constexpr float kThreatUvBottom = 0.6f;

constexpr float kEliteUvRight = 0.578125f;
constexpr float kEliteUvBottom = 0.84375f;

constexpr std::uint8_t kMaxRaidTargetIconIndex = 7u;
constexpr float kRaidTargetAtlasTileSize = 0.25f;

constexpr float kRaidIconFadeNearDistance = 4.0f;
constexpr float kRaidIconFadeFarDistance = 8.0f;
constexpr float kRaidIconMinimumAlpha = 34.0f / 255.0f;
constexpr float kRaidIconAlphaPerYard = 55.25f;

constexpr int kPlateDefaultFrameLevel = 10;
constexpr int kPlateStatusBarFrameLevel = kPlateDefaultFrameLevel - 1;

constexpr unsigned kPlateCallFailureReports = 8u;

[[nodiscard]] float ColorComponent(const std::uint32_t argb,
                                   const unsigned shift) {
  return static_cast<float>((argb >> shift) & 0xFFu) / 255.0f;
}

class MethodCall {
 public:
  MethodCall(lua_State* state, const int object_index, const char* method)
      : lua_(state), base_(lua_gettop(state)) {
    const int object = lua_absindex(state, object_index);
    lua_getfield(state, object, method);
    if (lua_isfunction(state, -1) == 0) {
      lua_settop(state, base_);
      valid_ = false;
      return;
    }
    lua_pushvalue(state, object);
    valid_ = true;
  }

  MethodCall(const MethodCall&) = delete;
  MethodCall& operator=(const MethodCall&) = delete;

  MethodCall& Number(const double value) {
    if (valid_) {
      lua_pushnumber(lua_, value);
      ++args_;
    }
    return *this;
  }
  MethodCall& String(const char* value) {
    if (valid_) {
      lua_pushstring(lua_, value);
      ++args_;
    }
    return *this;
  }
  MethodCall& Object(const int index) {
    if (valid_) {
      lua_pushvalue(lua_, lua_absindex(lua_, index));
      ++args_;
    }
    return *this;
  }

  bool Invoke() {
    if (!valid_) {
      return false;
    }
    valid_ = false;
    if (lua_pcall(lua_, args_ + 1, 0, 0) != 0) {

      if (failure_reports_remaining_ != 0u) {
        --failure_reports_remaining_;
        const char* message = lua_tostring(lua_, -1);
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            std::string("NameplateFrameManager: plate widget call failed: ") +
                (message != nullptr ? message : "unknown") +
                (failure_reports_remaining_ == 0u ? " (further plate widget "
                                                    "failures suppressed)"
                                                  : ""));
      }
      lua_settop(lua_, base_);
      return false;
    }
    lua_settop(lua_, base_);
    return true;
  }

  ~MethodCall() {
    if (valid_) {
      lua_settop(lua_, base_);
    }
  }

 private:
  lua_State* lua_;
  int base_;
  int args_{0};
  bool valid_{false};

  static inline unsigned failure_reports_remaining_{kPlateCallFailureReports};
};

void CallShow(lua_State* state, const int index, const bool shown) {
  (void)MethodCall(state, index, shown ? "Show" : "Hide").Invoke();
}

[[nodiscard]] int PushRegion(lua_State* state, const int plate_index,
                             const char* key) {
  lua_getfield(state, plate_index, key);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return 0;
  }
  return lua_gettop(state);
}

UiFrame MakeTexture(std::string name, const char* draw_layer,
                    const char* parent_key, const char* file,
                    const char* parent, const bool visible = true) {
  UiFrame frame;
  frame.kind = "Texture";
  frame.name = std::move(name);
  frame.publish_to_lua = false;
  frame.parent = parent;
  frame.parent_keys = {parent_key};
  frame.draw_layer = draw_layer;

  frame.visible = visible;
  frame.visibility_explicit = true;
  if (file != nullptr) {
    frame.file = file;
  }
  return frame;
}

UiFrame MakeFontString(std::string name, const char* parent_key,
                       const char* parent, const bool visible = true) {
  UiFrame frame;
  frame.kind = "FontString";
  frame.name = std::move(name);
  frame.publish_to_lua = false;
  frame.parent = parent;
  frame.parent_keys = {parent_key};

  frame.draw_layer = "OVERLAY";
  frame.visible = visible;
  frame.visibility_explicit = true;
  return frame;
}

UiFrame MakeStatusBar(std::string name, const char* parent_key,
                      const char* parent, const bool visible = true) {
  UiFrame frame;
  frame.kind = "StatusBar";
  frame.name = std::move(name);
  frame.publish_to_lua = false;
  frame.parent = parent;
  frame.parent_keys = {parent_key};
  frame.has_frame_level = true;
  frame.frame_level = kPlateStatusBarFrameLevel;
  frame.visible = visible;
  frame.visibility_explicit = true;
  openwow::ui::widgets::StatusBarDefinition definition;
  definition.minimum = 0.0f;
  definition.maximum = 1.0f;
  definition.default_value = 0.0f;
  frame.status_bar = std::move(definition);
  return frame;
}

}

NameplateFrameManager::NameplateFrameManager(
    runtime::FrameMaterializer& materializer, runtime::FrameStore& frames)
    : materializer_(materializer), frames_(frames) {}

void NameplateFrameManager::BindLuaState(lua_State* const state) {
  if (state == lua_) {
    return;
  }
  ReleasePlates();
  lua_ = state;
  world_frame_missing_logged_ = false;
}

void NameplateFrameManager::ReleasePlates() {
  if (lua_ != nullptr) {
    for (auto& plate : plates_) {
      if (plate.lua_ref != LUA_NOREF) {
        luaL_unref(lua_, LUA_REGISTRYINDEX, plate.lua_ref);
      }
      if (!plate.key.empty()) {
        frames_.DestroySubtree(plate.key);
      }
    }
  }
  plates_.clear();
  active_plates_ = 0;
  NameplateFrameChannel::Get().RecordWidgetUpdate({});
}

bool NameplateFrameManager::CreatePlate(PlateState& plate) {

  const std::string root_name = "NamePlate";
  UiFrame root;
  root.kind = "Frame";
  root.name = root_name;
  root.publish_to_lua = false;
  root.parent = kWorldFrameKey;
  root.visible = false;
  root.has_frame_level = true;
  root.frame_level = kPlateDefaultFrameLevel;

  root.frame_strata = "BACKGROUND";

  const char* const parent = root_name.c_str();
  std::vector<UiFrame> children;
  children.reserve(13);

  children.push_back(MakeTexture("NamePlateThreatFlash", "BACKGROUND",
                                 kKeyThreatFlash, kThreatFlashTexture, parent,
                                 false));

  {
    auto border = MakeTexture("NamePlateBorder", "ARTWORK", kKeyBorder,
                              kBorderTexture, parent);
    border.set_all_points = true;
    border.set_all_points_explicit = true;
    children.push_back(std::move(border));
  }

  children.push_back(MakeStatusBar("NamePlateHealthBar", kKeyHealthBar, parent));

  children.push_back(MakeTexture("NamePlateCastBorder", "ARTWORK",
                                 kKeyCastBorder, kBorderTexture, parent, false));

  children.push_back(MakeTexture("NamePlateCastShield", "ARTWORK",
                                 kKeyCastShield, kCastShieldTexture, parent,
                                 false));

  children.push_back(MakeStatusBar("NamePlateCastBar", kKeyCastBar, parent, false));

  children.push_back(MakeTexture("NamePlateCastIcon", "OVERLAY", kKeyCastIcon,
                                 nullptr, parent, false));

  {
    auto glow = MakeTexture("NamePlateGlow", "HIGHLIGHT", kKeyGlow, kGlowTexture,
                            parent, false);
    glow.alpha_mode = "ADD";
    glow.set_all_points = true;
    glow.set_all_points_explicit = true;
    children.push_back(std::move(glow));
  }

  children.push_back(MakeFontString("NamePlateName", kKeyName, parent));
  children.push_back(MakeFontString("NamePlateLevel", kKeyLevel, parent, false));

  children.push_back(
      MakeTexture("NamePlateSkull", "OVERLAY", kKeySkull, kSkullTexture, parent,
                  false));

  children.push_back(MakeTexture("NamePlateRaidIcon", "ARTWORK", kKeyRaidIcon,
                                 kRaidTargetIconsTexture, parent, false));

  children.push_back(MakeTexture("NamePlateElite", "BORDER", kKeyElite,
                                 kEliteIconTexture, parent, false));

  const int ref = materializer_.InstantiateFrameTree(std::move(root),
                                                     std::move(children), 0,
                                                     false, false, false);
  if (ref == LUA_NOREF) {
    return false;
  }
  plate.lua_ref = ref;

  lua_rawgeti(lua_, LUA_REGISTRYINDEX, ref);
  const int plate_index = lua_gettop(lua_);
  lua_getfield(lua_, plate_index, frame_api::kLuaFrameRuntimeKeyField);
  if (const char* key = lua_tostring(lua_, -1); key != nullptr) {
    plate.key = key;
  }
  lua_pop(lua_, 1);

  if (const int threat = PushRegion(lua_, plate_index, kKeyThreatFlash);
      threat != 0) {
    (void)MethodCall(lua_, threat, "SetTexCoord")
        .Number(kThreatUvLeft)
        .Number(kThreatUvRight)
        .Number(kThreatUvTop)
        .Number(kThreatUvBottom)
        .Invoke();
    lua_settop(lua_, plate_index);
  }
  if (const int cast_border = PushRegion(lua_, plate_index, kKeyCastBorder);
      cast_border != 0) {
    (void)MethodCall(lua_, cast_border, "SetTexCoord")
        .Number(kCastBorderUvLeft)
        .Number(kCastBorderUvRight)
        .Number(0.0f)
        .Number(1.0f)
        .Invoke();
    lua_settop(lua_, plate_index);
  }
  if (const int elite = PushRegion(lua_, plate_index, kKeyElite); elite != 0) {
    (void)MethodCall(lua_, elite, "SetTexCoord")
        .Number(0.0f)
        .Number(kEliteUvRight)
        .Number(0.0f)
        .Number(kEliteUvBottom)
        .Invoke();
    lua_settop(lua_, plate_index);
  }
  for (const char* bar : {kKeyHealthBar, kKeyCastBar}) {
    if (const int index = PushRegion(lua_, plate_index, bar); index != 0) {
      (void)MethodCall(lua_, index, "SetStatusBarTexture")
          .String(kBarFillTexture)
          .Invoke();
      (void)MethodCall(lua_, index, "SetMinMaxValues")
          .Number(0.0)
          .Number(1.0)
          .Invoke();
      lua_settop(lua_, plate_index);
    }
  }
  lua_settop(lua_, plate_index - 1);
  return true;
}

bool NameplateFrameManager::EnsurePlate(const std::size_t index) {
  if (index < plates_.size() && plates_[index].lua_ref != LUA_NOREF) {
    return true;
  }
  if (index >= plates_.size()) {
    plates_.resize(index + 1u);
  }
  auto& plate = plates_[index];
  plate.lua_ref = LUA_NOREF;
  if (!CreatePlate(plate)) {
    plate.lua_ref = LUA_NOREF;
    return false;
  }
  return true;
}

void NameplateFrameManager::ApplyGeometry(
    PlateState& plate, const int plate_index,
    const NameplateScreenLayout& layout) {

  const float scale = layout.ui_pixel_scale > 0.0f ? layout.ui_pixel_scale : 1.0f;
  const float unit = layout.geometry.pixels_per_stored / scale;
  if (!(unit > 0.0f)) {
    return;
  }
  if (plate.geometry_valid &&
      std::fabs(plate.applied_units_per_stored - unit) < 1e-4f) {
    return;
  }
  plate.applied_units_per_stored = unit;
  plate.geometry_valid = true;

  const float frame_width = kFrameStoredWidth * unit;
  const float frame_height = kFrameStoredHeight * unit;
  const float bar_inset_x = frame_width * kStatusBarInsetXRatio;
  const float bar_offset_y = frame_height * kStatusBarOffsetYRatio;
  const float bar_width =
      layout.geometry.cast_bar_width / scale;
  const float bar_height =
      layout.geometry.cast_bar_height / scale;

  (void)MethodCall(lua_, plate_index, "SetWidth").Number(frame_width).Invoke();
  (void)MethodCall(lua_, plate_index, "SetHeight").Number(frame_height).Invoke();

  const auto size = [&](const int index, const float width,
                        const float height) {
    (void)MethodCall(lua_, index, "SetWidth").Number(width).Invoke();
    (void)MethodCall(lua_, index, "SetHeight").Number(height).Invoke();
  };
  const auto point = [&](const int index, const char* self_point,
                         const int relative_index, const char* relative_point,
                         const float x, const float y) {
    (void)MethodCall(lua_, index, "SetPoint")
        .String(self_point)
        .Object(relative_index)
        .String(relative_point)
        .Number(x)
        .Number(y)
        .Invoke();
  };

  if (const int threat = PushRegion(lua_, plate_index, kKeyThreatFlash);
      threat != 0) {
    size(threat, kThreatStoredWidth * unit, kThreatStoredHeight * unit);
    point(threat, "TOP", plate_index, "TOP", kThreatStoredOffsetX * unit,
          kThreatStoredOffsetY * unit);
    lua_settop(lua_, plate_index);
  }

  if (const int health = PushRegion(lua_, plate_index, kKeyHealthBar);
      health != 0) {
    point(health, "BOTTOMLEFT", plate_index, "BOTTOMLEFT", bar_inset_x,
          bar_offset_y);
    size(health, bar_width, bar_height);
    lua_settop(lua_, plate_index);
  }

  int cast_border = PushRegion(lua_, plate_index, kKeyCastBorder);
  if (cast_border != 0) {
    size(cast_border, frame_width, frame_height);
    point(cast_border, "CENTER", plate_index, "CENTER", 0.0f,
          -0.5f * frame_height);
  }

  if (const int shield = PushRegion(lua_, plate_index, kKeyCastShield);
      shield != 0 && cast_border != 0) {
    size(shield, frame_width, frame_height);
    point(shield, "CENTER", cast_border, "CENTER", 0.0f, -0.5f * frame_height);
    lua_settop(lua_, cast_border);
  }

  if (const int cast = PushRegion(lua_, plate_index, kKeyCastBar);
      cast != 0 && cast_border != 0) {
    point(cast, "BOTTOMRIGHT", cast_border, "BOTTOMRIGHT", -bar_inset_x,
          bar_offset_y);
    size(cast, bar_width, bar_height);
    lua_settop(lua_, cast_border);
  }

  if (const int icon = PushRegion(lua_, plate_index, kKeyCastIcon);
      icon != 0 && cast_border != 0) {
    size(icon, kCastIconStoredSize * unit, kCastIconStoredSize * unit);
    point(icon, "CENTER", cast_border, "BOTTOMLEFT",
          frame_width * kCastIconAnchorXRatio,
          frame_height * kCastIconAnchorYRatio);
    lua_settop(lua_, cast_border);
  }
  if (cast_border != 0) {
    lua_settop(lua_, plate_index);
    cast_border = 0;
  }

  lua_getglobal(lua_, "NAMEPLATE_FONT");
  const char* nameplate_font = lua_tostring(lua_, -1);
  const std::string font_path = nameplate_font != nullptr ? nameplate_font : "";
  lua_pop(lua_, 1);

  if (const int name = PushRegion(lua_, plate_index, kKeyName); name != 0) {
    if (!font_path.empty()) {
      (void)MethodCall(lua_, name, "SetFont")
          .String(font_path.c_str())
          .Number(kNameFontStoredHeight * unit)
          .Invoke();
    }
    (void)MethodCall(lua_, name, "SetShadowColor")
        .Number(0.0)
        .Number(0.0)
        .Number(0.0)
        .Number(1.0)
        .Invoke();
    (void)MethodCall(lua_, name, "SetShadowOffset")
        .Number(kNameShadowStoredOffsetX * unit)
        .Number(kNameShadowStoredOffsetY * unit)
        .Invoke();
    point(name, "BOTTOM", plate_index, "CENTER", 0.0f, 0.0f);
    lua_settop(lua_, plate_index);
  }

  int level = PushRegion(lua_, plate_index, kKeyLevel);
  if (level != 0) {
    if (!font_path.empty()) {
      (void)MethodCall(lua_, level, "SetFont")
          .String(font_path.c_str())
          .Number(kLevelFontStoredHeight * unit)
          .Invoke();
    }
    (void)MethodCall(lua_, level, "SetShadowColor")
        .Number(0.0)
        .Number(0.0)
        .Number(0.0)
        .Number(1.0)
        .Invoke();
    (void)MethodCall(lua_, level, "SetShadowOffset")
        .Number(kNameShadowStoredOffsetX * unit)
        .Number(kNameShadowStoredOffsetY * unit)
        .Invoke();
    point(level, "CENTER", plate_index, "BOTTOMRIGHT",
          -frame_width * kCastIconAnchorXRatio,
          frame_height * kCastIconAnchorYRatio);
  }

  if (const int skull = PushRegion(lua_, plate_index, kKeySkull);
      skull != 0 && level != 0) {
    size(skull, kSkullStoredSize * unit, kSkullStoredSize * unit);
    point(skull, "CENTER", level, "CENTER", 0.0f, 0.0f);
    lua_settop(lua_, level);
  }
  if (const int elite = PushRegion(lua_, plate_index, kKeyElite);
      elite != 0 && level != 0) {
    size(elite, kEliteStoredWidth * unit, kEliteStoredHeight * unit);
    point(elite, "CENTER", level, "CENTER", kEliteStoredOffsetX * unit,
          kEliteStoredOffsetY * unit);
    lua_settop(lua_, level);
  }
  if (level != 0) {
    lua_settop(lua_, plate_index);
    level = 0;
  }

  if (const int border = PushRegion(lua_, plate_index, kKeyBorder);
      border != 0) {
    if (const int raid = PushRegion(lua_, plate_index, kKeyRaidIcon);
        raid != 0) {
      size(raid, kRaidTargetIconStoredSize * unit,
           kRaidTargetIconStoredSize * unit);
      point(raid, "RIGHT", border, "LEFT", 0.0f, 0.0f);
    }
    lua_settop(lua_, plate_index);
  }
}

void NameplateFrameManager::ApplyPlacement(
    PlateState& plate, const int plate_index,
    const NameplateScreenPlacement& placement,
    const NameplateScreenLayout& layout) {
  const auto& info = placement.info;
  const float scale = layout.ui_pixel_scale > 0.0f ? layout.ui_pixel_scale : 1.0f;

  (void)MethodCall(lua_, plate_index, "SetPoint")
      .String("TOP")
      .String(kWorldFrameKey)
      .String("TOPLEFT")
      .Number(placement.screen_x / scale)
      .Number(-placement.screen_y / scale)
      .Invoke();

  if (plate.frame_level != static_cast<int>(placement.frame_level)) {
    plate.frame_level = static_cast<int>(placement.frame_level);
    (void)MethodCall(lua_, plate_index, "SetFrameLevel")
        .Number(plate.frame_level)
        .Invoke();
  }
  const float alpha = static_cast<float>(info.frame_alpha) / 255.0f;
  if (std::fabs(plate.alpha - alpha) > 1.0f / 512.0f) {
    plate.alpha = alpha;
    (void)MethodCall(lua_, plate_index, "SetAlpha").Number(alpha).Invoke();
  }

  const bool unit_changed = plate.guid != info.guid;
  plate.guid = info.guid;

  if (const int health = PushRegion(lua_, plate_index, kKeyHealthBar);
      health != 0) {
    if (unit_changed || std::fabs(plate.health_pct - info.health_pct) > 1e-4f) {
      plate.health_pct = info.health_pct;
      (void)MethodCall(lua_, health, "SetValue")
          .Number(std::clamp(info.health_pct, 0.0f, 1.0f))
          .Invoke();
    }
    if (unit_changed || plate.bar_color_argb != info.health_bar_color_argb) {
      plate.bar_color_argb = info.health_bar_color_argb;
      (void)MethodCall(lua_, health, "SetStatusBarColor")
          .Number(ColorComponent(info.health_bar_color_argb, 16u))
          .Number(ColorComponent(info.health_bar_color_argb, 8u))
          .Number(ColorComponent(info.health_bar_color_argb, 0u))
          .Invoke();
    }
    lua_settop(lua_, plate_index);
  }

  if (const int name = PushRegion(lua_, plate_index, kKeyName); name != 0) {
    if (unit_changed || plate.name != info.name) {
      plate.name = info.name;
      (void)MethodCall(lua_, name, "SetText").String(info.name.c_str()).Invoke();
    }
    if (unit_changed || plate.name_color_argb != info.name_color_argb) {
      plate.name_color_argb = info.name_color_argb;
      (void)MethodCall(lua_, name, "SetTextColor")
          .Number(ColorComponent(info.name_color_argb, 16u))
          .Number(ColorComponent(info.name_color_argb, 8u))
          .Number(ColorComponent(info.name_color_argb, 0u))
          .Invoke();
    }
    lua_settop(lua_, plate_index);
  }

  const bool show_level = info.show_level && info.level != 0u;
  const int level_value = show_level ? static_cast<int>(info.level) : 0;
  if (const int level = PushRegion(lua_, plate_index, kKeyLevel); level != 0) {
    if (plate.show_level != show_level) {
      plate.show_level = show_level;
      CallShow(lua_, level, show_level);
    }
    if (show_level &&
        (unit_changed || plate.level_text != level_value ||
         plate.level_color_argb != info.level_color_argb)) {
      plate.level_text = level_value;
      plate.level_color_argb = info.level_color_argb;
      (void)MethodCall(lua_, level, "SetText")
          .String(std::to_string(level_value).c_str())
          .Invoke();
      (void)MethodCall(lua_, level, "SetTextColor")
          .Number(ColorComponent(info.level_color_argb, 16u))
          .Number(ColorComponent(info.level_color_argb, 8u))
          .Number(ColorComponent(info.level_color_argb, 0u))
          .Invoke();
    }
    lua_settop(lua_, plate_index);
  }

  const auto toggle = [&](const char* key, bool& cached, const bool shown) {
    if (cached == shown) {
      return;
    }
    cached = shown;
    if (const int index = PushRegion(lua_, plate_index, key); index != 0) {
      CallShow(lua_, index, shown);
      lua_settop(lua_, plate_index);
    }
  };

  toggle(kKeySkull, plate.show_skull, info.show_skull && !show_level);
  toggle(kKeyElite, plate.show_elite, info.show_elite);
  toggle(kKeyGlow, plate.show_glow, info.is_mouseover);

  const bool show_threat = info.show_threat;
  if (const int threat = PushRegion(lua_, plate_index, kKeyThreatFlash);
      threat != 0) {
    if (plate.show_threat != show_threat) {
      plate.show_threat = show_threat;
      CallShow(lua_, threat, show_threat);
    }
    if (show_threat && plate.threat_color_argb != info.threat_color_argb) {
      plate.threat_color_argb = info.threat_color_argb;
      (void)MethodCall(lua_, threat, "SetVertexColor")
          .Number(ColorComponent(info.threat_color_argb, 16u))
          .Number(ColorComponent(info.threat_color_argb, 8u))
          .Number(ColorComponent(info.threat_color_argb, 0u))
          .Invoke();
    }
    lua_settop(lua_, plate_index);
  }

  const bool show_raid_icon =
      info.raid_target_icon_index <= kMaxRaidTargetIconIndex;
  if (const int raid = PushRegion(lua_, plate_index, kKeyRaidIcon); raid != 0) {
    const int icon_index =
        show_raid_icon ? static_cast<int>(info.raid_target_icon_index) : -1;
    if (plate.raid_target_icon_index != icon_index) {
      plate.raid_target_icon_index = icon_index;
      CallShow(lua_, raid, show_raid_icon);
      if (show_raid_icon) {
        const float u0 = static_cast<float>(icon_index & 3) *
                         kRaidTargetAtlasTileSize;
        const float v0 = static_cast<float>(icon_index >> 2) *
                         kRaidTargetAtlasTileSize;
        (void)MethodCall(lua_, raid, "SetTexCoord")
            .Number(u0)
            .Number(u0 + kRaidTargetAtlasTileSize)
            .Number(v0)
            .Number(v0 + kRaidTargetAtlasTileSize)
            .Invoke();
      }
    }
    if (show_raid_icon) {

      const float distance = placement.camera_distance;
      float icon_alpha = 1.0f;
      if (std::isfinite(distance) && distance < kRaidIconFadeFarDistance) {
        icon_alpha =
            distance <= kRaidIconFadeNearDistance
                ? kRaidIconMinimumAlpha
                : std::min(1.0f,
                           ((distance - kRaidIconFadeNearDistance) *
                                kRaidIconAlphaPerYard +
                            kRaidIconMinimumAlpha * 255.0f) /
                               255.0f);
      }
      if (std::fabs(plate.raid_icon_alpha - icon_alpha) > 1.0f / 512.0f) {
        plate.raid_icon_alpha = icon_alpha;
        (void)MethodCall(lua_, raid, "SetAlpha").Number(icon_alpha).Invoke();
      }
    }
    lua_settop(lua_, plate_index);
  }

  const bool show_cast = info.cast_pct >= 0.0f && info.cast_alpha > 0.0f;
  if (plate.show_cast != show_cast) {
    plate.show_cast = show_cast;
    for (const char* key : {kKeyCastBorder, kKeyCastBar, kKeyCastIcon}) {
      if (const int index = PushRegion(lua_, plate_index, key); index != 0) {
        CallShow(lua_, index, show_cast);
        lua_settop(lua_, plate_index);
      }
    }
  }

  const bool show_shield = show_cast && info.cast_not_interruptible;
  if (plate.show_shield != show_shield) {
    plate.show_shield = show_shield;
    if (const int shield = PushRegion(lua_, plate_index, kKeyCastShield);
        shield != 0) {
      CallShow(lua_, shield, show_shield);
      lua_settop(lua_, plate_index);
    }
  }
  if (show_cast) {
    if (const int cast = PushRegion(lua_, plate_index, kKeyCastBar);
        cast != 0) {
      if (std::fabs(plate.cast_pct - info.cast_pct) > 1e-4f) {
        plate.cast_pct = info.cast_pct;
        (void)MethodCall(lua_, cast, "SetValue")
            .Number(std::clamp(info.cast_pct, 0.0f, 1.0f))
            .Invoke();
      }
      if (plate.cast_color_argb != info.cast_color_argb) {
        plate.cast_color_argb = info.cast_color_argb;
        (void)MethodCall(lua_, cast, "SetStatusBarColor")
            .Number(ColorComponent(info.cast_color_argb, 16u))
            .Number(ColorComponent(info.cast_color_argb, 8u))
            .Number(ColorComponent(info.cast_color_argb, 0u))
            .Invoke();
      }
      lua_settop(lua_, plate_index);
    }
    if (plate.cast_icon_texture != info.cast_icon_texture) {
      plate.cast_icon_texture = info.cast_icon_texture;
      if (const int icon = PushRegion(lua_, plate_index, kKeyCastIcon);
          icon != 0) {
        (void)MethodCall(lua_, icon, "SetTexture")
            .String(info.cast_icon_texture.c_str())
            .Invoke();
        lua_settop(lua_, plate_index);
      }
    }
  }

  if (!plate.shown) {
    plate.shown = true;
    CallShow(lua_, plate_index, true);
  }
}

void NameplateFrameManager::HidePlate(PlateState& plate) {
  if (!plate.shown || plate.lua_ref == LUA_NOREF) {
    return;
  }
  plate.shown = false;
  plate.guid = 0;
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, plate.lua_ref);
  const int plate_index = lua_gettop(lua_);
  if (lua_istable(lua_, plate_index) != 0) {
    CallShow(lua_, plate_index, false);
  }
  lua_settop(lua_, plate_index - 1);
}

void NameplateFrameManager::Update() {
  const auto lease = NameplateFrameChannel::Get().AcquireLayout();
  const auto& layout = *lease;

  NameplateFrameChannel::Evidence evidence;
  evidence.generation = layout.generation;

  if (lua_ == nullptr) {
    NameplateFrameChannel::Get().RecordWidgetUpdate(evidence);
    return;
  }
  if (!frames_.FindLuaRef(kWorldFrameKey).has_value()) {

    if (!world_frame_missing_logged_ && !layout.plates.empty()) {
      world_frame_missing_logged_ = true;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "NameplateFrameManager: WorldFrame is not materialized; nameplates "
          "cannot be parented");
    }
    for (auto& plate : plates_) {
      HidePlate(plate);
    }
    active_plates_ = 0;
    NameplateFrameChannel::Get().RecordWidgetUpdate(evidence);
    return;
  }

  constexpr int kPlateUpdateStackHeadroom = 24;
  if (lua_checkstack(lua_, kPlateUpdateStackHeadroom) == 0) {
    NameplateFrameChannel::Get().RecordWidgetUpdate(evidence);
    return;
  }
  const int base = lua_gettop(lua_);
  std::size_t applied = 0;
  for (std::size_t index = 0; index < layout.plates.size(); ++index) {
    if (!EnsurePlate(index)) {
      break;
    }
    auto& plate = plates_[index];
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, plate.lua_ref);
    const int plate_index = lua_gettop(lua_);
    if (lua_istable(lua_, plate_index) != 0) {
      ApplyGeometry(plate, plate_index, layout);
      ApplyPlacement(plate, plate_index, layout.plates[index], layout);
      ++applied;
      if (!layout.plates[index].info.name.empty()) {
        ++evidence.named_plates;
      }
    }
    lua_settop(lua_, base);
  }
  for (std::size_t index = applied; index < plates_.size(); ++index) {
    HidePlate(plates_[index]);
  }
  lua_settop(lua_, base);

  active_plates_ = applied;
  evidence.plates = applied;
  evidence.widgets_ready = applied == layout.plates.size();
  NameplateFrameChannel::Get().RecordWidgetUpdate(evidence);
}

}
