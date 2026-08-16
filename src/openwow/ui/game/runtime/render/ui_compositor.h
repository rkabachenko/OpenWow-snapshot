#pragma once

#include "openwow/render/backend/bgfx/bgfx_text_cache.h"
#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/ui/game/minimap_surface_submission.h"

#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_frame_projection.h"
#include "openwow/ui/game/runtime/render/model_region_renderer.h"
#include "openwow/ui/game/runtime/render/text_region_renderer.h"
#include "openwow/ui/game/runtime/render/texture_region_renderer.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace openwow::game {
class WorldSession;
}
namespace openwow::render {
class TextureManager;
namespace m2 {
class M2System;
}
}
namespace openwow::vfs {
class VirtualFileSystem;
}
namespace openwow::ui::framexml {
struct UiFrame;
}
namespace openwow::ui {
class WorldMapSystem;
}

namespace openwow::ui::game::runtime {
class FrameInputRouter;
class FrameTraversalIndex;
class RetainedLayout;
namespace render {

class UiRenderResources;

struct UiRenderTelemetry {
  std::size_t& last_render_candidates;
  std::size_t& last_render_lua_visibility_queries;
  std::uint64_t& last_render_generation;
  std::size_t& last_render_world_map_descendant_submissions;
  std::size_t& last_render_world_map_background_submissions;
  std::size_t& last_render_world_map_detail_tile_submissions;
  std::size_t& last_render_character_panel_descendant_submissions;
  std::size_t& last_render_character_panel_background_submissions;
  bool& last_render_character_model_submitted;
  std::size_t& last_render_player_frame_background_submissions;
  bool& last_render_player_portrait_submitted;
  bool& last_render_player_health_submitted;
  bool& last_render_player_power_submitted;
  bool& last_render_action_icon_submitted;
  bool& last_render_chat_content_submitted;
};

struct UiCompositorFrame {
  struct MoviePresentation {
    std::string_view owner;
    const std::uint8_t* rgba;
    int width;
    int height;
    std::uint32_t version;
  };

  lua_State* lua;
  openwow::game::WorldSession* session;
  const openwow::vfs::VirtualFileSystem* vfs;
  const MinimapSurfaceSubmitter& minimap_surface_submitter;
  MoviePresentation movie;
  UiRenderTelemetry telemetry;
  std::uint8_t view_id;
  std::uint64_t generation;
  std::uint8_t offscreen_view_begin;
  std::uint8_t offscreen_view_count;
};

class UiCompositor final {
 public:
  struct Dependencies {
    FrameTraversalIndex& traversal;
    FrameStore& frames;
    RetainedLayout& layout;
    FrameInputRouter& input;
    UiRenderResources& resources;
    openwow::render::TextureManager& textures;
    openwow::render::m2::M2System& m2;
    openwow::ui::WorldMapSystem& world_map;
  };

  explicit UiCompositor(Dependencies dependencies) noexcept;
  void Render(const UiCompositorFrame& frame);
  void SetDebugSubmissionReceiptsEnabled(bool enabled);
  [[nodiscard]] bool WasSubmittedLastFrame(std::string_view key) const;
  [[nodiscard]] std::uint64_t last_generation() const noexcept {
    return last_generation_;
  }

 private:
  [[nodiscard]] float screen_width() const noexcept;
  [[nodiscard]] float screen_height() const noexcept;
  [[nodiscard]] float root_scale() const noexcept;
  [[nodiscard]] std::uint8_t ComputeParentAlphaByte(
      FrameStore::FrameHandle parent_handle) const;

  [[nodiscard]] std::uint8_t ResolveFrameAlphaByte(
      FrameStore::FrameHandle handle,
      const openwow::ui::framexml::UiFrame& frame) const;

  FrameTraversalIndex& frame_traversal_index_;
  FrameStore& frame_store_;
  RetainedLayout& retained_layout_;
  FrameInputRouter& frame_input_router_;
  UiRenderResources* render_resources_;
  openwow::render::TextureManager& texture_manager_;
  openwow::render::m2::M2System& m2_system_;
  openwow::ui::WorldMapSystem& world_map_;
  TextureRegionRenderer texture_regions_;
  TextRegionRenderer text_regions_;
  ModelRegionRenderer model_regions_;
  std::unordered_set<std::string> submitted_keys_;
  std::uint64_t last_generation_{0U};
  bool debug_submission_receipts_enabled_{false};

  mutable lua_State* alpha_source_lua_{nullptr};
  mutable std::unordered_map<FrameStore::FrameHandle, std::uint8_t>
      parent_alpha_memo_;

  std::deque<UiTextureInfo> pass_textures_;

  struct PassTextureSlot {
    std::uint64_t pass;
    std::size_t index;
  };
  std::unordered_map<std::string, PassTextureSlot> ui_texture_pass_memo_;
  std::uint64_t texture_pass_generation_{0U};

  [[nodiscard]] const UiTextureInfo* ResolvePassTexture(const std::string& path);

  openwow::render::BgfxTextKey text_key_scratch_;
  openwow::ui::game::lua_projection::TextureRenderState texture_state_scratch_;
};

}
}
