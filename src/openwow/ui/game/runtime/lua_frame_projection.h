#pragma once

#include "openwow/game/tabard_renderer.h"
#include "openwow/render/ui/ui_renderer.h"
#include "openwow/ui/framexml/layout_resolver.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/runtime/render/ui_render_resources.h"
#include "openwow/ui/game/texture_quad_uv.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <bgfx/bgfx.h>

struct lua_State;

namespace openwow::game {
class WorldSession;
}
namespace openwow::render {
class PortraitRenderer;
}
namespace openwow::vfs {
class VirtualFileSystem;
}
namespace openwow::ui {
class WorldMapSystem;
}

namespace openwow::ui::game::runtime {
class FrameStore;
}

namespace openwow::ui::game::lua_projection {

struct TextureRenderState {
  using GradientColor = openwow::ui::widgets::TextureGradientColor;

  std::string texture_path;
  bgfx::TextureHandle dynamic_texture = BGFX_INVALID_HANDLE;
  int dynamic_texture_width{0};
  int dynamic_texture_height{0};
  TextureQuadUv uv_quad{};
  std::optional<openwow::game::TabardEmblemRenderTargetDescriptor>
      tabard_emblem_render_target;
  std::array<GradientColor, 4> gradient_colors{};
  bool has_custom_uv_quad{false};
  bool has_gradient{false};
  bool clear_texture{false};
  bool solid_color_texture{false};
  bool tile_x{false};
  bool tile_y{false};
  float color_r{1.0f};
  float color_g{1.0f};
  float color_b{1.0f};
  float color_a{1.0f};
  openwow::render::ui::BlendMode blend{
      openwow::render::ui::BlendMode::kAlpha};
  bool desaturated{false};
  bool dynamic_texture_is_render_target{false};
};

[[nodiscard]] openwow::ui::framexml::UiFrame::RuntimeKind ClassifyFrameRuntimeKind(
    std::string_view kind) noexcept;
std::uint8_t GetLuaFrameAlphaByteOrDefault(lua_State* state, int index,
                                           std::uint8_t fallback);
std::uint8_t GetLuaFontStringAlphaByteOrDefault(lua_State* state, int index,
                                                std::uint8_t fallback);

bool SyncFrameRuntimeMetadataFromLua(
    lua_State* state, int frame_index,
    openwow::ui::framexml::UiFrame* frame);
void SyncRetainedBackdropColors(lua_State* state, int owner_index,
                                const std::string& owner_key,
                                runtime::FrameStore* frames);

bool GetLuaBooleanField(lua_State* state, int index, const char* field_name);
std::optional<std::string> GetLuaStringField(lua_State* state, int index,
                                             const char* field_name);
std::optional<std::string> ReadLuaFrameReferenceKey(lua_State* state,
                                                    int value_index);
std::uint32_t PackTextAbgrStraight(float red, float green, float blue,
                                   float alpha);
std::uint32_t PackStraightTextureAbgr(float red, float green, float blue,
                                      float color_alpha,
                                      float inherited_alpha);
void ApplyFrameTextureDefaults(const openwow::ui::framexml::UiFrame& frame,
                               TextureRenderState* state);
TextureRenderState BuildTextureRenderState(
    lua_State* state, int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, std::uint16_t portrait_view_limit);

void BuildTextureRenderStateInto(
    lua_State* state_machine, int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, std::uint16_t portrait_view_limit,
    TextureRenderState* state);

void BuildTextureRenderStateFromLuaFieldsInto(
    lua_State* state_machine, int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, std::uint16_t portrait_view_limit,
    TextureRenderState* state);
bgfx::TextureHandle TextureHandleFromInfo(
    const runtime::render::UiTextureInfo& texture_info);
void RenderQuestPoiFrame(
    lua_State* state, int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    const openwow::ui::framexml::FrameRect& rect,
    openwow::game::WorldSession* session,
    const openwow::ui::WorldMapSystem& world_map,
    const runtime::render::UiTextureLoad& texture_loader,
    openwow::render::ui::UiRenderer* ui_renderer);

bool ShouldRenderNativeTextureRegion(
    lua_State* state, const openwow::ui::framexml::UiFrame& frame,
    int region_ref, const runtime::FrameStore& frames,
    std::uint64_t region_handle, const std::string& mouseover_frame);

}
