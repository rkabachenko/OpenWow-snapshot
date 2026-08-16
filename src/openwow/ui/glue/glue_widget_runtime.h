#pragma once

#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/widgets/status_bar_value_state.h"
#include "openwow/vfs/virtual_file_system.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "openwow/ui/glue/glue_model_ffx_widget.h"

namespace openwow::ui {
class TextureNaturalSizeSource;
}

namespace openwow::ui::glue {

struct ModelLightEntry {
  bool enabled{false};
  bool omni{true};
  float dir_x{0.0f};
  float dir_y{0.0f};
  float dir_z{0.0f};
  float amb_r{0.0f};
  float amb_g{0.0f};
  float amb_b{0.0f};
  float dir_r{0.0f};
  float dir_g{0.0f};
  float dir_b{0.0f};
};

enum class ModelLightCategory : std::uint8_t {
  kGeneral = 0,
  kCharacter = 1,
  kPet = 2,
};

enum class GlueButtonFontState : std::uint8_t {
  kNormal = 0,
  kDisabled = 1,
  kHighlight = 2,
};

static constexpr int kMaxLightsPerSlot = 4;

struct PreparedFrameGroup {
  std::string top_level_name;
  bool is_virtual{false};
  std::vector<openwow::ui::framexml::UiFrame> frames;
};

struct PreparedXmlResult {
  bool ok{false};
  std::string error;
  std::vector<PreparedFrameGroup> groups;
};

enum class GlueTemplateValidation {
  kFound,
  kMissing,
  kRecursive,
};

struct GlueWidgetState {
  std::string name;

  std::string lua_name;
  std::string kind;
  std::string parent;
  std::string inherits;
  int id{0};
  bool password{false};
  int max_letters{-1};
  bool auto_focus{true};
  std::string texture_file;
  std::string model_file;
  std::string font_style;
  std::string justify_h;
  std::string justify_v;
  std::string alpha_mode;
  std::string draw_layer;
  int draw_sublevel{0};
  std::string frame_strata;
  int frame_level{0};
  float depth{0.0F};

  bool ignore_depth{false};
  bool protected_frame{false};

  bool clamped_to_screen{false};
  bool movable{false};
  bool resizable{false};
  bool toplevel{false};
  bool user_placed{false};
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  float animation_rotation_radians{0.0F};
  float alpha{1.0F};
  std::uint8_t alpha_byte{0xFF};

  float tex_left{0.0F};
  float tex_right{1.0F};
  float tex_top{0.0F};
  float tex_bottom{1.0F};
  openwow::ui::framexml::UiTextureCoordQuad tex_coords{};
  float color_r{1.0F};
  float color_g{1.0F};
  float color_b{1.0F};
  float color_a{1.0F};
  bool has_vertex_color{false};
  openwow::ui::framexml::TextureGradient gradient;
  std::string button_normal_font_style;
  std::string button_disabled_font_style;
  std::string button_highlight_font_style;
  std::optional<openwow::ui::framexml::UiColor> button_normal_color;
  std::optional<openwow::ui::framexml::UiColor> button_disabled_color;
  std::optional<openwow::ui::framexml::UiColor> button_highlight_color;
  std::optional<openwow::ui::widgets::StatusBarDefinition> status_bar;
  std::string color_wheel_texture_file;
  std::string color_wheel_thumb_texture_file;
  std::string color_value_texture_file;
  std::string color_value_thumb_texture_file;
  float fog_r{0.0F};
  float fog_g{0.0F};
  float fog_b{0.0F};
  bool has_fog_color{false};
  float fog_near{0.0F};
  bool has_fog_near{false};
  float fog_far{0.0F};
  bool has_fog_far{false};
  float glow{0.0F};
  bool has_glow{false};
  float model_scale{1.0F};
  bool has_model_scale{false};
  int model_sequence{0};
  bool has_model_sequence{false};
  int model_camera{0};
  bool has_model_camera{false};
  std::uint32_t model_sequence_time_ms{0};
  bool has_model_sequence_time{false};
  float model_x{0.0F};
  float model_y{0.0F};
  float model_z{0.0F};
  bool has_model_position{false};
  float model_facing_rad{0.0F};
  bool has_model_facing{false};
  bool tile_x{false};
  bool tile_y{false};
  int tile_size_x{0};
  int tile_size_y{0};
  openwow::ui::framexml::TextureSlice slice{openwow::ui::framexml::TextureSlice::kNone};
  int slice_edge_size_px{0};
  bool enabled{true};
  bool mouse_enabled{false};
  std::string text;
  float text_spacing_stored{0.0f};
  float text_height_stored{0.0f};
  int max_lines{0};
  float shadow_r{0.0F};
  float shadow_g{0.0F};
  float shadow_b{0.0F};
  float shadow_a{1.0F};
  float shadow_x{0.0F};
  float shadow_y{0.0F};
  bool has_shadow_color{false};
  bool has_shadow_offset{false};
  bool visible{true};
  bool virtual_template{false};
  bool scroll_child_content{false};
  float hit_rect_inset_left{0.0F};
  float hit_rect_inset_right{0.0F};
  float hit_rect_inset_top{0.0F};
  float hit_rect_inset_bottom{0.0F};
  bool has_hit_rect_insets{false};

  float text_inset_left{0.0F};
  float text_inset_right{0.0F};
  float text_inset_top{0.0F};
  float text_inset_bottom{0.0F};
  bool has_text_insets{false};

  bool word_wrap{true};
  bool non_space_wrap{false};
  bool indented_word_wrap{false};

  std::optional<openwow::ui::framexml::detail::BackdropSpec> backdrop;

  bool publish_to_lua{true};
  [[nodiscard]] std::string_view LuaName() const noexcept {
    if (!publish_to_lua) {
      return {};
    }
    return lua_name.empty() ? std::string_view{name} : std::string_view{lua_name};
  }
  openwow::ui::framexml::UiFrame::RegionRole region_role{
      openwow::ui::framexml::UiFrame::RegionRole::Normal};
};

struct GlueWidgetClipRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

struct GlueWidgetPresentation {
  GlueWidgetState widget;
  int offset_x{0};
  int offset_y{0};
  std::optional<GlueWidgetClipRect> clip;
  bool clipped_out{false};
};

std::string GlueEditBoxTextRegionKey(std::string_view owner_name);

struct GlueTextExtent {
  float width{0.0F};
  float height{0.0F};
};

struct GlueEditBoxInsertion {
  bool accepted{false};
  std::string text;
  int cursor_byte{0};
};

class GlueWidgetRuntime {
 public:
  GlueWidgetRuntime() = default;

  void BindTextureNaturalSizeSource(
      openwow::ui::TextureNaturalSizeSource* source) noexcept {
    texture_natural_size_source_ = source;
  }
  [[nodiscard]] openwow::ui::TextureNaturalSizeSource*
  texture_natural_size_source() const noexcept {
    return texture_natural_size_source_;
  }
  using XmlTextResolver =
      std::function<std::optional<std::string>(std::string_view)>;
  using FocusOwnerChangedCallback = std::function<void()>;

  struct InteractionPerformanceCounters {
    std::uint64_t hit_test_index_rebuilds{0};
    std::size_t hit_test_index_entries{0};
    std::size_t last_hit_test_candidates{0};
  };

  [[nodiscard]] const InteractionPerformanceCounters&
  interaction_performance_counters() const noexcept {
    return interaction_performance_counters_;
  }
  void ResetInteractionPerformanceCounters() const noexcept {
    interaction_performance_counters_ = {};
  }

  void RegisterWidget(GlueWidgetState widget);

  std::string AllocateUniqueWidgetKey(std::string_view preferred_name);
  std::string RegisterAnonymousWidget(GlueWidgetState widget);

  void SetXmlTextResolver(XmlTextResolver resolver);
  static bool IsAnonymousWidgetKey(std::string_view name);
  void LoadWidgetsFromXml(const openwow::vfs::VirtualFileSystem& vfs,
                          const std::vector<std::string>& xml_candidates);

  void ClearAll();

  PreparedXmlResult PrepareXml(const openwow::vfs::VirtualFileSystem& vfs,
                               const std::string& xml_text);

  struct RegisteredFrameGroup {
    std::vector<std::string> registered;
    std::vector<std::string> newly_created;
  };

  RegisteredFrameGroup RegisterFrameGroup(
      std::vector<openwow::ui::framexml::UiFrame> frames,
      const openwow::vfs::VirtualFileSystem* vfs = nullptr);
  bool HasTemplate(const std::string& template_root) const;
  GlueTemplateValidation ValidateTemplateChain(const std::string& template_root) const;

  std::vector<std::string> InstantiateTemplate(
      const std::string& instance_root, const std::string& template_root);
  void ResolveLayout(int viewport_width, int viewport_height);
  void SetViewport(int viewport_width, int viewport_height);
  int viewport_width() const;
  int viewport_height() const;

  void SetGlobalTransitionFactor(float t);
  float GlobalTransitionFactor() const;

  void SetGlobalTransitionOverlayVisible(bool visible);
  bool GlobalTransitionOverlayVisible() const;

  float ui_scale() const { return ui_scale_; }

  float root_scale() const { return root_scale_; }

  float ndc_to_pixel() const { return ndc_to_pixel_; }

  float kx() const { return kx_; }

  float conversion_factor() const { return conversion_factor_; }
  bool IsLayoutDirty() const { return layout_dirty_; }

  std::uint64_t layout_resolve_count() const { return layout_resolve_count_; }

  bool IsVisibleRenderOrderDirty() const { return visible_widgets_cache_dirty_; }

  std::uint64_t visible_widget_order_revision() const {
    return visible_widget_order_revision_;
  }
  bool IsModelFFXViewportDirty() const { return model_ffx_viewport_dirty_; }
  bool ConsumeModelFFXViewportDirty();

  void Show(const std::string& name);
  void Hide(const std::string& name);
  bool IsVisible(const std::string& name) const;

  bool IsVisibleIgnoringLifecycleOverride(const std::string& name) const;
  void SetLifecycleVisibilityOverride(const std::string& name, bool visible);
  void ClearLifecycleVisibilityOverride(const std::string& name);
  void ClearLifecycleVisibilityOverrides();

  std::vector<std::string> VisibilitySubtreeNames(const std::string& name) const;

  std::vector<std::string> ShownDescendantNames(const std::string& name) const;

  std::uint64_t visibility_revision() const { return visibility_revision_; }

  bool IsShown(const std::string& name) const;
  bool IsVirtualTemplate(const std::string& name) const;
  void SetId(const std::string& name, int id);
  int GetId(const std::string& name) const;
  int GetFrameLevel(const std::string& name) const;
  std::string GetButtonState(const std::string& name) const;
  void SetButtonState(const std::string& name, const std::string& state);
  void SetDisabledTextColor(
      const std::string& name,
      const openwow::ui::framexml::UiColor& color);
  void Raise(const std::string& name);
  void SetClampedToScreen(const std::string& name, bool clamped);
  bool IsClampedToScreen(const std::string& name) const;
  void SetMovable(const std::string& name, bool movable);
  bool IsMovable(const std::string& name) const;
  void SetResizable(const std::string& name, bool resizable);
  bool IsResizable(const std::string& name) const;
  void SetToplevel(const std::string& name, bool toplevel);
  bool IsToplevel(const std::string& name) const;
  void SetUserPlaced(const std::string& name, bool user_placed);
  bool IsUserPlaced(const std::string& name) const;
  void SetMouseWheelEnabled(const std::string& name, bool enabled);
  bool IsMouseWheelEnabled(const std::string& name) const;
  void SetInputCategoryMask(const std::string& name, std::uint32_t mask, bool enabled);
  bool HasInputCategoryMask(const std::string& name, std::uint32_t mask) const;
  void SetMouseEnabled(const std::string& name, bool enabled);
  bool IsMouseEnabled(const std::string& name) const;
  void SetKeyboardEnabled(const std::string& name, bool enabled);
  bool IsKeyboardEnabled(const std::string& name) const;
  void SetJoystickEnabled(const std::string& name, bool enabled);
  bool IsJoystickEnabled(const std::string& name) const;
  float GetScale(const std::string& name) const;
  float GetEffectiveScale(const std::string& name) const;
  void SetAnimationTranslation(const std::string& name, float x_pixels, float y_pixels);
  void SetAnimationTransform(const std::string& name, float x_pixels, float y_pixels,
                             float scale_x, float scale_y, float rotation_radians,
                             std::optional<float> alpha, float alpha_change);
  void SetScale(const std::string& name, float scale);
  void SetDepth(const std::string& name, float depth);
  void SetEffectiveDepth(const std::string& name, float effective_depth);
  float GetDepth(const std::string& name) const;
  double GetEffectiveDepth(const std::string& name) const;
  void SetIgnoreDepth(const std::string& name, bool ignore_depth);
  bool IsIgnoringDepth(const std::string& name) const;
  void RegisterForClicks(const std::string& name, std::uint64_t click_mask);
  bool IsClickRegistered(const std::string& name, std::uint32_t button_flag,
                         bool is_down) const;
  void LockHighlight(const std::string& name, bool locked);
  bool HighlightLocked(const std::string& name) const;
  void SetHovered(const std::string& name, bool hovered);
  bool Hovered(const std::string& name) const;
  void SetChecked(const std::string& name, bool checked);
  bool Checked(const std::string& name) const;
  std::pair<double, double> GetMinMaxValues(const std::string& name) const;
  void SetMinMaxValues(const std::string& name, double min_value,
                       double max_value,
                       bool requantize_current_value = true);
  bool HasSliderRange(const std::string& name) const;
  double GetValue(const std::string& name) const;
  bool HasSliderValue(const std::string& name) const;
  void SetValue(const std::string& name, double value);
  [[nodiscard]] bool IsStatusBar(const std::string& name) const;
  [[nodiscard]] openwow::ui::widgets::StatusBarValueSnapshot
  GetStatusBarValueSnapshot(const std::string& name) const;
  [[nodiscard]] openwow::ui::widgets::StatusBarRangeChange SetStatusBarRange(
      const std::string& name, float minimum, float maximum);
  [[nodiscard]] bool SetStatusBarValue(const std::string& name, float value);
  double GetValueStep(const std::string& name) const;
  void SetValueStep(const std::string& name, double value_step);
  double GetVerticalScroll(const std::string& name) const;
  void SetVerticalScroll(const std::string& name, double offset);
  double GetVerticalScrollRange(const std::string& name) const;
  void SetVerticalScrollRange(const std::string& name, double range);
  double GetHorizontalScroll(const std::string& name) const;
  void SetHorizontalScroll(const std::string& name, double offset);
  double GetHorizontalScrollRange(const std::string& name) const;
  void SetHorizontalScrollRange(const std::string& name, double range);
  void SetMaxBytes(const std::string& name, int max_bytes);
  int GetMaxBytes(const std::string& name) const;
  void SetMaxLetters(const std::string& name, int max_letters);
  int GetMaxLetters(const std::string& name) const;
  void SetModel(const std::string& name, const std::string& model_file);

  [[nodiscard]] std::shared_ptr<GlueModelFFXWidget> ResolveModelFFXWidget(const std::string& name) const;
  void SetSequence(const std::string& name, int sequence);
  void SetSequenceTime(const std::string& name, int sequence, std::uint32_t time_ms);
  int GetSequence(const std::string& name) const;
  std::uint64_t GetSequenceRevision(const std::string& name) const;
  void SetCamera(const std::string& name, int camera_index);
  int GetCamera(const std::string& name) const;
  std::optional<std::uint32_t> ConsumeSequenceTimeMs(const std::string& name);
  void SetModelScale(const std::string& name, float scale);
  float GetModelScale(const std::string& name) const;
  void SetModelPosition(const std::string& name, float x, float y, float z);
  void GetModelPosition(const std::string& name, float& x, float& y, float& z) const;
  void SetFacing(const std::string& name, float facing_rad);
  float GetFacing(const std::string& name) const;
  void SetDesaturated(const std::string& name, bool desaturated);
  bool Desaturated(const std::string& name) const;
  void SetFogColor(const std::string& name, float r, float g, float b);
  void SetFogNear(const std::string& name, float near_v);
  void SetFogFar(const std::string& name, float far_v);
  void GetFogColor(const std::string& name, float& r, float& g, float& b) const;
  float GetFogNear(const std::string& name) const;
  float GetFogFar(const std::string& name) const;

  void ClearFog(const std::string& name);
  bool IsFogEnabled(const std::string& name) const;

  void SetGlow(const std::string& name, float glow);
  float GetGlow(const std::string& name) const;

  void ResetLights(const std::string& name);

  bool AddModelLight(const std::string& name, ModelLightCategory category,
                     int light_type, const ModelLightEntry& entry);

  const std::vector<ModelLightEntry>& GetModelLights(
      const std::string& name, ModelLightCategory category, int light_type) const;

  void SetPoint(const std::string& name,
                const std::string& point,
                const std::string& relative_to,
                const std::string& relative_point,
                float x,
                float y);
  void SetAllPoints(const std::string& name, const std::string& relative_to);
  void ClearAllPoints(const std::string& name);
  void SetSize(const std::string& name, float width, float height);
  void SetWidth(const std::string& name, float width);
  void SetHeight(const std::string& name, float height);
  bool SetFontStringIntrinsicSize(const std::string& name,
                                  float width,
                                  float height,
                                  bool width_intrinsic,
                                  bool height_intrinsic);
  bool ClearFontStringIntrinsicSize(const std::string& name);

  std::vector<std::string> ConsumeDirtyFontStringMetricNames();
  [[nodiscard]] bool HasDirtyFontStringMetrics() const {
    return !dirty_font_string_metric_names_.empty();
  }
  [[nodiscard]] std::optional<GlueTextExtent> GetCachedTextExtent(
      const std::string& name, std::uint64_t request_key) const;
  [[nodiscard]] std::optional<GlueTextExtent> GetCachedIntrinsicTextExtent(
      const std::string& name) const;
  void CacheTextExtent(const std::string& name,
                       std::uint64_t request_key,
                       GlueTextExtent extent,
                       bool intrinsic_layout,
                       bool laid_out_now);
  [[nodiscard]] std::uint64_t TextLayoutCount(
      const std::string& name) const;
  void SetParent(const std::string& name, const std::string& parent);
  void SetFrameLevel(const std::string& name, int level);
  void SetFrameStrata(const std::string& name, const std::string& strata);
  int GetNumPoints(const std::string& name) const;
  std::optional<openwow::ui::framexml::UiAnchor> GetPoint(const std::string& name, int index) const;
  void SetAlpha(const std::string& name, float alpha);
  float EffectiveAlpha(const std::string& name) const;
  void SetTexture(const std::string& name, const std::string& texture_file);
  void SetTexCoord(const std::string& name, float left, float right, float top, float bottom);
  void SetTexCoordQuad(const std::string& name,
                       const openwow::ui::framexml::UiTextureCoordQuad& tex_coords);
  void SetVertexColor(const std::string& name, float r, float g, float b, float a);
  void SetShadowColor(const std::string& name, float r, float g, float b, float a);
  void SetShadowOffset(const std::string& name, float x, float y);

  void SetBackdrop(const std::string& name, const openwow::ui::framexml::detail::BackdropSpec& spec);
  void SetBackdropColor(const std::string& name, float r, float g, float b,
                        float a = 1.0F);
  void SetBackdropBorderColor(const std::string& name, float r, float g,
                              float b, float a = 1.0F);

  void ClearBackdrop(const std::string& name);
  void SetCapabilityAvailable(const std::string& name, bool available);
  [[nodiscard]] bool IsCapabilityAvailable(const std::string& name) const;
  void SetEnabled(const std::string& name, bool enabled);
  bool IsEnabled(const std::string& name) const;
  void SetText(const std::string& name, const std::string& text);
  std::string GetText(const std::string& name) const;
  void SetEditInputLanguageToken(const std::string& name, const std::string& token);
  std::string GetEditInputLanguageToken(const std::string& name) const;
  void SetEditAutoFocus(const std::string& name, bool auto_focus);
  bool IsEditAutoFocus(const std::string& name) const;
  void SetEditCursorByte(const std::string& name, int byte_index);
  int GetEditCursorByte(const std::string& name) const;
  void SetEditSelectionBytes(const std::string& name, int start_byte, int end_byte);
  std::pair<int, int> GetEditSelectionBytes(const std::string& name) const;
  GlueEditBoxInsertion BuildEditBoxInsertion(
      const std::string& name, std::string_view inserted_text) const;
  void ClearEditSelection(const std::string& name);

  int GetEditVisibleStartCodepoints(const std::string& name) const;
  void SetEditVisibleStartCodepoints(const std::string& name, int visible_codepoints);
  int GetEditScrollOffsetPx(const std::string& name) const;
  void SetEditScrollOffsetPx(const std::string& name, int offset);

  void MarkCursorDirty(const std::string& name);
  std::string TextRegionForWidget(const std::string& name) const;
  void SetJustifyH(const std::string& name, const std::string& justify_h);
  void SetJustifyV(const std::string& name, const std::string& justify_v);
  void SetFontStyle(const std::string& name, const std::string& font_style);
  void SetButtonFontStyle(const std::string& name,
                          GlueButtonFontState state,
                          const std::string& font_style);
  std::string GetButtonFontStyle(const std::string& name,
                                 GlueButtonFontState state) const;
  void SetTextSpacing(const std::string& name, float stored_spacing);
  void SetTextHeightStored(const std::string& name, float stored_height);
  void SetMaxLines(const std::string& name, int max_lines);
  void SetWordWrap(const std::string& name, bool enable);
  void SetNonSpaceWrap(const std::string& name, bool enable);
  void SetIndentedWordWrap(const std::string& name, bool enable);
  [[nodiscard]] bool HasWidget(const std::string& name) const;
  std::optional<GlueWidgetState> GetWidget(const std::string& name) const;

  std::string NearestLuaName(const std::string& runtime_key) const;

  std::optional<GlueWidgetState> GetResolvedWidget(const std::string& name) const;

  [[nodiscard]] GlueWidgetPresentation ResolveScrollPresentation(
      const GlueWidgetState& resolved_widget) const;
  bool HasResolvedLayout(const std::string& name) const;
  const openwow::ui::framexml::UiFrame* GetLayoutFrameDefinition(const std::string& name) const;

  std::string TemplateSourceName(const std::string& name) const;
  std::optional<GlueWidgetState> HitTestTopmostVisibleWidget(int x, int y) const;
  std::optional<GlueWidgetState> HitTestTopmostInteractiveWidget(int x, int y) const;

  std::optional<GlueWidgetState> HitTestMouseTarget(int x, int y) const;

  std::string ResolveMouseTargetName(const std::string& name) const;

  [[nodiscard]] bool WidgetHitRectContainsPoint(const std::string& name, int x,
                                                int y) const;

  bool WidgetContainsInputPoint(const std::string& name, int x, int y) const;
  std::vector<std::string> WidgetNames() const;
  std::vector<std::string> WidgetNamesInRegistrationOrder() const;

  std::vector<std::string> WidgetNamesInSourceOrder() const;

  [[nodiscard]] const std::vector<GlueWidgetState>&
  VisibleWidgetsInRenderOrder() const;

  [[nodiscard]] const std::vector<const GlueWidgetState*>&
  VisibleWidgetPointersInRenderOrder() const;

  [[nodiscard]] bool CanFocusEditBox(const std::string& name) const;

  void SetFocusedWidget(const std::string& name);
  [[nodiscard]] const std::string& focused_widget() const noexcept;

  void SetFocusOwnerChangedCallback(FocusOwnerChangedCallback callback);
  void SetCachedCursorPosition(int x, int y);
  std::optional<std::pair<int, int>> cached_cursor_position() const;

  void MarkCursorHitTestRefresh();
  void MarkDeferredHitTestRefresh();
  bool ConsumeDeferredHitTestRefresh(bool* cursor_motion = nullptr);

  void UpdateCaretBlink(const std::string& name, float dt_seconds);

  bool IsCaretVisible(const std::string& name) const;

  bool ConsumeCursorChanged(const std::string& name);

  struct PendingCursorChangedEvent {
    std::string widget_name;
    float x{0}, y{0}, w{0}, h{0};
  };
  void QueueCursorChangedEvent(const std::string& name, float x, float y, float w, float h);

  std::vector<PendingCursorChangedEvent> ConsumeCursorChangedEvents();

  struct PendingAnimationFinishedEvent {
    std::string widget_name;
  };
  void QueueAnimationFinishedEvent(const std::string& name);
  std::vector<PendingAnimationFinishedEvent> ConsumeAnimationFinishedEvents();
  struct PendingScrollRangeChangedEvent {
    std::string widget_name;
    double horizontal_range{0.0};
    double vertical_range{0.0};
  };
  std::vector<PendingScrollRangeChangedEvent>
  ConsumeScrollRangeChangedEvents();

 private:
  openwow::ui::TextureNaturalSizeSource* texture_natural_size_source_{nullptr};

  std::unordered_map<std::string, std::string> texture_natural_size_pending_;
  static int StrataOrder(const std::string& strata);
  void RetryPendingTextureNaturalSizes();
  void SyncTextureNaturalSize(const std::string& name,
                              openwow::ui::framexml::UiFrame& frame);
  void MarkVisibleWidgetsDirty();
  [[nodiscard]] bool ButtonTextureStateAllowsVisible(
      const GlueWidgetState& widget) const;
  void ApplyAnimationTranslation(const GlueWidgetState& widget,
                                 GlueWidgetState* resolved) const;
  void RebuildVisibleWidgetOrderCache() const;
  void RebuildHitTestSpatialCache() const;

  enum class HitTestMode : std::uint8_t {

    kVisible,

    kInteractive,

    kMouseTarget,
  };
  [[nodiscard]] std::optional<GlueWidgetState> HitTestTopmostWidget(
      int x, int y, HitTestMode mode) const;

  struct WidgetScriptProps {
    double min_value{0.0};
    double max_value{0.0};
    double value{0.0};
    double value_step{0.0};
    bool slider_range_set{false};
    bool slider_value_set{false};
    std::optional<openwow::ui::widgets::StatusBarValueState> status_bar;
    double vertical_scroll{0.0};
    double vertical_scroll_range{0.0};
    double horizontal_scroll{0.0};
    double horizontal_scroll_range{0.0};
    int max_bytes{-1};
    int max_letters{-1};
    float scale{1.0F};
    float animation_translation_x{0.0F};
    float animation_translation_y{0.0F};
    float animation_scale_x{1.0F};
    float animation_scale_y{1.0F};
    float animation_rotation_radians{0.0F};
    std::optional<float> animation_alpha;
    float animation_alpha_change{0.0F};
    bool mouse_wheel_enabled{false};
    std::uint32_t input_category_mask{0};
    bool highlight_locked{false};
    bool hovered{false};
    bool checked{false};
    bool desaturated{false};

    std::uint64_t click_mask{1u};
    std::string button_state;
    float fog_r{0.0F};
    float fog_g{0.0F};
    float fog_b{0.0F};
    float fog_near{0.0F};
    float fog_far{0.0F};
    bool fog_enabled{false};
    float glow{0.0F};

    std::vector<ModelLightEntry> general_lights[2];
    std::vector<ModelLightEntry> character_lights[2];
    std::vector<ModelLightEntry> pet_lights[2];
    int model_sequence{0};
    std::uint64_t model_sequence_revision{0};
    int model_camera{0};
    bool model_sequence_time_override{false};
    std::uint32_t model_sequence_time_ms{0};
    float model_scale{1.0F};
    float model_x{0.0F};
    float model_y{0.0F};
    float model_z{0.0F};
    float facing_rad{0.0F};
    std::string edit_input_language{"ROMAN"};
    int edit_cursor_byte{0};
    int edit_sel_start_byte{-1};
    int edit_sel_end_byte{-1};
    int edit_visible_start_codepoints{0};
    int edit_scroll_offset_px{0};

    float blink_accumulator{0.0f};
    bool caret_visible{true};

    bool cursor_changed{false};
  };

  void RefreshAnimatedGeometryRoot(const std::string& name,
                                   const WidgetScriptProps& props);
  void ResolveXmlText(openwow::ui::framexml::UiFrame* frame) const;
  std::vector<openwow::ui::framexml::UiFrame> ExpandFrameDeclarations(
      std::vector<openwow::ui::framexml::UiFrame> frames);
  [[nodiscard]] bool WouldCreateAnchorCycle(
      const std::string& widget_name,
      const std::string& relative_to) const;
  void ApplyExplicitDimensions(
      const std::string& name,
      GlueWidgetState& widget,
      std::optional<float> width,
      std::optional<float> height);

  WidgetScriptProps* GetProps(const std::string& name);
  const WidgetScriptProps* FindProps(const std::string& name) const;
  void ApplyModelWidgetStateToProps(const GlueWidgetState& stored);
  void SyncSimpleHtmlContent(const std::string& name);
  std::string EnsureButtonTextRegion(const std::string& name);
  void MarkModelFFXViewportDirty();
  void MarkFontStringMetricsDirty(const std::string& name);
  void MarkFontStringMetricsDirtyInSubtree(const std::string& name);
  void MarkAllFontStringMetricsDirty();
  void ForgetFontStringMetrics(const std::string& name);
  void QueueScrollRangeChangedEvent(const std::string& name);
  void ReindexVisibilityRelationships(const std::string& name,
                                      const std::string& old_parent,
                                      const std::string& old_inherits);

  struct CachedTextExtent {
    std::uint64_t revision{0};
    std::uint64_t request_key{0};
    GlueTextExtent extent;
    bool valid{false};
  };

  struct FontStringMetricState {
    std::uint64_t revision{0};
    std::uint64_t layout_count{0};
    CachedTextExtent latest;
    CachedTextExtent intrinsic;
  };

  std::vector<std::string> source_widget_order_;
  std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> templates_;
  std::unordered_map<std::string, openwow::ui::framexml::UiFrame> layout_frames_by_name_;
  std::unordered_map<std::string, GlueWidgetState> widgets_;
  std::unordered_set<std::string> widget_names_lower_;
  std::unordered_set<std::string> capability_unavailable_widgets_;
  std::unordered_map<std::string, std::vector<std::string>> children_by_parent_;
  std::unordered_map<std::string, std::vector<std::string>> inheritors_by_template_;
  std::unordered_map<std::string, bool> lifecycle_visibility_overrides_;
  std::unordered_map<std::string, std::string> owned_text_region_by_widget_;
  std::unordered_map<std::string, FontStringMetricState> font_string_metric_state_;
  std::unordered_set<std::string> dirty_font_string_metric_names_set_;
  std::vector<std::string> dirty_font_string_metric_names_;
  std::unordered_set<std::string> resolved_layout_widgets_;
  std::vector<std::string> widget_registration_order_;
  std::unordered_map<std::string, std::shared_ptr<GlueModelFFXWidget>> model_ffx_widgets_;
  std::unordered_map<std::string, WidgetScriptProps> widget_props_;
  std::unordered_map<std::string, std::string> template_source_by_instance_;
  XmlTextResolver xml_text_resolver_;

  std::uint64_t next_anonymous_widget_id_{1};
  int viewport_width_{1280};
  int viewport_height_{720};

  float ui_scale_{1.0f};

  float root_scale_{1.0f};

  float ndc_to_pixel_{1.0f};

  float kx_{1.0f};

  float conversion_factor_{1024.0f};
  float model_ffx_viewport_height_{720.0f};
  bool model_ffx_viewport_dirty_{false};
  bool layout_dirty_{false};
  std::uint64_t layout_resolve_count_{0};
  bool deferred_hit_test_refresh_{false};
  bool deferred_hit_test_refresh_from_cursor_motion_{false};
  std::uint64_t visibility_revision_{0};
  std::uint64_t visible_widget_order_revision_{0};
  mutable bool visible_widgets_cache_dirty_{true};

  mutable std::vector<const GlueWidgetState*> visible_widget_order_cache_;
  struct HitTestSpatialRecord {
    const GlueWidgetState* widget{nullptr};
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
  };
  mutable std::vector<HitTestSpatialRecord> hit_test_spatial_records_;
  mutable std::unordered_map<std::int64_t, std::vector<std::size_t>>
      hit_test_spatial_buckets_;
  mutable std::vector<std::size_t> hit_test_dynamic_records_;
  mutable bool hit_test_spatial_cache_dirty_{true};
  std::unordered_set<std::string> animated_geometry_roots_;
  mutable InteractionPerformanceCounters interaction_performance_counters_{};

  mutable std::vector<GlueWidgetState> resolved_visible_widgets_;
  std::string focused_widget_;
  FocusOwnerChangedCallback focus_owner_changed_callback_;
  std::optional<std::pair<int, int>> cached_cursor_position_;
  std::vector<PendingCursorChangedEvent> pending_cursor_events_;
  std::vector<PendingAnimationFinishedEvent> pending_animation_finished_events_;
  std::vector<PendingScrollRangeChangedEvent>
      pending_scroll_range_changed_events_;

  float global_transition_factor_{1.0f};
  bool global_transition_overlay_visible_{false};
};

}
