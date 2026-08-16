#pragma once

#include "openwow/ui/glue/glue_streaming_counters.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::media {
class MoviePlayer;
}

namespace openwow::ui {
class TextureNaturalSizeSource;
}

namespace openwow::ui::glue {
class GlueCharSelectScene;
class GlueFontRegistry;
class GlueWidgetRuntime;
}

namespace openwow::render {
class RenderSubmitTrace;
class TextureManager;
namespace m2 {
class M2System;
}
}

namespace openwow::render::api {
class FrameGraph;
class RendererContext;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::client {

struct GlueModelPassPlanItem {
  std::string widget_name;
  bool use_post_process{false};
  float glow{0.0f};
  std::optional<float> death_effect_alpha;
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

struct GlueRenderSettings {
  bool effects_enabled{true};
  bool glow_enabled{true};
  bool death_effect_enabled{true};
  bool rectangle_textures{true};
  bool widescreen{true};
  float particle_density{1.0f};
};

[[nodiscard]] std::vector<GlueModelPassPlanItem> BuildGlueModelPassPlan(
    openwow::ui::glue::GlueWidgetRuntime& widgets, int width, int height,
    const GlueRenderSettings& settings);

class GlueBgfxRenderer {
 public:
  using SoundKitSink = std::function<void(std::uint32_t)>;

  GlueBgfxRenderer(const openwow::vfs::VirtualFileSystem* vfs,
                   openwow::render::TextureManager& texture_manager,
                   openwow::render::m2::M2System& m2_system,
                   SoundKitSink sound_kit_sink);
  ~GlueBgfxRenderer();

  GlueBgfxRenderer(const GlueBgfxRenderer&) = delete;
  GlueBgfxRenderer& operator=(const GlueBgfxRenderer&) = delete;

  bool Init(const openwow::render::api::RendererContext* renderer_context);
  void Shutdown();

  void BindFontRegistry(const openwow::ui::glue::GlueFontRegistry* registry);
  void BindRenderSubmitTrace(
      std::optional<std::reference_wrapper<openwow::render::RenderSubmitTrace>> trace,
      std::optional<std::filesystem::path> output_path);
  void SetDbcLoader(const openwow::data::dbc::DbcLoader* dbc_loader);

  void BindAttachedCharacterScene(openwow::ui::glue::GlueCharSelectScene* scene,
                                  std::string model_frame_widget_name);
  void BindAttachedCharacterScenes(openwow::ui::glue::GlueCharSelectScene* select_scene,
                                   std::string select_model_frame_widget_name,
                                   openwow::ui::glue::GlueCharSelectScene* create_scene,
                                   std::string create_model_frame_widget_name);

  void SetMoviePlayer(const openwow::media::MoviePlayer* player);

  void RenderGlue(openwow::ui::glue::GlueWidgetRuntime& widgets,
                  openwow::render::api::FrameGraph& frame_graph,
                  int width,
                  int height,
                  std::uint32_t now_ms,
                  std::uint32_t delta_ms,
                  const GlueRenderSettings& settings);

  [[nodiscard]] std::optional<int> ResolveEditBoxCursorByteAtPixel(
      const openwow::ui::glue::GlueWidgetRuntime& widgets,
      const std::string& editbox_name,
      int mouse_x);

  void PrewarmTextures(const openwow::ui::glue::GlueWidgetRuntime& widgets);

  void PrewarmModels(const openwow::ui::glue::GlueWidgetRuntime& widgets);

  void TickStreaming(openwow::ui::glue::GlueWidgetRuntime& widgets,
                     std::uint32_t step_budget = 4);
  [[nodiscard]] openwow::ui::glue::GlueStreamingCounters StreamingCounters() const;

  [[nodiscard]] openwow::ui::TextureNaturalSizeSource& texture_natural_size_source();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
