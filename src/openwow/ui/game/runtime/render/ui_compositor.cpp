#include "openwow/ui/game/runtime/render/ui_compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <lua.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/formats/m2/model_path.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/world_session.h"
#include "openwow/render/backend/bgfx/bgfx_text_cache.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/models/characters/model_portrait.h"
#include "openwow/render/models/characters/portrait_renderer.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/ui/ui_renderer.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/font_string_justification.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/framexml/backdrop_render_utils.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/game_ui_scale.h"
#include "openwow/ui/game/runtime/frame_input_router.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/runtime/lua_frame_projection.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/render/text_alpha_gradient.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/stateful_widget_render.h"
#include "openwow/ui/game/ui_region_render_policy.h"
#include "openwow/ui/rect_utils.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_paint_order.h"
#include "openwow/ui/widgets/status_bar.h"

namespace openwow::ui::game::runtime::render {

UiCompositor::UiCompositor(Dependencies dependencies) noexcept
    : frame_traversal_index_(dependencies.traversal),
      frame_store_(dependencies.frames),
      retained_layout_(dependencies.layout),
      frame_input_router_(dependencies.input),
      render_resources_(&dependencies.resources),
      texture_manager_(dependencies.textures),
      m2_system_(dependencies.m2),
      world_map_(dependencies.world_map),
      texture_regions_(dependencies.resources),
      text_regions_(dependencies.resources),
      model_regions_(dependencies.resources) {}

float UiCompositor::screen_width() const noexcept {
  return retained_layout_.viewport_width();
}

float UiCompositor::screen_height() const noexcept {
  return retained_layout_.viewport_height();
}

float UiCompositor::root_scale() const noexcept {
  return retained_layout_.root_scale();
}

std::uint8_t UiCompositor::ResolveFrameAlphaByte(
    const FrameStore::FrameHandle handle,
    const openwow::ui::framexml::UiFrame& frame) const {

  const auto mirrored =
      openwow::ui::game::QuantizeFrameAlphaByteTruncated(frame.color_a);
  if (alpha_source_lua_ == nullptr) return mirrored;
  const auto lua_ref = frame_store_.FindLuaRef(handle);
  if (!lua_ref.has_value()) return mirrored;
  const int stack_top = lua_gettop(alpha_source_lua_);
  lua_rawgeti(alpha_source_lua_, LUA_REGISTRYINDEX, *lua_ref);
  auto resolved = mirrored;
  if (lua_istable(alpha_source_lua_, -1) != 0) {
    resolved = openwow::ui::game::lua_projection::GetLuaFrameAlphaByteOrDefault(
        alpha_source_lua_, -1, mirrored);
  }
  lua_settop(alpha_source_lua_, stack_top);
  return resolved;
}

std::uint8_t UiCompositor::ComputeParentAlphaByte(
    const FrameStore::FrameHandle parent_handle) const {

  if (const auto cached = parent_alpha_memo_.find(parent_handle);
      cached != parent_alpha_memo_.end()) {
    return cached->second;
  }

  std::vector<std::pair<FrameStore::FrameHandle, std::uint8_t>> chain;
  FrameStore::FrameHandle current = parent_handle;
  std::uint8_t inherited = 0xffu;
  constexpr int kMaxDepth = 64;
  for (int depth = 0;
       current != FrameStore::kInvalidFrameHandle && depth < kMaxDepth;
       ++depth) {
    if (const auto cached = parent_alpha_memo_.find(current);
        cached != parent_alpha_memo_.end()) {
      inherited = cached->second;
      break;
    }
    const auto* frame = frame_store_.FindFrame(current);
    if (frame == nullptr) break;
    chain.emplace_back(current, ResolveFrameAlphaByte(current, *frame));
    current = frame_store_.ParentHandleOf(current);
  }

  for (auto link = chain.rbegin(); link != chain.rend(); ++link) {
    inherited =
        openwow::ui::game::MultiplyFrameAlphaBytes(link->second, inherited);
    parent_alpha_memo_.emplace(link->first, inherited);
  }
  return inherited;
}

}

namespace openwow::ui::game {
namespace {
class ScopedUiScissor {
 public:
  ScopedUiScissor(openwow::render::ui::UiRenderer* renderer,
                  const std::optional<openwow::ui::UiPaintRect>& clip)
      : renderer_(clip ? renderer : nullptr) {
    if (renderer_)
      renderer_->SetScissor(clip->x, clip->y, clip->width, clip->height);
  }
  ~ScopedUiScissor() {
    if (renderer_) renderer_->ClearScissor();
  }

 private:
  openwow::render::ui::UiRenderer* renderer_;
};

namespace ui_field {

constexpr const char* const kNames[] = {
    "__ow_text",
    "__ow_text_r",
    "__ow_text_g",
    "__ow_text_b",
    "__ow_text_height",
    "__ow_shadow_x",
    "__ow_shadow_y",
    "__ow_shadow_r",
    "__ow_shadow_g",
    "__ow_shadow_b",
    "__ow_shadow_a",
    "__ow_font_path",
    "__ow_font_flags",
    "__ow_font_size",
    "__ow_justifyH",
    "__ow_justifyV",
    "__ow_wordwrap",
    "__ow_nonspacewrap",
    "__ow_indented_wrap",
    "__ow_spacing",
    "__ow_maxlines",
    "__ow_alpha_grad_start",
    "__ow_alpha_grad_length",
    "__ow_model_unit_guid_lo",
    "__ow_model_unit_guid_hi",
    "__ow_model_path",
    "__ow_model_display",
    "__ow_model_facing",
    "__ow_model_scale",
    "__ow_model_x",
    "__ow_model_y",
    "__ow_model_z",
    "__ow_model_camera",
    "__ow_model_sequence",
    "__ow_model_sequence_time",
};

using Keys = openwow::ui::game::runtime::InternedLuaFieldKeys;

consteval int Ordinal(const std::string_view name) {
  return openwow::ui::game::runtime::InternedLuaFieldKeyOrdinal(kNames, name);
}

template <int Key>
void RawGet(lua_State* lua, const int table_index, const Keys keys) {
  openwow::ui::game::runtime::RawGetInternedLuaBlockField(lua, table_index,
                                                          keys.index, Key);
}

template <int Key>
[[nodiscard]] bool ReadNumber(lua_State* lua, const int table_index,
                              const Keys keys, double* const out_value) {
  RawGet<Key>(lua, table_index, keys);
  const bool has_value = lua_isnumber(lua, -1) != 0;
  if (has_value) *out_value = lua_tonumber(lua, -1);
  lua_pop(lua, 1);
  return has_value;
}

template <int Key>
[[nodiscard]] bool ReadInteger(lua_State* lua, const int table_index,
                               const Keys keys, int* const out_value) {
  RawGet<Key>(lua, table_index, keys);
  const bool has_value = lua_isinteger(lua, -1) != 0;
  if (has_value) *out_value = static_cast<int>(lua_tointeger(lua, -1));
  lua_pop(lua, 1);
  return has_value;
}

template <int Key>
[[nodiscard]] std::optional<bool> ReadBooleanIfPresent(lua_State* lua,
                                                       const int table_index,
                                                       const Keys keys) {
  RawGet<Key>(lua, table_index, keys);
  std::optional<bool> value;
  if (lua_isboolean(lua, -1) != 0) value = lua_toboolean(lua, -1) != 0;
  lua_pop(lua, 1);
  return value;
}

template <int Key>
[[nodiscard]] std::optional<std::string> ReadString(lua_State* lua,
                                                    const int table_index,
                                                    const Keys keys) {
  RawGet<Key>(lua, table_index, keys);
  std::optional<std::string> value;
  if (const char* const text = lua_tostring(lua, -1); text != nullptr) {
    value = std::string(text);
  }
  lua_pop(lua, 1);
  return value;
}

template <int Key>
void ReadNonEmptyStringInto(lua_State* lua, const int table_index,
                            const Keys keys, std::string* const out_value) {
  RawGet<Key>(lua, table_index, keys);
  if (const char* const text = lua_tostring(lua, -1);
      text != nullptr && *text != '\0') {
    out_value->assign(text);
  }
  lua_pop(lua, 1);
}

template <int Key>
void ReadStringInto(lua_State* lua, const int table_index, const Keys keys,
                    std::string* const out_value) {
  RawGet<Key>(lua, table_index, keys);
  if (const char* const text = lua_tostring(lua, -1); text != nullptr) {
    out_value->assign(text);
  }
  lua_pop(lua, 1);
}

template <int Key>
void Get(lua_State* lua, const int table_index, const Keys keys) {
  openwow::ui::game::runtime::GetInternedLuaBlockField(lua, table_index,
                                                       keys.index, Key);
}

}

}

using namespace lua_projection;
using runtime::render::ResolveTextGradientAlpha;
using runtime::render::ScalePremultipliedTextColor;
using runtime::render::TextAlphaGradient;

const runtime::render::UiTextureInfo*
runtime::render::UiCompositor::ResolvePassTexture(const std::string& path) {
  const auto& underlying_texture_loader = texture_regions_.texture_loader();
  if (!underlying_texture_loader) return nullptr;
  auto slot = ui_texture_pass_memo_.find(path);
  if (slot != ui_texture_pass_memo_.end() &&
      slot->second.pass == texture_pass_generation_) {
    return &pass_textures_[slot->second.index];
  }
  auto info = underlying_texture_loader(path);
  if (!info.has_value()) return nullptr;
  pass_textures_.push_back(std::move(*info));
  const PassTextureSlot resolved{.pass = texture_pass_generation_,
                                 .index = pass_textures_.size() - 1U};
  if (slot != ui_texture_pass_memo_.end()) {
    slot->second = resolved;
  } else {
    ui_texture_pass_memo_.emplace(path, resolved);
  }
  return &pass_textures_.back();
}

void runtime::render::UiCompositor::Render(const UiCompositorFrame& compositor_frame) {
  auto* const lua_ = compositor_frame.lua;
  auto* const session_ = compositor_frame.session;
  const auto* const vfs_ = compositor_frame.vfs;
  const auto& minimap_surface_submitter_ = compositor_frame.minimap_surface_submitter;
  const auto& telemetry = compositor_frame.telemetry;
  const auto& movie_presentation = compositor_frame.movie;
  const std::uint8_t view_id = compositor_frame.view_id;
  const std::uint64_t compositor_generation = compositor_frame.generation;
  if (debug_submission_receipts_enabled_) submitted_keys_.clear();

  alpha_source_lua_ = lua_;
  parent_alpha_memo_.clear();
  last_generation_ = compositor_generation;
  const std::uint8_t offscreen_view_begin = compositor_frame.offscreen_view_begin;
  const std::uint8_t offscreen_view_count = compositor_frame.offscreen_view_count;

  const int ui_field_base_top = lua_ != nullptr ? lua_gettop(lua_) : 0;
  const ui_field::Keys ui_keys =
      lua_ != nullptr ? openwow::ui::game::runtime::PushInternedLuaFieldKeys(
                            lua_, ui_field::kNames)
                      : ui_field::Keys{0};
  struct UiFieldKeyBlockPop {
    lua_State* lua;
    int restore_top;
    ~UiFieldKeyBlockPop() {
      if (lua != nullptr) lua_settop(lua, restore_top);
    }
  } const ui_field_key_block_pop{lua_, ui_field_base_top};

  auto* const ui_renderer_ = &render_resources_->ui_renderer();
  auto* const text_cache_ = text_regions_.cache();
  auto* const portrait_renderer_ = texture_regions_.portrait_renderer();
  auto& text_mesh_vertices_ = text_regions_.mesh_vertices();
  auto& text_mesh_indices_ = text_regions_.mesh_indices();
  auto& slider_render_state_ = texture_regions_.slider_state();
  auto& cooldown_render_state_ = texture_regions_.cooldown_state();
  auto& scrolling_message_render_state_ =
      text_regions_.scrolling_message_state();
  auto& scrolling_message_measurements_ =
      text_regions_.scrolling_message_measurements();
  auto& scrolling_message_line_plans_ =
      text_regions_.scrolling_message_line_plans();
  auto& player_model_diagnostic_state_ = model_regions_.diagnostics();

  pass_textures_.clear();
  ++texture_pass_generation_;

  const UiTextureLoad texture_loader_ =
      [this](const std::string& path) -> std::optional<UiTextureInfo> {
    if (const UiTextureInfo* const info = ResolvePassTexture(path);
        info != nullptr) {
      return *info;
    }
    return std::nullopt;
  };

  retained_layout_.RetryPendingNaturalSizes();
  retained_layout_.SolveIfDirty();
  if (frame_traversal_index_.order_dirty())
    frame_traversal_index_.Rebuild(root_scale(), screen_height());

  frame_traversal_index_.RefreshRectCache();

  frame_input_router_.BeginHyperlinkHitTestFrame();

  ui_renderer_->SetSubmissionBatchingEnabled(true);
  ui_renderer_->Begin(view_id, static_cast<int>(screen_width()),
                      static_cast<int>(screen_height()));

  text_regions_.BeginFrame(view_id, screen_width(), screen_height());

  struct TextSubmissionReceipt {
    std::size_t outline_draws{0u};
    std::size_t body_draws{0u};
  };
  const auto submit_text_layout =
      [&text_mesh_vertices_, &text_mesh_indices_, ui_renderer_](
          const openwow::render::BgfxTextLayout& layout,
          const std::uint32_t base_abgr, const float x, const float y,
          const std::optional<TextAlphaGradient>& alpha_gradient,
          const bool shadow_pass = false)
      -> TextSubmissionReceipt {
    TextSubmissionReceipt receipt;
    constexpr std::size_t kMaxGlyphsPerMesh =
        (static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) +
         1u) /
        4u;
    const auto submit_pass = [&](const bool outline_pass) {
      for (const auto& batch : layout.batches) {
        const bgfx::TextureHandle texture =
            outline_pass ? batch.outline_texture : batch.body_texture;
        if (!bgfx::isValid(texture) || batch.glyphs.empty()) {
          continue;
        }

        for (std::size_t first = 0; first < batch.glyphs.size();
             first += kMaxGlyphsPerMesh) {
          const std::size_t glyph_count =
              std::min(kMaxGlyphsPerMesh, batch.glyphs.size() - first);
          text_mesh_vertices_.resize(glyph_count * 4u);
          text_mesh_indices_.resize(glyph_count * 6u);
          for (std::size_t glyph_index = 0; glyph_index < glyph_count;
               ++glyph_index) {
            const auto& glyph = batch.glyphs[first + glyph_index];

            const std::uint32_t resolved_color =
                outline_pass
                    ? openwow::render::ResolveBgfxTextOutlineVertexColor(
                          base_abgr, glyph.color)
                    : (shadow_pass
                           ? openwow::render::ResolveBgfxTextShadowVertexColor(
                                 base_abgr, glyph.color)
                           : openwow::render::ResolveBgfxTextVertexColor(
                                 base_abgr, glyph.color));
            const float x0 = x + glyph.x;
            const float y0 = y + glyph.y;
            const float x1 = x0 + glyph.width;
            const float y1 = y0 + glyph.height;
            const std::size_t vertex = glyph_index * 4u;
            const auto gradient_color = [&](const bool trailing_edge) {
              return alpha_gradient.has_value()
                         ? ScalePremultipliedTextColor(
                               resolved_color,
                               ResolveTextGradientAlpha(*alpha_gradient,
                                                        glyph.sequence_index,
                                                        trailing_edge))
                         : resolved_color;
            };
            const std::uint32_t leading_color = gradient_color(false);
            const std::uint32_t trailing_color = gradient_color(true);
            text_mesh_vertices_[vertex + 0u] = {x0, y0, glyph.u0, glyph.v0,
                                                leading_color};
            text_mesh_vertices_[vertex + 1u] = {x1, y0, glyph.u1, glyph.v0,
                                                trailing_color};
            text_mesh_vertices_[vertex + 2u] = {x1, y1, glyph.u1, glyph.v1,
                                                trailing_color};
            text_mesh_vertices_[vertex + 3u] = {x0, y1, glyph.u0, glyph.v1,
                                                leading_color};
            const std::size_t index = glyph_index * 6u;
            text_mesh_indices_[index + 0u] =
                static_cast<std::uint16_t>(vertex + 0u);
            text_mesh_indices_[index + 1u] =
                static_cast<std::uint16_t>(vertex + 1u);
            text_mesh_indices_[index + 2u] =
                static_cast<std::uint16_t>(vertex + 2u);
            text_mesh_indices_[index + 3u] =
                static_cast<std::uint16_t>(vertex + 0u);
            text_mesh_indices_[index + 4u] =
                static_cast<std::uint16_t>(vertex + 2u);
            text_mesh_indices_[index + 5u] =
                static_cast<std::uint16_t>(vertex + 3u);
          }

          const bool submitted = ui_renderer_->SubmitMesh({
              .texture = texture,
              .vertices = text_mesh_vertices_,
              .indices = text_mesh_indices_,
              .blend = openwow::render::ui::BlendMode::kCoveragePremultiplied,
              .topology = openwow::render::ui::PrimitiveTopology::kTriangleList,
              .sampler_flags = outline_pass
                                   ? (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP)
                                   : (BGFX_SAMPLER_U_CLAMP |
                                      BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT),
          });
          if (submitted) {
            ++(outline_pass ? receipt.outline_draws : receipt.body_draws);
          }
        }
      }
    };

    submit_pass(true);
    submit_pass(false);
    return receipt;
  };
  const auto publish_hyperlink_regions =
      [this](const openwow::render::BgfxTextLayout& layout,
             const std::string_view frame_name, const float draw_x,
             const float draw_y, const stateful_widgets::Rect& clip) {
        for (const auto& hyperlink : layout.hyperlinks) {
          for (const auto& bounds : hyperlink.bounds) {
            const float clipped_left = std::max(draw_x + bounds.x, clip.x);
            const float clipped_top = std::max(draw_y + bounds.y, clip.y);
            const float clipped_right =
                std::min(draw_x + bounds.x + bounds.width, clip.x + clip.width);
            const float clipped_bottom = std::min(
                draw_y + bounds.y + bounds.height, clip.y + clip.height);
            frame_input_router_.AddHyperlinkHitRegion(
                std::string(frame_name), clipped_left, clipped_top,
                clipped_right, clipped_bottom, hyperlink.link, hyperlink.text);
          }
        }
      };

  std::uint8_t portrait_view_id = offscreen_view_begin;
  const std::uint16_t offscreen_view_end = std::min<std::uint16_t>(
      static_cast<std::uint16_t>(offscreen_view_begin) + offscreen_view_count,
      std::numeric_limits<std::uint8_t>::max() + 1u);

  telemetry.last_render_candidates = 0;
  telemetry.last_render_lua_visibility_queries = 0;
  telemetry.last_render_generation = compositor_generation;
  telemetry.last_render_world_map_descendant_submissions = 0;
  telemetry.last_render_world_map_background_submissions = 0;
  telemetry.last_render_world_map_detail_tile_submissions = 0;
  telemetry.last_render_character_panel_descendant_submissions = 0;
  telemetry.last_render_character_panel_background_submissions = 0;
  telemetry.last_render_character_model_submitted = false;
  telemetry.last_render_player_frame_background_submissions = 0;
  telemetry.last_render_player_portrait_submitted = false;
  telemetry.last_render_player_health_submitted = false;
  telemetry.last_render_player_power_submitted = false;
  telemetry.last_render_action_icon_submitted = false;
  telemetry.last_render_chat_content_submitted = false;

  const auto record_descendant_submissions =
      [&telemetry, this](
          const runtime::FrameTraversalIndex::TraversalEntry& entry,
          const std::size_t count) {
        if (count == 0u) {
          return;
        }
        if (debug_submission_receipts_enabled_) {
          submitted_keys_.insert(entry.key);
        }
        if ((entry.semantic_owner_mask &
             runtime::FrameTraversalIndex::kWorldMapOwner) != 0u) {
          telemetry.last_render_world_map_descendant_submissions += count;
        }
        if ((entry.semantic_owner_mask &
             runtime::FrameTraversalIndex::kCharacterPanelOwner) != 0u) {
          telemetry.last_render_character_panel_descendant_submissions += count;
        }
      };
  const auto record_background_submission =
      [&telemetry, &record_descendant_submissions](
          const runtime::FrameTraversalIndex::TraversalEntry& entry,
          const bool submitted) {
        if (!submitted) {
          return;
        }
        record_descendant_submissions(entry, 1u);
        if ((entry.semantic_owner_mask &
             runtime::FrameTraversalIndex::kWorldMapOwner) != 0u) {
          ++telemetry.last_render_world_map_background_submissions;
        }
        if ((entry.semantic_owner_mask &
             runtime::FrameTraversalIndex::kCharacterPanelOwner) != 0u) {
          ++telemetry.last_render_character_panel_background_submissions;
        }
        if ((entry.semantic_owner_mask &
             runtime::FrameTraversalIndex::kPlayerFrameOwner) != 0u) {

          if (entry.key != "PlayerPortrait") {
            ++telemetry.last_render_player_frame_background_submissions;
          }
        }
      };
  for (const auto& entry : frame_traversal_index_.render_snapshot()) {
    ++telemetry.last_render_candidates;
    if (!entry.effective_visible) {
      continue;
    }

    const auto* frame_ptr = entry.frame;
    if (frame_ptr == nullptr) continue;
    const auto& frame = *frame_ptr;
    const auto* rect_ptr = entry.rect;
    if (rect_ptr == nullptr) continue;

    const auto& rect = *rect_ptr;
    if (rect.width <= 0 || rect.height <= 0) continue;

    const bool has_lua_ref = entry.lua_ref != LUA_NOREF;
    const bool is_texture =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::Texture;
    const bool is_status_bar_fill =
        is_texture &&
        frame.texture_role ==
            openwow::ui::framexml::UiFrame::TextureRole::StatusBarFill;
    const bool is_fontstring =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::FontString;
    const bool is_quest_poi =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::QuestPoiFrame;
    const bool is_status_bar =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::StatusBar;
    const bool is_scrolling_message_frame =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::ScrollingMessageFrame;
    const bool is_message_frame =
        is_scrolling_message_frame ||
        frame.runtime_kind ==
            openwow::ui::framexml::UiFrame::RuntimeKind::MessageFrame;
    const bool is_cooldown =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::Cooldown;
    const bool is_minimap =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::Minimap;
    const bool is_movie_frame =
        frame.runtime_kind ==
        openwow::ui::framexml::UiFrame::RuntimeKind::MovieFrame;
    const bool is_model_widget =
        frame.runtime_kind ==
            openwow::ui::framexml::UiFrame::RuntimeKind::Model ||
        frame.runtime_kind ==
            openwow::ui::framexml::UiFrame::RuntimeKind::PlayerModel ||
        frame.runtime_kind ==
            openwow::ui::framexml::UiFrame::RuntimeKind::DressUpModel ||
        frame.runtime_kind ==
            openwow::ui::framexml::UiFrame::RuntimeKind::TabardModel;

    const auto parent_handle = frame_store_.ParentHandleOf(entry.handle);
    if (frame.texture_role !=
            openwow::ui::framexml::UiFrame::TextureRole::Normal &&
        (!has_lua_ref || !ShouldRenderNativeTextureRegion(
                             lua_, frame, entry.lua_ref, frame_store_,
                             entry.handle,
                             frame_input_router_.mouseover_frame_name()))) {
      continue;
    }

    stateful_widgets::Rect render_rect{
        .x = static_cast<float>(rect.x),
        .y = static_cast<float>(rect.y),
        .width = static_cast<float>(rect.width),
        .height = static_cast<float>(rect.height),
    };
    std::array<openwow::ui::UiScrollClipNode, 8> scroll_clip_nodes{};
    std::size_t scroll_clip_count = 0u;
    for (std::uint8_t index = 0u; index < entry.scroll_clip_ancestor_count;
         ++index) {
      const auto& ancestor = entry.scroll_clip_ancestors[index];

      const auto* ancestor_frame = frame_store_.FindFrame(ancestor.handle);
      if (ancestor_frame == nullptr) continue;
      const auto owner_rect = retained_layout_.rects().find(ancestor.key);
      if (owner_rect == retained_layout_.rects().end()) continue;
      const float pixel_scale = ComputeGameUiRenderPixelScale(
          screen_height(), ancestor.effective_scale);
      scroll_clip_nodes[scroll_clip_count++] = {
          .viewport = {static_cast<float>(owner_rect->second.x),
                       static_cast<float>(owner_rect->second.y),
                       static_cast<float>(owner_rect->second.width),
                       static_cast<float>(owner_rect->second.height)},
          .horizontal_scroll_pixels =
              ancestor_frame->runtime_horizontal_scroll * pixel_scale,
          .vertical_scroll_pixels =
              ancestor_frame->runtime_vertical_scroll * pixel_scale,
      };
    }
    const auto scroll_presentation = openwow::ui::BuildUiScrollPresentation(
        std::span<const openwow::ui::UiScrollClipNode>(scroll_clip_nodes.data(),
                                                       scroll_clip_count));
    if (scroll_presentation.clipped_out) continue;
    render_rect.x += scroll_presentation.offset_x;
    render_rect.y += scroll_presentation.offset_y;
    ScopedUiScissor scroll_scissor(ui_renderer_, scroll_presentation.clip);
    if (frame.texture_role ==
            openwow::ui::framexml::UiFrame::TextureRole::SliderThumb &&
        !frame.parent.empty()) {
      const auto owner_rect = retained_layout_.rects().find(frame.parent);
      const auto owner_ref = frame_store_.FindLuaRef(parent_handle);
      if (owner_rect != retained_layout_.rects().end() &&
          owner_ref.has_value()) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, *owner_ref);
        if (lua_istable(lua_, -1) != 0 &&
            stateful_widgets::ReadSliderState(lua_, -1,
                                              &slider_render_state_)) {
          const auto thumb_plan = stateful_widgets::BuildSliderThumbRenderPlan(
              slider_render_state_,
              {
                  .x = static_cast<float>(owner_rect->second.x),
                  .y = static_cast<float>(owner_rect->second.y),
                  .width = static_cast<float>(owner_rect->second.width),
                  .height = static_cast<float>(owner_rect->second.height),
              },
              render_rect.width, render_rect.height);
          if (thumb_plan.visible) {
            render_rect = thumb_plan.thumb_rect;
          }
        }
        lua_settop(lua_, top);
      }
    }

    auto alpha_byte = is_texture
                          ? std::uint8_t{255}
                          : openwow::ui::game::QuantizeFrameAlphaByteTruncated(
                                frame.color_a);
    if (has_lua_ref && !is_texture) {
      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
      if (lua_istable(lua_, -1)) {
        alpha_byte =
            is_fontstring
                ? GetLuaFontStringAlphaByteOrDefault(lua_, -1, alpha_byte)
                : GetLuaFrameAlphaByteOrDefault(lua_, -1, alpha_byte);
      }
      lua_settop(lua_, top);
    }

    if (parent_handle != FrameStore::kInvalidFrameHandle) {
      alpha_byte = openwow::ui::game::MultiplyFrameAlphaBytes(
          alpha_byte, ComputeParentAlphaByte(parent_handle));
    }
    const float alpha = static_cast<float>(
        openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte));

    if (alpha <= 0.0f) continue;

    if (is_movie_frame) {
      const auto& movie = movie_presentation;
      if (movie.owner != entry.key || movie.rgba == nullptr ||
          movie.width <= 0 || movie.height <= 0) {
        continue;
      }
      const bgfx::TextureHandle movie_texture =
          render_resources_->UploadMovieFrame(movie.rgba, movie.width,
                                              movie.height, movie.version);
      if (!bgfx::isValid(movie_texture)) {
        continue;
      }

      const float video_aspect =
          static_cast<float>(movie.width) / static_cast<float>(movie.height);
      const float frame_aspect = render_rect.width / render_rect.height;
      stateful_widgets::Rect fitted_rect = render_rect;
      if (frame_aspect > video_aspect) {
        fitted_rect.width = render_rect.height * video_aspect;
        fitted_rect.x += (render_rect.width - fitted_rect.width) * 0.5f;
      } else if (frame_aspect < video_aspect) {
        fitted_rect.height = render_rect.width / video_aspect;
        fitted_rect.y += (render_rect.height - fitted_rect.height) * 0.5f;
      }

      openwow::render::ui::Quad backdrop;
      backdrop.x = render_rect.x;
      backdrop.y = render_rect.y;
      backdrop.w = render_rect.width;
      backdrop.h = render_rect.height;
      backdrop.abgr = PackStraightTextureAbgr(0.0f, 0.0f, 0.0f, 1.0f, alpha);
      backdrop.blend = openwow::render::ui::BlendMode::kAlpha;
      const bool backdrop_submitted = ui_renderer_->SubmitSolid(backdrop);

      openwow::render::ui::Quad quad;
      quad.texture = movie_texture;
      quad.x = fitted_rect.x;
      quad.y = fitted_rect.y;
      quad.w = fitted_rect.width;
      quad.h = fitted_rect.height;
      quad.abgr = PackStraightTextureAbgr(1.0f, 1.0f, 1.0f, 1.0f, alpha);
      quad.blend = openwow::render::ui::BlendMode::kAlpha;
      quad.sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
      const bool movie_submitted = ui_renderer_->Submit(quad);
      record_background_submission(entry,
                                   backdrop_submitted || movie_submitted);
      continue;
    }

    if (is_status_bar_fill) {

      const auto owner_rect = retained_layout_.rects().find(frame.parent);
      auto* status_bar = frame_store_.FindStatusBar(frame.parent);
      if (owner_rect == retained_layout_.rects().end() ||
          status_bar == nullptr || !has_lua_ref) {
        continue;
      }

      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
      if (lua_istable(lua_, -1) != 0) {
        stateful_widgets::Rect owner_render_rect{
            .x = static_cast<float>(owner_rect->second.x) +
                 scroll_presentation.offset_x,
            .y = static_cast<float>(owner_rect->second.y) +
                 scroll_presentation.offset_y,
            .width = static_cast<float>(owner_rect->second.width),
            .height = static_cast<float>(owner_rect->second.height),
        };
        const auto status_bar_snapshot = status_bar->Snapshot();
        const auto plan = stateful_widgets::BuildStatusBarRenderPlan(
            status_bar_snapshot, owner_render_rect);
        if (plan.visible) {
          TextureRenderState& texture_state = texture_state_scratch_;
          BuildTextureRenderStateInto(lua_, -1, frame, session_, vfs_,
                                      portrait_renderer_, &portrait_view_id,
                                      offscreen_view_end, &texture_state);

          openwow::render::ui::Quad quad;
          quad.x = plan.fill_rect.x;
          quad.y = plan.fill_rect.y;
          quad.w = plan.fill_rect.width;
          quad.h = plan.fill_rect.height;
          quad.blend = texture_state.blend;
          quad.abgr = PackStraightTextureAbgr(
              texture_state.color_r, texture_state.color_g,
              texture_state.color_b, texture_state.color_a, alpha);
          const auto status_uv = texture_state.uv_quad.ToUiRendererOrder();
          for (std::size_t uv_index = 0u; uv_index < status_uv.size();
               ++uv_index) {
            quad.uv_quad[uv_index] = {status_uv[uv_index].u,
                                      status_uv[uv_index].v};
          }
          quad.has_custom_uv_quad = true;
          quad.desaturated = texture_state.desaturated;
          quad.flip_texture_y =
              texture_state.dynamic_texture_is_render_target &&
              bgfx::getCaps()->originBottomLeft;
          quad.sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

          bool submitted = false;
          if (bgfx::isValid(texture_state.dynamic_texture)) {
            quad.texture = texture_state.dynamic_texture;
            submitted = ui_renderer_->Submit(quad);
          } else if (!texture_state.texture_path.empty()) {
            if (const UiTextureInfo* const texture =
                    ResolvePassTexture(texture_state.texture_path);
                texture != nullptr) {
              quad.texture = TextureHandleFromInfo(*texture);
              submitted =
                  bgfx::isValid(quad.texture) && ui_renderer_->Submit(quad);
            }
          }
          if (!submitted && (texture_state.solid_color_texture ||
                             texture_state.has_gradient)) {
            submitted = ui_renderer_->SubmitSolid(quad);
          }
          record_descendant_submissions(entry, submitted ? 1u : 0u);
          if (submitted && frame.parent == "PlayerFrameHealthBar") {
            telemetry.last_render_player_health_submitted = true;
          } else if (submitted && frame.parent == "PlayerFrameManaBar") {
            telemetry.last_render_player_power_submitted = true;
          }
        }
      }
      lua_settop(lua_, top);
      continue;
    }

    if (is_minimap) {
      if (!minimap_surface_submitter_) continue;
      const bool submitted = minimap_surface_submitter_(
          *ui_renderer_,
          MinimapCompositeCommand{
              .x = render_rect.x,
              .y = render_rect.y,
              .width = render_rect.width,
              .height = render_rect.height,
              .abgr = PackStraightTextureAbgr(1.0f, 1.0f, 1.0f, 1.0f, alpha)});
      record_background_submission(entry, submitted);
      continue;
    }

    if (is_model_widget) {
      const auto record_model_state = [&player_model_diagnostic_state_,
                                       &entry](std::string detail) {
        auto& previous = player_model_diagnostic_state_[entry.key];
        if (previous == detail) return;
        previous = std::move(detail);
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                  "PlayerModel " + entry.key + ": " + previous);
      };
      const auto clear_model_state = [&player_model_diagnostic_state_,
                                      &entry]() {
        player_model_diagnostic_state_.erase(entry.key);
      };
      if (!has_lua_ref || portrait_view_id >= offscreen_view_end) {
        record_model_state(!has_lua_ref ? "missing retained Lua binding"
                                        : "offscreen view budget exhausted");
        continue;
      }

      std::uint64_t guid_raw = 0u;

      std::uint32_t bound_display_id = 0u;
      float model_rotation =
          frame.has_model_facing ? frame.model_facing_rad : 0.0f;
      float model_scale = frame.has_model_scale ? frame.model_scale : 1.0f;
      float model_x = frame.has_model_position ? frame.model_x : 0.0f;
      float model_y = frame.has_model_position ? frame.model_y : 0.0f;
      float model_z = frame.has_model_position ? frame.model_z : 0.0f;
      int model_camera = frame.has_model_camera ? frame.model_camera : 0;
      bool has_explicit_model_camera = frame.has_model_camera;
      std::uint32_t model_sequence =
          frame.has_model_sequence
              ? static_cast<std::uint32_t>(std::max(0, frame.model_sequence))
              : 0u;
      std::optional<std::uint32_t> model_sequence_time =
          frame.has_model_sequence_time
              ? std::optional<std::uint32_t>(frame.model_sequence_time_ms)
              : std::nullopt;
      bool has_model_sequence = frame.has_model_sequence;
      std::string model_path = frame.file;
      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
      if (lua_istable(lua_, -1) != 0) {
        if (session_ != nullptr) {
          double guid_low = 0.0;
          double guid_high = 0.0;
          if (ui_field::ReadNumber<ui_field::Ordinal(
                  "__ow_model_unit_guid_lo")>(lua_, -1, ui_keys, &guid_low) &&
              ui_field::ReadNumber<ui_field::Ordinal(
                  "__ow_model_unit_guid_hi")>(lua_, -1, ui_keys, &guid_high)) {
            guid_raw = (static_cast<std::uint64_t>(
                            static_cast<std::uint32_t>(guid_high))
                        << 32u) |
                       static_cast<std::uint32_t>(guid_low);
          }
        }
        if (const auto path =
                ui_field::ReadString<ui_field::Ordinal("__ow_model_path")>(
                    lua_, -1, ui_keys);
            path.has_value()) {
          model_path = *path;
        }
        if (double display_value = 0.0;
            ui_field::ReadNumber<ui_field::Ordinal("__ow_model_display")>(
                lua_, -1, ui_keys, &display_value) &&
            std::isfinite(display_value) && display_value > 0.0) {
          bound_display_id = static_cast<std::uint32_t>(std::min(
              display_value,
              static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
        }

        double rotation = 0.0;
        const bool has_rotation =
            ui_field::ReadNumber<ui_field::Ordinal("__ow_model_facing")>(
                lua_, -1, ui_keys, &rotation);
        if (has_rotation && std::isfinite(rotation)) {
          model_rotation = static_cast<float>(rotation);
        }
        double component = 0.0;
        if (ui_field::ReadNumber<ui_field::Ordinal("__ow_model_scale")>(
                lua_, -1, ui_keys, &component) &&
            std::isfinite(component) && component > 0.0) {
          model_scale = static_cast<float>(component);
        }
        if (ui_field::ReadNumber<ui_field::Ordinal("__ow_model_x")>(
                lua_, -1, ui_keys, &component) &&
            std::isfinite(component)) {
          model_x = static_cast<float>(component);
        }
        if (ui_field::ReadNumber<ui_field::Ordinal("__ow_model_y")>(
                lua_, -1, ui_keys, &component) &&
            std::isfinite(component)) {
          model_y = static_cast<float>(component);
        }
        if (ui_field::ReadNumber<ui_field::Ordinal("__ow_model_z")>(
                lua_, -1, ui_keys, &component) &&
            std::isfinite(component)) {
          model_z = static_cast<float>(component);
        }
        if (ui_field::ReadNumber<ui_field::Ordinal("__ow_model_camera")>(
                lua_, -1, ui_keys, &component) &&
            std::isfinite(component)) {
          model_camera = static_cast<int>(
              std::clamp(component, 0.0,
                         static_cast<double>(std::numeric_limits<int>::max())));
          has_explicit_model_camera = true;
        }
        if (ui_field::ReadNumber<ui_field::Ordinal("__ow_model_sequence")>(
                lua_, -1, ui_keys, &component) &&
            std::isfinite(component) && component >= 0.0) {
          model_sequence = static_cast<std::uint32_t>(std::min(
              component,
              static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
          has_model_sequence = true;
        }
        if (ui_field::ReadNumber<ui_field::Ordinal(
                    "__ow_model_sequence_time")>(lua_, -1, ui_keys, &component) &&
            std::isfinite(component) && component >= 0.0) {
          model_sequence_time = static_cast<std::uint32_t>(std::min(
              component,
              static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
        }
      }
      lua_settop(lua_, top);

      const auto* unit =
          session_ != nullptr
              ? session_->objects().GetUnit(openwow::game::ObjectGuid(guid_raw))
              : nullptr;
      const std::uint32_t instance_id =
          unit != nullptr ? unit->GetPrimaryM2InstanceId() : 0u;

      const bool allow_display_fallback =
          unit == nullptr || !unit->IsPlayer();

      std::array<std::string, 3> display_texture_paths{};
      std::optional<openwow::render::m2::M2ParticleColorRecord>
          display_particle_colors;
      if (instance_id == 0u && model_path.empty() && bound_display_id != 0u &&
          allow_display_fallback && session_ != nullptr) {
        if (const auto* dbc = session_->GetDbcLoader(); dbc != nullptr) {
          if (const auto* display =
                  dbc->creature_display_info().LookupEntry(bound_display_id);
              display != nullptr) {
            if (const auto* model_data =
                    dbc->creature_model_data().LookupEntry(display->model_id);
                model_data != nullptr) {
              model_path = openwow::data::m2::NormalizeModelPath(
                  std::string(model_data->model_name));

              const auto separator = model_path.find_last_of("\\/");
              const std::string_view model_dir =
                  separator == std::string::npos
                      ? std::string_view{}
                      : std::string_view(model_path).substr(0u, separator + 1u);
              for (std::size_t index = 0u;
                   index < display_texture_paths.size(); ++index) {
                const std::string_view variation =
                    display->texture_variation[index];
                if (!variation.empty()) {
                  display_texture_paths[index] =
                      std::string(model_dir).append(variation);
                }
              }
              if (display->particle_color_id != 0u) {
                if (const auto* record = dbc->particle_color().LookupEntry(
                        display->particle_color_id);
                    record != nullptr) {
                  display_particle_colors =
                      openwow::render::m2::M2ParticleColorRecord{
                          .start = record->start,
                          .mid = record->mid,
                          .end = record->end,
                      };
                }
              }
            }
          }
        }
      }
      if (instance_id == 0u && model_path.empty()) {
        record_model_state("bound unit has no prepared M2 instance");
        continue;
      }

      auto& surface = model_regions_.surface(entry.key);
      const auto target_width = static_cast<std::uint16_t>(
          std::clamp(std::lround(render_rect.width), 16l, 2048l));
      const auto target_height = static_cast<std::uint16_t>(
          std::clamp(std::lround(render_rect.height), 16l, 2048l));
      if (!surface->IsValid() &&
          !surface->Initialize(target_width, target_height)) {
        continue;
      }
      surface->Resize(target_width, target_height);
      if (instance_id != 0u) {
        const auto visual_tree =
            m2_system_.QueryVisualTreeRevision(instance_id);
        if (visual_tree.status != openwow::render::m2::M2ResultStatus::kReady ||
            visual_tree.revision == 0u) {
          record_model_state(
              "visual tree unavailable status=" +
              std::to_string(static_cast<int>(visual_tree.status)) +
              " reason=" +
              std::to_string(static_cast<int>(visual_tree.reason)) +
              " detail=" + visual_tree.detail);
          continue;
        }
        surface->SetSourceInstance(instance_id, visual_tree.revision);
      } else {
        surface->SetModelPath(std::move(model_path),
                              std::move(display_texture_paths),
                              std::move(display_particle_colors));
      }
      surface->SetModelRotation(model_rotation);
      surface->SetModelTransform(model_scale, model_x, model_y, model_z);
      if (frame.runtime_kind ==
          openwow::ui::framexml::UiFrame::RuntimeKind::Model) {

        const float pixels_per_script_unit =
            ComputeGameUiRenderPixelScale(screen_height(), 1.0f);
        const float rect_width_script =
            pixels_per_script_unit > 0.0f
                ? render_rect.width / pixels_per_script_unit
                : render_rect.width;
        const float rect_height_script =
            pixels_per_script_unit > 0.0f
                ? render_rect.height / pixels_per_script_unit
                : render_rect.height;
        surface->SetSimpleModelOrthographicFrame(
            openwow::render::SimpleModelOrthographicFrame{
                .rect_width = openwow::ui::PixelUiHorizontalCoordinateToStored(
                    rect_width_script),
                .rect_height = openwow::ui::PixelUiHorizontalCoordinateToStored(
                    rect_height_script),
                .frame_scale = entry.effective_scale,
                .ui_vertical_stretch =
                    openwow::ui::ApplyCachedUiVerticalScale(1.0f),
            });
      } else {
        surface->SetSimpleModelOrthographicFrame(std::nullopt);
      }
      if (has_explicit_model_camera ||
          frame.runtime_kind ==
              openwow::ui::framexml::UiFrame::RuntimeKind::Model) {

        surface->SetCameraIndex(model_camera);
      } else {

        surface->SetCameraType(1u);
      }
      if (has_model_sequence) {
        surface->SetAnimation(model_sequence, model_sequence_time);
      } else {
        surface->ClearAnimationOverride();
      }
      const auto result = surface->RenderToTexture(portrait_view_id);
      ++portrait_view_id;
      if ((!result.SubmittedCompleteVisual() &&
           !surface->HasPresentedContent()) ||
          !bgfx::isValid(surface->GetTexture())) {
        record_model_state(
            "no presented visual status=" +
            std::to_string(static_cast<int>(result.status)) +
            " reason=" + std::to_string(static_cast<int>(result.reason)) +
            " detail=" + result.detail);
        continue;
      }
      clear_model_state();

      openwow::render::ui::Quad quad;
      quad.texture = surface->GetTexture();
      quad.x = render_rect.x;
      quad.y = render_rect.y;
      quad.w = render_rect.width;
      quad.h = render_rect.height;
      quad.abgr = PackStraightTextureAbgr(1.0f, 1.0f, 1.0f, 1.0f, alpha);
      quad.blend = openwow::render::ui::BlendMode::kAlpha;
      quad.flip_texture_y = bgfx::getCaps()->originBottomLeft;
      const bool submitted = ui_renderer_->Submit(quad);
      if (!submitted) {
        record_model_state("final UI quad submission rejected");
      } else {
        clear_model_state();
      }
      if (submitted && entry.key == "CharacterModelFrame") {
        telemetry.last_render_character_model_submitted = true;

        static bool logged_character_model_receipt = false;
        if (!logged_character_model_receipt) {
          logged_character_model_receipt = true;
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kInfo,
              "CharacterModelFrame receipt: instance=" +
                  std::to_string(instance_id) +
                  " has_sequence=" + (has_model_sequence ? "1" : "0") +
                  " sequence=" + std::to_string(model_sequence) +
                  " camera_by_type=" +
                  (has_explicit_model_camera ? "0" : "1"));
        }
      }
      record_background_submission(entry, submitted);
      continue;
    }

    if (is_quest_poi) {
      if (has_lua_ref) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
        if (lua_istable(lua_, -1)) {
          const openwow::ui::framexml::FrameRect presented_rect{
              .x = static_cast<int>(std::lround(render_rect.x)),
              .y = static_cast<int>(std::lround(render_rect.y)),
              .width = static_cast<int>(std::lround(render_rect.width)),
              .height = static_cast<int>(std::lround(render_rect.height)),
          };
          RenderQuestPoiFrame(lua_, -1, frame, presented_rect, session_,
                              world_map_, texture_loader_, ui_renderer_);
        }
        lua_settop(lua_, top);
      }
      continue;
    }

    if (is_status_bar) {

      continue;
    }

    if (is_cooldown) {

      static bool logged_cooldown_widget_reached = false;
      if (!logged_cooldown_widget_reached) {
        logged_cooldown_widget_reached = true;
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "Cooldown widget reached the compositor: key=" + entry.key +
                " lua_ref=" + std::to_string(entry.lua_ref));
      }
      if (!has_lua_ref) {
        continue;
      }
      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
      if (lua_istable(lua_, -1) == 0 ||
          !stateful_widgets::ReadCooldownState(lua_, -1,
                                               &cooldown_render_state_)) {
        lua_settop(lua_, top);
        continue;
      }
      lua_settop(lua_, top);

      static bool logged_cooldown_state_read = false;
      if (!logged_cooldown_state_read) {
        logged_cooldown_state_read = true;
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "Cooldown widget state read: key=" + entry.key + " enabled=" +
                std::to_string(cooldown_render_state_.enabled ? 1 : 0) +
                " start_ms=" +
                std::to_string(cooldown_render_state_.start_tick_ms) +
                " duration_ms=" +
                std::to_string(cooldown_render_state_.duration_ms) +
                " now_ms=" +
                std::to_string(openwow::core::GameClock::GetTickCount32()) +
                " fill_abgr=" +
                std::to_string(cooldown_render_state_.fill_abgr) + " rect=" +
                std::to_string(static_cast<int>(render_rect.width)) + "x" +
                std::to_string(static_cast<int>(render_rect.height)));
      }

      const auto plan = stateful_widgets::BuildCooldownRenderPlan(
          cooldown_render_state_, render_rect,
          openwow::core::GameClock::GetTickCount32());
      std::size_t submitted_draws = 0u;
      if (plan.draw_sweep) {
        std::array<openwow::render::ui::MeshVertex, 11u> vertices{};
        for (std::size_t index = 0u; index < plan.sweep_vertex_count; ++index) {
          const auto& source = plan.sweep_vertices[index];
          vertices[index] = {source.x, source.y, source.u, source.v,
                             cooldown_render_state_.fill_abgr};
        }
        if (ui_renderer_->SubmitSolidMesh({
                .vertices = std::span<const openwow::render::ui::MeshVertex>(
                    vertices.data(), plan.sweep_vertex_count),
                .indices = std::span<const std::uint16_t>(
                    plan.sweep_indices.data(), plan.sweep_index_count),
                .blend = openwow::render::ui::BlendMode::kAlpha,
            })) {
          ++submitted_draws;
        }
      }

      const auto submit_cooldown_texture =
          [&](const std::string& path, const std::uint32_t abgr,
              const float rotation_radians, const float uv_scale) {
            if (path.empty()) {
              return false;
            }
            const UiTextureInfo* const texture = ResolvePassTexture(path);
            if (texture == nullptr) {
              return false;
            }
            openwow::render::ui::Quad quad;
            quad.texture = TextureHandleFromInfo(*texture);
            if (!bgfx::isValid(quad.texture)) {
              return false;
            }
            quad.x = render_rect.x;
            quad.y = render_rect.y;
            quad.w = render_rect.width;
            quad.h = render_rect.height;
            quad.abgr = abgr;
            quad.blend = openwow::render::ui::BlendMode::kAlpha;
            if (std::fabs(rotation_radians) > 0.000001f ||
                std::fabs(uv_scale - 1.0f) > 0.000001f) {
              const float cosine = std::cos(rotation_radians);
              const float sine = std::sin(rotation_radians);
              constexpr std::array<std::array<float, 2>, 4u> corners{{
                  {{0.0f, 0.0f}},
                  {{1.0f, 0.0f}},
                  {{1.0f, 1.0f}},
                  {{0.0f, 1.0f}},
              }};
              for (std::size_t index = 0u; index < corners.size(); ++index) {
                const float x = corners[index][0] - 0.5f;
                const float y = corners[index][1] - 0.5f;
                quad.uv_quad[index] = {
                    0.5f + (x * cosine - y * sine) * uv_scale,
                    0.5f + (x * sine + y * cosine) * uv_scale,
                };
              }
              quad.has_custom_uv_quad = true;
            }
            return ui_renderer_->Submit(quad);
          };
      if (plan.draw_edge &&
          submit_cooldown_texture(cooldown_render_state_.edge_texture,
                                  cooldown_render_state_.edge_abgr,
                                  plan.edge_rotation_radians, 1.0f)) {
        ++submitted_draws;
      }
      if (plan.draw_ready_flash &&
          submit_cooldown_texture(
              cooldown_render_state_.bling_texture,
              [&]() {

                const auto color = cooldown_render_state_.ready_flash_abgr;
                const auto alpha = static_cast<std::uint32_t>(
                    static_cast<float>(color >> 24u) * plan.ready_flash_alpha);
                return (color & 0x00ffffffu) | (alpha << 24u);
              }(),
              plan.ready_flash_rotation_radians, plan.ready_flash_uv_scale)) {
        ++submitted_draws;
      }
      record_descendant_submissions(entry, submitted_draws);

      if (plan.draw_sweep) {
        static bool logged_active_cooldown_submission = false;
        static bool logged_starved_cooldown_submission = false;
        if (submitted_draws > 0u && !logged_active_cooldown_submission) {
          logged_active_cooldown_submission = true;
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kInfo,
              "Cooldown sweep active and submitted: key=" + entry.key);
        } else if (submitted_draws == 0u && !logged_starved_cooldown_submission) {
          logged_starved_cooldown_submission = true;
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kWarn,
              "Cooldown sweep active but no draws submitted: key=" + entry.key);
        }
      }
      continue;
    }

    if (is_message_frame) {
      if (!has_lua_ref || text_cache_ == nullptr) {
        continue;
      }

      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
      if (lua_istable(lua_, -1) == 0 ||
          !stateful_widgets::ReadScrollingMessageFrameState(
              lua_, -1, &scrolling_message_render_state_)) {
        lua_settop(lua_, top);
        continue;
      }

      std::string font_flags_token;
      ui_field::ReadStringInto<ui_field::Ordinal("__ow_font_flags")>(
          lua_, -1, ui_keys, &font_flags_token);
      const std::uint32_t font_flags =
          openwow::render::ParseFontFlagsString(font_flags_token);
      std::string justify_h = "LEFT";
      ui_field::ReadStringInto<ui_field::Ordinal("__ow_justifyH")>(
          lua_, -1, ui_keys, &justify_h);
      const bool indented_word_wrap =
          ui_field::ReadBooleanIfPresent<ui_field::Ordinal(
              "__ow_indented_wrap")>(lua_, -1, ui_keys)
              .value_or(false);
      double shadow_component = 0.0;

      float shadow_x = 0.0f;
      float shadow_y = 0.0f;
      float shadow_r = 0.0f;
      float shadow_g = 0.0f;
      float shadow_b = 0.0f;
      float shadow_a = 1.0f;
      if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_x")>(
              lua_, -1, ui_keys, &shadow_component)) {
        shadow_x = openwow::ui::StoredUiHorizontalCoordinateToPixels(
            static_cast<float>(shadow_component));
      }
      if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_y")>(
              lua_, -1, ui_keys, &shadow_component)) {
        shadow_y = openwow::ui::StoredUiHorizontalCoordinateToPixels(
            static_cast<float>(shadow_component));
      }
      if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_r")>(
              lua_, -1, ui_keys, &shadow_component)) {
        shadow_r = static_cast<float>(shadow_component);
      }
      if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_g")>(
              lua_, -1, ui_keys, &shadow_component)) {
        shadow_g = static_cast<float>(shadow_component);
      }
      if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_b")>(
              lua_, -1, ui_keys, &shadow_component)) {
        shadow_b = static_cast<float>(shadow_component);
      }
      if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_a")>(
              lua_, -1, ui_keys, &shadow_component)) {
        shadow_a = static_cast<float>(shadow_component);
      }
      lua_settop(lua_, top);

      const float text_render_scale =
          ComputeGameUiRenderPixelScale(screen_height(), entry.effective_scale);
      const int scaled_height_px = std::max(
          1, openwow::render::ClampRetailFontPixelHeight(static_cast<int>(
                 std::lround(scrolling_message_render_state_.font_height *
                             text_render_scale))));

      const int outline_px =
          openwow::render::FontFlagsIsThickOutline(font_flags)
              ? openwow::render::kRetailThickOutlinePixels
              : (openwow::render::FontFlagsHasOutline(font_flags)
                     ? openwow::render::kRetailNormalOutlinePixels
                     : 0);
      shadow_x *= text_render_scale;
      shadow_y *= text_render_scale;

      const std::size_t message_count =
          scrolling_message_render_state_.messages.size();
      scrolling_message_measurements_.resize(message_count);

      auto& message_text_key = text_key_scratch_;
      const auto make_text_key =
          [&](const std::string& text) -> const openwow::render::BgfxTextKey& {
        message_text_key.font_path.assign(
            scrolling_message_render_state_.font_path);
        message_text_key.height_px = scaled_height_px;
        message_text_key.text.assign(text);
        message_text_key.abgr = 0xffffffffu;
        message_text_key.wrap_px =
            std::max(0, static_cast<int>(render_rect.width));
        message_text_key.max_height_px = 0;
        message_text_key.line_spacing_px = 0;
        message_text_key.line_height_override_px = 0;
        message_text_key.outline_px = outline_px;
        message_text_key.max_lines = 0u;
        message_text_key.word_wrap = true;
        message_text_key.non_space_wrap = false;
        message_text_key.indented_word_wrap = indented_word_wrap;
        message_text_key.monochrome =
            openwow::render::FontFlagsIsMonochrome(font_flags);
        return message_text_key;
      };

      std::size_t message_index =
          message_count == 0u
              ? 0u
              : message_count - 1u -
                    std::min(scrolling_message_render_state_.scroll_offset,
                             message_count - 1u);
      float measured_height = 0.0f;
      bool first_message = true;
      while (message_count != 0u) {
        const auto& message =
            scrolling_message_render_state_.messages[message_index];
        const auto* text_layout =
            text_cache_->Layout(make_text_key(message.text));
        const float line_height =
            text_layout != nullptr
                ? std::max(1.0f, static_cast<float>(text_layout->height))
                : static_cast<float>(scaled_height_px);
        scrolling_message_measurements_[message_index] = {
            .width = text_layout != nullptr ? text_layout->width : 0.0f,
            .height = line_height,
        };
        if (!first_message &&
            measured_height + line_height > render_rect.height) {
          break;
        }
        measured_height += line_height;
        first_message = false;
        if (message_index == 0u) {
          break;
        }
        --message_index;
      }

      stateful_widgets::BuildScrollingMessageLinePlans(
          scrolling_message_render_state_, render_rect,
          scrolling_message_measurements_,
          openwow::core::GameClock::GetTickCountSeconds(),
          &scrolling_message_line_plans_);
      if (!scrolling_message_line_plans_.empty()) {
        ui_renderer_->SetScissor(render_rect.x, render_rect.y,
                                 render_rect.width, render_rect.height);
        for (const auto& line : scrolling_message_line_plans_) {
          if (line.message_index >=
                  scrolling_message_render_state_.messages.size() ||
              line.alpha <= 0.0f) {
            continue;
          }
          const auto& message =
              scrolling_message_render_state_.messages[line.message_index];

          const auto* text_layout =
              text_cache_->Layout(make_text_key(message.text));
          if (text_layout == nullptr || text_layout->width <= 0 ||
              text_layout->height <= 0) {
            continue;
          }

          float draw_x = line.rect.x;
          if (openwow::text::EqualsIgnoreCaseAscii(justify_h, "CENTER")) {
            draw_x += (line.rect.width - text_layout->width) * 0.5f;
          } else if (openwow::text::EqualsIgnoreCaseAscii(justify_h, "RIGHT")) {
            draw_x += line.rect.width - text_layout->width;
          }
          const float draw_y = line.rect.y;
          const float line_alpha = line.alpha * alpha;
          if ((std::fabs(shadow_x) > 0.001f || std::fabs(shadow_y) > 0.001f) &&
              shadow_a > 0.001f) {
            (void)submit_text_layout(
                *text_layout,
                PackTextAbgrStraight(shadow_r, shadow_g, shadow_b,
                                     shadow_a * line_alpha),
                std::floor(draw_x + shadow_x + 0.5f),
                std::floor(draw_y - shadow_y + 0.5f), std::nullopt,
                true);
          }
          const auto foreground_receipt = submit_text_layout(
              *text_layout,
              PackTextAbgrStraight(message.color.r, message.color.g,
                                   message.color.b, line_alpha),
              std::floor(draw_x + 0.5f), std::floor(draw_y + 0.5f),
              std::nullopt);
          if (scrolling_message_render_state_.hyperlinks_enabled) {
            publish_hyperlink_regions(
                *text_layout, entry.key, std::floor(draw_x + 0.5f),
                std::floor(draw_y + 0.5f), line.clip_rect);
          }
          record_descendant_submissions(entry, foreground_receipt.body_draws);
          if (entry.key == "ChatFrame1" && foreground_receipt.body_draws > 0u) {
            telemetry.last_render_chat_content_submitted = true;
          }
        }
        ui_renderer_->ClearScissor();
      }
      continue;
    }

    if (is_fontstring && text_cache_) {

      openwow::render::BgfxTextKey& text_key = text_key_scratch_;
      std::string& text = text_key.text;
      text.assign(frame.text);
      float text_r = frame.color_r;
      float text_g = frame.color_g;
      float text_b = frame.color_b;

      float shadow_x = 0.0f;
      float shadow_y = 0.0f;
      float shadow_r = 0.0f;
      float shadow_g = 0.0f;
      float shadow_b = 0.0f;
      float shadow_a = 1.0f;
      std::string font_path = "/Fonts/FRIZQT__.TTF";
      float font_height_px = 12.0f;
      std::string justify_h = frame.justify_h;
      std::string justify_v = frame.justify_v;
      bool word_wrap = true;
      bool non_space_wrap = false;
      bool indented_word_wrap = false;
      float line_spacing_px = 0.0f;
      float line_height_override_px = 0.0f;
      int max_lines = 0;
      std::uint32_t font_flags = 0;
      std::optional<TextAlphaGradient> alpha_gradient;
      if (has_lua_ref) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
        if (lua_istable(lua_, -1)) {

          ui_field::Get<ui_field::Ordinal("__ow_text")>(lua_, -1, ui_keys);
          if (lua_isstring(lua_, -1)) {
            text.assign(lua_tostring(lua_, -1));
          }
          lua_pop(lua_, 1);

          double component = 0.0;
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_text_r")>(
                  lua_, -1, ui_keys, &component)) {
            text_r = static_cast<float>(component);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_text_g")>(
                  lua_, -1, ui_keys, &component)) {
            text_g = static_cast<float>(component);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_text_b")>(
                  lua_, -1, ui_keys, &component)) {
            text_b = static_cast<float>(component);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_x")>(
                  lua_, -1, ui_keys, &component)) {
            shadow_x = openwow::ui::StoredUiHorizontalCoordinateToPixels(
                static_cast<float>(component));
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_y")>(
                  lua_, -1, ui_keys, &component)) {
            shadow_y = openwow::ui::StoredUiHorizontalCoordinateToPixels(
                static_cast<float>(component));
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_r")>(
                  lua_, -1, ui_keys, &component)) {
            shadow_r = static_cast<float>(component);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_g")>(
                  lua_, -1, ui_keys, &component)) {
            shadow_g = static_cast<float>(component);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_b")>(
                  lua_, -1, ui_keys, &component)) {
            shadow_b = static_cast<float>(component);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_shadow_a")>(
                  lua_, -1, ui_keys, &component)) {
            shadow_a = static_cast<float>(component);
          }
          ui_field::ReadNonEmptyStringInto<ui_field::Ordinal("__ow_font_path")>(
              lua_, -1, ui_keys, &font_path);
          if (const auto value =
                  ui_field::ReadString<ui_field::Ordinal("__ow_font_flags")>(
                      lua_, -1, ui_keys);
              value.has_value()) {
            font_flags = openwow::render::ParseFontFlagsString(*value);
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_font_size")>(
                  lua_, -1, ui_keys, &component) &&
              component > 0.0) {
            font_height_px = static_cast<float>(component);
          }
          ui_field::ReadStringInto<ui_field::Ordinal("__ow_justifyH")>(
              lua_, -1, ui_keys, &justify_h);
          ui_field::ReadStringInto<ui_field::Ordinal("__ow_justifyV")>(
              lua_, -1, ui_keys, &justify_v);
          word_wrap =
              ui_field::ReadBooleanIfPresent<ui_field::Ordinal("__ow_wordwrap")>(
                  lua_, -1, ui_keys)
                  .value_or(true);
          non_space_wrap = ui_field::ReadBooleanIfPresent<ui_field::Ordinal(
                               "__ow_nonspacewrap")>(lua_, -1, ui_keys)
                               .value_or(false);
          indented_word_wrap = ui_field::ReadBooleanIfPresent<ui_field::Ordinal(
                                   "__ow_indented_wrap")>(lua_, -1, ui_keys)
                                   .value_or(false);
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_spacing")>(
                  lua_, -1, ui_keys, &component)) {
            line_spacing_px = openwow::ui::StoredUiHorizontalCoordinateToPixels(
                static_cast<float>(component));
          }
          if (ui_field::ReadNumber<ui_field::Ordinal("__ow_text_height")>(
                  lua_, -1, ui_keys, &component)) {
            line_height_override_px =
                openwow::ui::StoredUiHorizontalCoordinateToPixels(
                    static_cast<float>(component));
          }
          (void)ui_field::ReadInteger<ui_field::Ordinal("__ow_maxlines")>(
              lua_, -1, ui_keys, &max_lines);
          int gradient_start = 0;
          int gradient_length = 0;
          if (ui_field::ReadInteger<ui_field::Ordinal("__ow_alpha_grad_start")>(
                  lua_, -1, ui_keys, &gradient_start) &&
              ui_field::ReadInteger<ui_field::Ordinal("__ow_alpha_grad_length")>(
                  lua_, -1, ui_keys, &gradient_length) &&
              gradient_start >= 0 && gradient_length >= 0) {
            alpha_gradient = TextAlphaGradient{
                .start = static_cast<std::uint32_t>(gradient_start),
                .length = static_cast<std::uint32_t>(gradient_length),
            };
          }
        }
        lua_settop(lua_, top);
      }

      if (!text.empty()) {

        const float text_render_scale = ComputeGameUiRenderPixelScale(
            screen_height(), entry.effective_scale);
        const int scaled_height_px = std::max(
            1,
            openwow::render::ClampRetailFontPixelHeight(static_cast<int>(
                std::lround(font_height_px * text_render_scale))));
        shadow_x *= text_render_scale;
        shadow_y *= text_render_scale;

        const int outline_px =
            openwow::render::FontFlagsIsThickOutline(font_flags)
                ? openwow::render::kRetailThickOutlinePixels
                : (openwow::render::FontFlagsHasOutline(font_flags)
                       ? openwow::render::kRetailNormalOutlinePixels
                       : 0);
        const int wrap_px = openwow::ui::ResolveFontStringRenderWrapWidth(
            frame, word_wrap, non_space_wrap,
            static_cast<int>(render_rect.width));

        text_key.font_path.assign(font_path);
        text_key.height_px = scaled_height_px;

        text_key.abgr = 0xffffffffu;
        text_key.wrap_px = wrap_px;
        text_key.max_height_px =
            wrap_px > 0 ? std::max(0, static_cast<int>(render_rect.height)) : 0;
        text_key.line_spacing_px = static_cast<int>(
            std::lround(line_spacing_px * text_render_scale));
        text_key.line_height_override_px = static_cast<int>(
            std::lround(line_height_override_px * text_render_scale));
        text_key.outline_px = outline_px;
        text_key.max_lines = static_cast<std::uint32_t>(std::max(0, max_lines));
        text_key.word_wrap = word_wrap;
        text_key.non_space_wrap = non_space_wrap;
        text_key.indented_word_wrap = indented_word_wrap;
        text_key.monochrome = openwow::render::FontFlagsIsMonochrome(font_flags);
        const openwow::render::BgfxTextLayout* foreground =
            text_cache_->Layout(text_key);
        if (foreground == nullptr || foreground->width <= 0 ||
            foreground->height <= 0) {
          continue;
        }

        float draw_x = render_rect.x;
        float draw_y = render_rect.y;
        const auto justification =
            openwow::ui::ResolveFontStringJustification(justify_h, justify_v);

        if (openwow::text::EqualsIgnoreCaseAscii(justification.horizontal,
                                                 "center")) {
          draw_x += (render_rect.width - foreground->width) * 0.5f;
        } else if (openwow::text::EqualsIgnoreCaseAscii(
                       justification.horizontal, "right")) {
          draw_x += render_rect.width - foreground->width;
        }

        if (openwow::text::EqualsIgnoreCaseAscii(justification.vertical,
                                                 "middle") ||
            openwow::text::EqualsIgnoreCaseAscii(justification.vertical,
                                                 "center")) {
          draw_y += (render_rect.height - foreground->height) * 0.5f;
        } else if (openwow::text::EqualsIgnoreCaseAscii(justification.vertical,
                                                        "bottom")) {
          draw_y += render_rect.height - foreground->height;
        }

        if ((std::fabs(shadow_x) > 0.001f || std::fabs(shadow_y) > 0.001f) &&
            shadow_a > 0.001f) {
          (void)submit_text_layout(
              *foreground,
              PackTextAbgrStraight(shadow_r, shadow_g, shadow_b,
                                   shadow_a * alpha),
              std::floor(draw_x + shadow_x + 0.5f),
              std::floor(draw_y - shadow_y + 0.5f), alpha_gradient,
              true);
        }

        const auto foreground_receipt = submit_text_layout(
            *foreground, PackTextAbgrStraight(text_r, text_g, text_b, alpha),
            std::floor(draw_x + 0.5f), std::floor(draw_y + 0.5f),
            alpha_gradient);
        record_descendant_submissions(entry, foreground_receipt.body_draws);
      }
      continue;
    }

    TextureRenderState& texture_state = texture_state_scratch_;
    const auto reset_texture_state = [&texture_state]() {
      std::string reusable_texture_path = std::move(texture_state.texture_path);
      reusable_texture_path.clear();
      texture_state = TextureRenderState{};
      texture_state.texture_path = std::move(reusable_texture_path);
    };
    if (has_lua_ref) {
      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, entry.lua_ref);
      if (lua_istable(lua_, -1)) {
        BuildTextureRenderStateInto(lua_, -1, frame, session_, vfs_,
                                    portrait_renderer_, &portrait_view_id,
                                    offscreen_view_end, &texture_state);
      } else {
        reset_texture_state();
        ApplyFrameTextureDefaults(frame, &texture_state);
      }
      lua_settop(lua_, top);
    } else {
      reset_texture_state();
      ApplyFrameTextureDefaults(frame, &texture_state);
    }

    openwow::render::ui::Quad quad;
    quad.x = render_rect.x;
    quad.y = render_rect.y;
    quad.w = render_rect.width;
    quad.h = render_rect.height;
    quad.u0 = frame.tex_left;
    quad.v0 = frame.tex_top;
    quad.u1 = frame.tex_right;
    quad.v1 = frame.tex_bottom;
    quad.blend = texture_state.blend;

    quad.abgr = PackStraightTextureAbgr(
        texture_state.color_r, texture_state.color_g, texture_state.color_b,
        texture_state.color_a, alpha);
    if (texture_state.has_gradient) {
      quad.has_vertex_colors = true;
      for (std::size_t color_index = 0; color_index < quad.vertex_abgr.size();
           ++color_index) {
        const auto& color = texture_state.gradient_colors[color_index];
        quad.vertex_abgr[color_index] =
            PackStraightTextureAbgr(color.r, color.g, color.b, color.a, alpha);
      }
      quad.abgr = quad.vertex_abgr[0];
    }
    quad.desaturated = texture_state.desaturated;
    quad.flip_texture_y = texture_state.dynamic_texture_is_render_target &&
                          bgfx::getCaps()->originBottomLeft;

    auto apply_loaded_texture = [&](const bgfx::TextureHandle handle,
                                    const int texture_width,
                                    const int texture_height) -> bool {
      if (!bgfx::isValid(handle)) {
        return false;
      }

      quad.texture = handle;
      if (is_texture) {
        if (frame.slice != openwow::ui::framexml::TextureSlice::kNone &&
            frame.slice_edge_size_px > 0 && !texture_state.has_custom_uv_quad) {
          const float repeat_tiles =
              openwow::ui::framexml::BackdropEdgeRepeatTiles(frame.slice,
                                                             quad.w, quad.h);
          const auto strip_uv =
              openwow::ui::framexml::ComputeBackdropStripEdgeUvQuad(
                  frame.slice, frame.slice_edge_size_px, texture_width,
                  texture_height, repeat_tiles);
          if (strip_uv.has_value()) {
            const auto src_uvs = strip_uv->ToUiRendererOrder();
            for (std::size_t i = 0; i < src_uvs.size(); ++i) {
              quad.uv_quad[i] = {src_uvs[i].u, src_uvs[i].v};
            }
            quad.has_custom_uv_quad = true;
            quad.sampler_flags = BGFX_SAMPLER_U_CLAMP;
            return true;
          } else {
            const auto slice_uv =
                openwow::ui::framexml::ComputeBackdropSliceUvRect(
                    frame.slice, frame.slice_edge_size_px, texture_width,
                    texture_height);
            if (slice_uv.has_value()) {
              texture_state.uv_quad = TextureQuadUv::FromRect(
                  slice_uv->u0, slice_uv->u1, slice_uv->v0, slice_uv->v1);
            }
          }
        }

        ApplyCSimpleTextureTileExtent(
            texture_state.uv_quad, texture_state.tile_x, texture_state.tile_y,
            quad.w, quad.h, texture_width, texture_height, frame.tile_size_x,
            frame.tile_size_y);
        const auto src_uvs = texture_state.uv_quad.ToUiRendererOrder();
        for (std::size_t i = 0; i < 4; ++i) {
          quad.uv_quad[i] = {src_uvs[i].u, src_uvs[i].v};
        }
        quad.has_custom_uv_quad = true;
        quad.sampler_flags = 0;
        if (!texture_state.tile_x) {
          quad.sampler_flags |= BGFX_SAMPLER_U_CLAMP;
        }
        if (!texture_state.tile_y || texture_state.clamp_v_wrap) {
          quad.sampler_flags |= BGFX_SAMPLER_V_CLAMP;
        }
        return true;
      }

      if (frame.slice != openwow::ui::framexml::TextureSlice::kNone &&
          frame.slice_edge_size_px > 0) {
        const auto slice_uv = openwow::ui::framexml::ComputeBackdropSliceUvRect(
            frame.slice, frame.slice_edge_size_px, texture_width,
            texture_height);
        if (slice_uv.has_value()) {
          quad.u0 = slice_uv->u0;
          quad.v0 = slice_uv->v0;
          quad.u1 = slice_uv->u1;
          quad.v1 = slice_uv->v1;
        }
      }

      if (frame.tile_x || frame.tile_y) {
        const int tile_step_x = (frame.tile_x && frame.tile_size_x > 0)
                                    ? frame.tile_size_x
                                    : rect.width;
        const int tile_step_y = (frame.tile_y && frame.tile_size_y > 0)
                                    ? frame.tile_size_y
                                    : rect.height;
        const int base_step_x = std::max(1, tile_step_x);
        const int base_step_y = std::max(1, tile_step_y);
        const float tile_uv_width = quad.u1 - quad.u0;
        const float tile_uv_height = quad.v1 - quad.v0;

        for (int yy = 0; yy < rect.height; yy += base_step_y) {
          const int remaining_height = rect.height - yy;
          const int draw_height = std::min(tile_step_y, remaining_height);
          const float tile_v1 =
              frame.tile_y ? quad.v0 + tile_uv_height *
                                           (static_cast<float>(draw_height) /
                                            static_cast<float>(base_step_y))
                           : quad.v1;

          for (int xx = 0; xx < rect.width; xx += base_step_x) {
            const int remaining_width = rect.width - xx;
            const int draw_width = std::min(tile_step_x, remaining_width);
            const float tile_u1 =
                frame.tile_x ? quad.u0 + tile_uv_width *
                                             (static_cast<float>(draw_width) /
                                              static_cast<float>(base_step_x))
                             : quad.u1;

            auto tiled_quad = quad;
            tiled_quad.x = static_cast<float>(rect.x + xx);
            tiled_quad.y = static_cast<float>(rect.y + yy);
            tiled_quad.w = static_cast<float>(draw_width);
            tiled_quad.h = static_cast<float>(draw_height);
            tiled_quad.u1 = tile_u1;
            tiled_quad.v1 = tile_v1;
            tiled_quad.sampler_flags =
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            record_background_submission(entry,
                                         ui_renderer_->Submit(tiled_quad));
          }
        }
      }

      return true;
    };

    bool has_texture = false;
    bool used_dynamic_texture = false;
    if (texture_state.tabard_emblem_render_target.has_value()) {
      const auto& descriptor = *texture_state.tabard_emblem_render_target;
      const auto tabard_texture =
          texture_manager_.AcquireTabardEmblemRenderTargetAsync(descriptor);
      has_texture = apply_loaded_texture(
          openwow::render::BgfxTextureLeaseAccess::Get(tabard_texture),
          static_cast<int>(descriptor.width),
          static_cast<int>(descriptor.height));
      if (has_texture && !is_texture && (frame.tile_x || frame.tile_y)) {
        continue;
      }
    }

    if (!has_texture && bgfx::isValid(texture_state.dynamic_texture)) {
      has_texture = apply_loaded_texture(texture_state.dynamic_texture,
                                         texture_state.dynamic_texture_width,
                                         texture_state.dynamic_texture_height);
      used_dynamic_texture = has_texture;
      if (has_texture && !is_texture && (frame.tile_x || frame.tile_y)) {
        continue;
      }
    }

    if (!has_texture && !texture_state.texture_path.empty()) {
      const UiTextureInfo* const tex_info =
          ResolvePassTexture(texture_state.texture_path);
      if (tex_info != nullptr) {
        has_texture = apply_loaded_texture(TextureHandleFromInfo(*tex_info),
                                           tex_info->width, tex_info->height);
        if (has_texture && !is_texture && (frame.tile_x || frame.tile_y)) {
          continue;
        }
      }
    }

    if (has_texture) {
      const bool submitted = ui_renderer_->Submit(quad);
      record_background_submission(entry, submitted);
      if (submitted && entry.key.starts_with("WorldMapDetailTile")) {
        ++telemetry.last_render_world_map_detail_tile_submissions;
      }
      if (submitted && used_dynamic_texture && entry.key == "PlayerPortrait") {
        telemetry.last_render_player_portrait_submitted = true;
      }
      if (submitted && entry.key == "ActionButton1Icon") {
        telemetry.last_render_action_icon_submitted = true;
      }
    } else if (openwow::ui::game::detail::ShouldSubmitSolidUiRegion(
                   is_texture ? std::string_view("texture")
                              : std::string_view{},
                   texture_state.solid_color_texture,
                   texture_state.has_gradient)) {
      record_background_submission(entry, ui_renderer_->SubmitSolid(quad));
    }
  }

  if (render_resources_ != nullptr) {
    render_resources_->RetireIdleModelSurfaces();
  }

  ui_renderer_->End();
}

bool runtime::render::UiCompositor::WasSubmittedLastFrame(
    const std::string_view key) const {
  return debug_submission_receipts_enabled_ &&
         submitted_keys_.contains(std::string(key));
}

void runtime::render::UiCompositor::SetDebugSubmissionReceiptsEnabled(
    const bool enabled) {
  debug_submission_receipts_enabled_ = enabled;
  if (!enabled) submitted_keys_.clear();
}

}
