#include "openwow/render/glue/glue_bgfx_renderer.h"
#include "openwow/render/resources/fonts/font_string_flags.h"

#include "openwow/render/glue/glue_frame_timing.h"
#include "openwow/render/glue/glue_model_renderer.h"

#include "openwow/media/playback/movie_player.h"
#include "openwow/data/formats/blp/texture_path.h"
#include "openwow/render/api/frame_graph.h"
#include "openwow/render/backend/bgfx/bgfx_renderer_context.h"
#include "openwow/render/backend/bgfx/bgfx_text_cache.h"
#include "openwow/render/glue/glue_texture_stream.h"
#include "openwow/ui/texture_natural_size.h"
#include "openwow/render/effects/postprocess/post_process.h"
#include "openwow/ui/framexml/backdrop_render_utils.h"
#include "openwow/ui/glue/editbox_text_layout.h"
#include "openwow/ui/glue/glue_font_metrics.h"
#include "openwow/ui/glue/glue_font_registry.h"
#include "openwow/ui/glue/glue_streaming_counters.h"
#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/ui/ui_renderer.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/font_string_justification.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/game/loading_screen_progress_bar.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <bx/math.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::client {

using openwow::data::blp::NormalizeTexturePath;
using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

constexpr float kStandardLoadingScreenAspectRatio = 4.0f / 3.0f;
constexpr float kWideLoadingScreenAspectRatio = 16.0f / 10.0f;
constexpr float kWideLoadingScreenAspectRatioThreshold = 0.001f;

struct GlueFrameViews {
  std::uint16_t clear_view = 0;
  std::uint16_t next_model_view = 0;
  std::uint16_t model_view_end = 0;
  std::uint16_t ui_view = 0;
  std::uint16_t present_view = 0;
};

std::uint16_t ViewRangeEnd(const openwow::render::api::FrameGraphPass& pass) {
  const auto end = static_cast<std::uint32_t>(pass.view_id) + pass.view_count;
  return static_cast<std::uint16_t>(
      std::min<std::uint32_t>(end, std::numeric_limits<std::uint16_t>::max()));
}

GlueFrameViews BuildGlueFrameGraph(openwow::render::api::FrameGraph& frame_graph,
                                   const openwow::render::api::RenderExtent extent,
                                   const std::size_t ordered_render_view_count) {
  using openwow::render::api::FrameGraphPassId;

  const auto model_view_count = static_cast<std::uint16_t>(
      std::clamp<std::size_t>(ordered_render_view_count + 1,
                              1,
                              std::numeric_limits<std::uint16_t>::max()));

  frame_graph.Reset();
  const auto scene = frame_graph.AddPass(FrameGraphPassId::SceneOpaque, extent, model_view_count);
  frame_graph.AddPass(FrameGraphPassId::PostProcess, extent);
  const auto ui = frame_graph.AddPass(FrameGraphPassId::WorldUi, extent);
  frame_graph.AddPass(FrameGraphPassId::DebugOverlay, extent);
  const auto present = frame_graph.AddPass(FrameGraphPassId::Present, extent);

  GlueFrameViews views;
  views.clear_view = scene.view_id;
  views.next_model_view = static_cast<std::uint16_t>(scene.view_id + 1);
  views.model_view_end = ViewRangeEnd(scene);
  views.ui_view = ui.view_id;
  views.present_view = present.view_id;
  return views;
}

std::optional<std::uint16_t> TakeView(std::uint16_t& next_view, const std::uint16_t end_view) {
  if (next_view >= end_view) {
    return std::nullopt;
  }
  return next_view++;
}

std::size_t GlueModelViewBudgetForPass(const GlueModelPassPlanItem& pass) {
  return 1u + (pass.use_post_process ? openwow::render::PostProcess::kMaxViews : 0u);
}

std::size_t OrderedGlueModelViewBudget(const std::vector<GlueModelPassPlanItem>& plan) {
  std::size_t count = 0;
  for (const auto& pass : plan) {
    count += GlueModelViewBudgetForPass(pass);
  }
  return count;
}

bool HasModelPostProcessViewBudget(const GlueFrameViews& views) {
  return static_cast<std::uint32_t>(views.next_model_view) +
             1u + openwow::render::PostProcess::kMaxViews <=
         views.model_view_end;
}

std::uint32_t PackAbgrPremul(float r, float g, float b, float a) {
  const float ca = std::clamp(a, 0.0f, 1.0f);
  const auto to8 = [](float v) -> std::uint8_t {
    v = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
  };
  const std::uint8_t A = to8(ca);
  const std::uint8_t R = to8(r * ca);
  const std::uint8_t G = to8(g * ca);
  const std::uint8_t B = to8(b * ca);
  return (static_cast<std::uint32_t>(A) << 24) | (static_cast<std::uint32_t>(B) << 16)
         | (static_cast<std::uint32_t>(G) << 8) | static_cast<std::uint32_t>(R);
}

std::uint32_t PackAbgrStraight(float r, float g, float b, float a) {
  const auto to8 = [](float v) -> std::uint8_t {
    v = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
  };
  return (static_cast<std::uint32_t>(to8(a)) << 24) | (static_cast<std::uint32_t>(to8(b)) << 16)
         | (static_cast<std::uint32_t>(to8(g)) << 8) | static_cast<std::uint32_t>(to8(r));
}

openwow::render::ui::BlendMode ResolveGlueTextureBlend(
    const std::string_view mode) noexcept {
  if (mode == "DISABLE") return openwow::render::ui::BlendMode::kOpaque;
  if (mode == "ALPHAKEY") return openwow::render::ui::BlendMode::kAlphaKey;
  if (mode == "ADD") return openwow::render::ui::BlendMode::kAdditive;
  if (mode == "MOD") return openwow::render::ui::BlendMode::kModulate;
  return openwow::render::ui::BlendMode::kAlpha;
}

struct ResolvedEditBoxTextState {
  openwow::ui::glue::GlueWidgetState editbox;
  openwow::ui::glue::GlueWidgetState text_widget;
  openwow::ui::glue::GlueFontStyle font_style;
  std::string display_text;
  std::string real_text;
  std::string visible_display_text;
  int scaled_height_px{0};
  int visible_width_px{0};
  int text_area_width_px{0};
  int visible_start_codepoints{0};
  int visible_codepoints{0};
  int visible_start_real_byte{0};
  int visible_end_real_byte{0};
  int aligned_text_x{0};
  int aligned_text_y{0};
  int presented_editbox_x{0};
  int presented_editbox_y{0};
  openwow::ui::glue::GlueWidgetClipRect presented_text_rect{};

  float render_scale{1.0F};
};

std::optional<ResolvedEditBoxTextState> ResolveEditBoxTextState(
    const openwow::ui::glue::GlueWidgetRuntime& widgets,
    const std::string& editbox_name,
    const openwow::ui::glue::GlueFontRegistry* font_registry,
    openwow::render::BgfxTextCache& text_cache) {
  const auto editbox = widgets.GetResolvedWidget(editbox_name);
  if (!editbox.has_value() || ToLowerAscii(editbox->kind) != "editbox") {
    return std::nullopt;
  }

  const std::string text_region = widgets.TextRegionForWidget(editbox_name);

  const auto text_widget = widgets.GetResolvedWidget(text_region);
  if (!text_widget.has_value() || ToLowerAscii(text_widget->kind) != "fontstring") {
    return std::nullopt;
  }

  ResolvedEditBoxTextState state;
  state.editbox = *editbox;
  state.text_widget = *text_widget;
  state.font_style =
      openwow::ui::glue::ResolveGlueFontStringStyle(font_registry,
                                                     *text_widget)
          .font;
  if (state.font_style.justify_h.empty()) {
    state.font_style.justify_h = "LEFT";
  }
  if (state.font_style.justify_v.empty()) {
    state.font_style.justify_v = "CENTER";
  }
  state.display_text = text_widget->text;
  state.real_text = widgets.GetText(editbox_name);

  const auto coordinates =
      openwow::ui::glue::ResolveGlueFontCoordinateSpace(widgets, text_region);
  state.scaled_height_px = coordinates.RasterFontPixelHeight(
      static_cast<float>(state.font_style.height_px));
  state.render_scale = coordinates.render_scale();

  state.text_area_width_px = std::max(0, text_widget->width);

  const auto visible_window = openwow::ui::glue::ResolveEditBoxVisibleWindow(
      state.real_text, state.display_text, editbox->password,
      widgets.GetEditVisibleStartCodepoints(editbox_name),
      widgets.GetEditCursorByte(editbox_name),
      static_cast<float>(state.text_area_width_px),
      [&](std::string_view prefix) {
        return text_cache.MeasureLineWidthPx(state.font_style.font_file,
                                             state.scaled_height_px, prefix);
      });
  state.visible_start_codepoints = visible_window.start_visible_codepoints;
  state.visible_codepoints = visible_window.visible_codepoints;
  state.visible_start_real_byte = visible_window.start_real_byte;
  state.visible_end_real_byte = visible_window.end_real_byte;
  state.visible_display_text = state.display_text.substr(
      static_cast<std::size_t>(visible_window.start_display_byte),
      static_cast<std::size_t>(visible_window.end_display_byte
                               - visible_window.start_display_byte));
  state.visible_width_px = text_cache.MeasureLineWidthPx(
      state.font_style.font_file, state.scaled_height_px, state.visible_display_text);

  const auto justify = openwow::ui::glue::ResolveEditBoxCaretJustify(
      text_widget->justify_h, text_widget->justify_v, state.font_style.justify_h,
      state.font_style.justify_v);

  state.aligned_text_x = text_widget->x;
  state.aligned_text_y = text_widget->y;
  if (state.text_area_width_px > 0) {
    if (justify.horizontal == "CENTER") {
      state.aligned_text_x =
          text_widget->x + (state.text_area_width_px - state.visible_width_px) / 2;
    } else if (justify.horizontal == "RIGHT") {
      state.aligned_text_x =
          text_widget->x + state.text_area_width_px - state.visible_width_px;
    }
  }
  if (text_widget->height > 0) {
    if (justify.vertical == "CENTER") {
      state.aligned_text_y =
          text_widget->y + (text_widget->height - state.scaled_height_px) / 2;
    } else if (justify.vertical == "BOTTOM") {
      state.aligned_text_y =
          text_widget->y + text_widget->height - state.scaled_height_px;
    }
  }

  const auto presentation = widgets.ResolveScrollPresentation(*text_widget);
  state.aligned_text_x += presentation.offset_x;
  state.aligned_text_y += presentation.offset_y;
  state.presented_editbox_x = editbox->x + presentation.offset_x;
  state.presented_editbox_y = editbox->y + presentation.offset_y;
  state.presented_text_rect = {
      text_widget->x + presentation.offset_x,
      text_widget->y + presentation.offset_y,
      text_widget->width,
      text_widget->height,
  };

  return state;
}

void InsetUVHalfTexel(float& u0, float& v0, float& u1, float& v1,
                     float tex_width, float tex_height) {
  const float half_u = 0.5f / tex_width;
  const float half_v = 0.5f / tex_height;
  u0 += half_u;
  v0 += half_v;
  u1 -= half_u;
  v1 -= half_v;
}

using GluePixelRect = openwow::ui::glue::GlueWidgetClipRect;

std::optional<GluePixelRect> IntersectGlueRects(const GluePixelRect& lhs,
                                                const GluePixelRect& rhs) {
  const int left = std::max(lhs.x, rhs.x);
  const int top = std::max(lhs.y, rhs.y);
  const int right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const int bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  if (right <= left || bottom <= top) {
    return std::nullopt;
  }
  return GluePixelRect{left, top, right - left, bottom - top};
}

class ScopedGlueScissor final {
 public:
  ScopedGlueScissor(openwow::render::ui::UiRenderer* renderer,
                    bool* externally_active,
                    const std::optional<GluePixelRect>& clip)
      : renderer_(renderer), externally_active_(externally_active) {
    if (renderer_ == nullptr || externally_active_ == nullptr ||
        !clip.has_value()) {
      return;
    }
    renderer_->SetScissor(clip->x, clip->y, clip->width, clip->height);
    *externally_active_ = true;
    active_ = true;
  }

  ~ScopedGlueScissor() {
    if (!active_) {
      return;
    }
    renderer_->ClearScissor();
    *externally_active_ = false;
  }

  ScopedGlueScissor(const ScopedGlueScissor&) = delete;
  ScopedGlueScissor& operator=(const ScopedGlueScissor&) = delete;

 private:
  openwow::render::ui::UiRenderer* renderer_{nullptr};
  bool* externally_active_{nullptr};
  bool active_{false};
};

}

std::vector<GlueModelPassPlanItem> BuildGlueModelPassPlan(
    openwow::ui::glue::GlueWidgetRuntime& widgets,
    const std::vector<openwow::ui::glue::GlueWidgetState>& visible_widgets,
    int width,
    int height,
    const GlueRenderSettings& settings) {
  std::vector<GlueModelPassPlanItem> plan;
  for (const auto& widget : visible_widgets) {
    const std::string kind = ToLowerAscii(widget.kind);
    if ((kind != "model" && kind != "modelffx") || widget.model_file.empty()) {
      continue;
    }
    if (widgets.EffectiveAlpha(widget.name) <= 0.0f) {
      continue;
    }

    const int model_x = (widget.width > 0) ? widget.x : 0;
    const int model_y = (widget.height > 0) ? widget.y : 0;
    const int model_width = (widget.width > 0) ? widget.width : width;
    const int model_height = (widget.height > 0) ? widget.height : height;
    if (!ResolveGlueModelViewRect(model_x, model_y, model_width, model_height, width, height)
             .has_value()) {
      continue;
    }

    plan.push_back(GlueModelPassPlanItem{
        .widget_name = widget.name,
        .use_post_process = settings.effects_enabled &&
                            settings.glow_enabled && kind == "modelffx" &&
                            widgets.GetGlow(widget.name) > 0.0f,
        .glow = widgets.GetGlow(widget.name),
        .x = model_x,
        .y = model_y,
        .width = model_width,
        .height = model_height,
    });
  }
  return plan;
}

std::vector<GlueModelPassPlanItem> BuildGlueModelPassPlan(
    openwow::ui::glue::GlueWidgetRuntime& widgets, const int width,
    const int height, const GlueRenderSettings& settings) {
  return BuildGlueModelPassPlan(
      widgets, widgets.VisibleWidgetsInRenderOrder(), width, height,
      settings);
}

struct GlueTextureStreamNaturalSizeSource final
    : public openwow::ui::TextureNaturalSizeSource {
  explicit GlueTextureStreamNaturalSizeSource(
      openwow::render::GlueTextureStream& stream)
      : stream(stream) {}

  void QueueTextureLoad(const std::string& texture_path) override {
    if (texture_path.empty()) return;
    stream.QueueAsyncLoad(openwow::data::blp::ResolveNormalizedTexturePath(
        texture_path, normalized_path_cache));
  }

  [[nodiscard]] std::optional<openwow::ui::TexturePixelSize>
  ResolveTexturePixelSize(const std::string& texture_path) override {
    if (texture_path.empty()) return std::nullopt;
    const auto dimensions = stream.PeekTextureDimensions(
        openwow::data::blp::ResolveNormalizedTexturePath(
            texture_path, normalized_path_cache));
    if (!dimensions.has_value()) return std::nullopt;
    return openwow::ui::TexturePixelSize{
        .width = static_cast<std::uint32_t>(std::max(0, dimensions->first)),
        .height = static_cast<std::uint32_t>(std::max(0, dimensions->second)),
    };
  }

  openwow::render::GlueTextureStream& stream;

  std::unordered_map<std::string, std::string> normalized_path_cache;
};

struct GlueBgfxRenderer::Impl {
  Impl(const openwow::vfs::VirtualFileSystem* vfs,
       openwow::render::TextureManager& texture_manager,
       openwow::render::m2::M2System& m2_system,
       GlueBgfxRenderer::SoundKitSink sound_kit_sink)
      : textures(openwow::render::GlueTextureStreamConfig{
            .vfs = vfs,
            .async_worker_count = 4,
        }),
        natural_size_source(textures),
        text(vfs),
        m2_system(m2_system),
        models(vfs, texture_manager, m2_system,
               std::move(sound_kit_sink)) {}

  std::optional<openwow::render::ui::UiRenderer> ui;
  openwow::render::GlueTextureStream textures;
  GlueTextureStreamNaturalSizeSource natural_size_source;
  openwow::render::BgfxTextCache text;
  openwow::render::m2::M2System& m2_system;
  GlueModelRenderer models;
  std::vector<openwow::render::ui::MeshVertex> text_mesh_vertices;
  std::vector<std::uint16_t> text_mesh_indices;
  const openwow::ui::glue::GlueFontRegistry* font_registry{nullptr};
  std::optional<std::reference_wrapper<openwow::render::RenderSubmitTrace>> submit_trace;
  std::optional<std::filesystem::path> submit_trace_path;
  std::uint32_t submit_trace_frame{0};
  std::uint32_t submit_trace_idx{0};
  bool submit_trace_written{false};
  std::uint16_t white_texture_index{bgfx::kInvalidHandle};
  const openwow::media::MoviePlayer* movie_player{nullptr};
  bool texture_prewarm_queued{false};
  std::unordered_map<std::uint64_t, std::unique_ptr<openwow::render::PostProcess>>
      post_process_by_extent;
  GlueFrameDeltaState frame_delta_state;
  bool ok{false};

  [[nodiscard]] static std::uint64_t PostProcessExtentKey(std::uint32_t width,
                                                          std::uint32_t height) {
    return (static_cast<std::uint64_t>(width) << 32u) | static_cast<std::uint64_t>(height);
  }

  [[nodiscard]] openwow::render::PostProcess* ResolvePostProcess(
      std::uint32_t width, std::uint32_t height,
      const GlueRenderSettings& settings) {
    const openwow::render::PostProcessSettings post_process_settings{
        .enabled = settings.effects_enabled,
        .glow_enabled = settings.glow_enabled,
        .death_enabled = settings.death_effect_enabled,
        .rectangle_textures = settings.rectangle_textures,
    };
    const std::uint64_t key = PostProcessExtentKey(width, height);
    if (const auto it = post_process_by_extent.find(key); it != post_process_by_extent.end()) {
      it->second->SetSettings(post_process_settings);
      return it->second.get();
    }

    auto post_process = std::make_unique<openwow::render::PostProcess>();
    post_process->Init(width, height, post_process_settings);
    post_process->InitGPU();
    auto* result = post_process.get();
    post_process_by_extent.emplace(key, std::move(post_process));
    return result;
  }

  void ClearPostProcessCache() {
    for (auto& [_, post_process] : post_process_by_extent) {
      if (post_process) {
        post_process->Shutdown();
      }
    }
    post_process_by_extent.clear();
  }
};

GlueBgfxRenderer::GlueBgfxRenderer(
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::TextureManager& texture_manager,
    openwow::render::m2::M2System& m2_system,
    SoundKitSink sound_kit_sink)
    : impl_(std::make_unique<Impl>(
          vfs, texture_manager, m2_system, std::move(sound_kit_sink))) {}

GlueBgfxRenderer::~GlueBgfxRenderer() {
  Shutdown();
}

bool GlueBgfxRenderer::Init(const openwow::render::api::RendererContext* renderer_context) {
  Shutdown();
  const bgfx::TextureHandle shared_white_texture =
      openwow::render::GetBgfxRendererContextWhiteTexture(renderer_context);
  if (!bgfx::isValid(shared_white_texture)) {
    return false;
  }

  impl_->ui.emplace();
  if (!impl_->ui->Init()) {
    impl_->ui.reset();
    return false;
  }

  impl_->white_texture_index = shared_white_texture.idx;
  impl_->models.SetSharedWhiteTexture(shared_white_texture);
  impl_->models.StartStreaming();

  impl_->ok = true;
  return true;
}

void GlueBgfxRenderer::Shutdown() {
  if (impl_->submit_trace.has_value() && !impl_->submit_trace_written &&
      impl_->submit_trace_path.has_value()) {
    (void)impl_->submit_trace->get().WriteTsvFile(*impl_->submit_trace_path);
    impl_->submit_trace_written = true;
  }
  impl_->models.BindRenderSubmitTrace(std::nullopt);
  impl_->ClearPostProcessCache();
  impl_->models.Shutdown();
  impl_->textures.Shutdown();
  impl_->text.Shutdown();
  if (impl_->ui.has_value()) {
    impl_->ui->Shutdown();
    impl_->ui.reset();
  }
  impl_->white_texture_index = bgfx::kInvalidHandle;
  impl_->models.SetSharedWhiteTexture(BGFX_INVALID_HANDLE);
  impl_->font_registry = nullptr;
  impl_->movie_player = nullptr;
  impl_->texture_prewarm_queued = false;
  impl_->submit_trace.reset();
  impl_->submit_trace_path.reset();
  impl_->submit_trace_frame = 0;
  impl_->submit_trace_idx = 0;
  impl_->submit_trace_written = false;
  impl_->frame_delta_state = {};
  impl_->ok = false;
}

void GlueBgfxRenderer::BindFontRegistry(const openwow::ui::glue::GlueFontRegistry* registry) {
  impl_->font_registry = registry;
}

void GlueBgfxRenderer::SetDbcLoader(const openwow::data::dbc::DbcLoader* dbc_loader) {
  impl_->models.SetDbcLoader(dbc_loader);
}

void GlueBgfxRenderer::BindRenderSubmitTrace(
    std::optional<std::reference_wrapper<openwow::render::RenderSubmitTrace>> trace,
    std::optional<std::filesystem::path> output_path) {
  impl_->submit_trace = trace;
  impl_->submit_trace_path = std::move(output_path);
  impl_->submit_trace_frame = 0;
  impl_->submit_trace_idx = 0;
  impl_->submit_trace_written = false;
  if (impl_->submit_trace.has_value()) {
    impl_->models.BindRenderSubmitTrace(openwow::render::RenderSubmitTraceBinding{
        .trace = impl_->submit_trace->get(),
        .frame = impl_->submit_trace_frame,
        .index = impl_->submit_trace_idx,
    });
  } else {
    impl_->models.BindRenderSubmitTrace(std::nullopt);
  }
}

void GlueBgfxRenderer::BindAttachedCharacterScene(openwow::ui::glue::GlueCharSelectScene* scene,
                                                  std::string model_frame_widget_name) {
  impl_->models.BindAttachedCharacterScene(scene, std::move(model_frame_widget_name));
}

void GlueBgfxRenderer::BindAttachedCharacterScenes(
    openwow::ui::glue::GlueCharSelectScene* select_scene,
    std::string select_model_frame_widget_name,
    openwow::ui::glue::GlueCharSelectScene* create_scene,
    std::string create_model_frame_widget_name) {
  impl_->models.BindAttachedCharacterScenes(select_scene,
                                            std::move(select_model_frame_widget_name),
                                            create_scene,
                                            std::move(create_model_frame_widget_name));
}

void GlueBgfxRenderer::SetMoviePlayer(const openwow::media::MoviePlayer* player) {
  impl_->movie_player = player;
}

std::optional<int> GlueBgfxRenderer::ResolveEditBoxCursorByteAtPixel(
    const openwow::ui::glue::GlueWidgetRuntime& widgets,
    const std::string& editbox_name,
    int mouse_x) {
  const auto text_state =
      ResolveEditBoxTextState(widgets, editbox_name, impl_->font_registry, impl_->text);
  if (!text_state.has_value()) {
    return std::nullopt;
  }

  const float local_width = std::clamp(
      static_cast<float>(mouse_x - text_state->aligned_text_x),
      0.0f, static_cast<float>(text_state->visible_width_px));

  const int byte_index = openwow::ui::glue::ResolveEditBoxCursorByteFromDisplayWidthPx(
      std::string_view(text_state->real_text).substr(
          static_cast<std::size_t>(text_state->visible_start_real_byte),
          static_cast<std::size_t>(text_state->visible_end_real_byte
                                   - text_state->visible_start_real_byte)),
      text_state->visible_display_text, text_state->editbox.password, local_width,
      [&](std::string_view prefix) {
        return impl_->text.MeasureLineWidthPx(text_state->font_style.font_file,
                                        text_state->scaled_height_px, prefix);
      });
  return std::clamp(text_state->visible_start_real_byte + byte_index, 0,
                    static_cast<int>(text_state->real_text.size()));
}

void GlueBgfxRenderer::PrewarmTextures(
    const openwow::ui::glue::GlueWidgetRuntime& widgets) {

  const auto queue_prefetch = [this](const std::string& texture_path) {
    if (!texture_path.empty()) {
      impl_->textures.QueueAsyncLoad(NormalizeTexturePath(texture_path));
    }
  };
  if (!impl_->texture_prewarm_queued) {
    for (const auto& name : widgets.WidgetNamesInRegistrationOrder()) {
      if (const auto widget = widgets.GetWidget(name); widget.has_value()) {
        queue_prefetch(widget->texture_file);
        if (widget->status_bar.has_value()) {
          queue_prefetch(widget->status_bar->texture_path);
        }
        queue_prefetch(widget->color_wheel_texture_file);
        queue_prefetch(widget->color_wheel_thumb_texture_file);
        queue_prefetch(widget->color_value_texture_file);
        queue_prefetch(widget->color_value_thumb_texture_file);
      }
    }
    queue_prefetch(openwow::ui::game::kLoadingBarFillTexturePath);
    queue_prefetch(openwow::ui::game::kLoadingBarBorderTexturePath);
    impl_->texture_prewarm_queued = true;
  }

  for (const auto& widget : widgets.VisibleWidgetsInRenderOrder()) {
    if (!widget.texture_file.empty()) {
      (void)impl_->textures.LoadAsync(NormalizeTexturePath(widget.texture_file));
    }
  }
}

void GlueBgfxRenderer::PrewarmModels(
    const openwow::ui::glue::GlueWidgetRuntime& widgets) {
  impl_->models.PrewarmModels(widgets);
}

void GlueBgfxRenderer::TickStreaming(openwow::ui::glue::GlueWidgetRuntime& widgets,
                                     std::uint32_t step_budget) {
  impl_->models.PrewarmModels(widgets);
  if (!impl_->texture_prewarm_queued) PrewarmTextures(widgets);
  impl_->models.TickStreaming(widgets, step_budget);
}

openwow::ui::glue::GlueStreamingCounters GlueBgfxRenderer::StreamingCounters() const {
  return impl_->models.StreamingCounters();
}

openwow::ui::TextureNaturalSizeSource&
GlueBgfxRenderer::texture_natural_size_source() {
  return impl_->natural_size_source;
}

void GlueBgfxRenderer::RenderGlue(openwow::ui::glue::GlueWidgetRuntime& widgets,
                                  openwow::render::api::FrameGraph& frame_graph,
                                  int width,
                                  int height,
                                  std::uint32_t now_ms,
                                  std::uint32_t delta_ms,
                                  const GlueRenderSettings& settings) {
  if (!impl_->ok || !impl_->ui.has_value() || width <= 0 || height <= 0) {
    return;
  }
  impl_->m2_system.SetParticleDensity(settings.particle_density);
  impl_->textures.BeginFrame();

  (void)impl_->textures.PumpPreparedUploads(128);
  const std::uint32_t resolved_delta_ms =
      ResolveGlueFrameDeltaMs(impl_->frame_delta_state, now_ms, delta_ms);

  const bgfx::TextureHandle white_texture{impl_->white_texture_index};
  const std::uint32_t frame_idx = impl_->submit_trace_frame;
  bool scissor_active = false;
  auto ui_state_mask = [&](openwow::render::ui::BlendMode blend, bool rotate_90) -> std::uint32_t {
    std::uint32_t mask = 0;
    switch (blend) {
      case openwow::render::ui::BlendMode::kCoveragePremultiplied: mask |= 1u << 0; break;
      case openwow::render::ui::BlendMode::kAdditive: mask |= 1u << 1; break;
      case openwow::render::ui::BlendMode::kOpaque: mask |= 1u << 4; break;
      case openwow::render::ui::BlendMode::kAlphaKey: mask |= 1u << 5; break;
      case openwow::render::ui::BlendMode::kAlpha: mask |= 1u << 6; break;
      case openwow::render::ui::BlendMode::kModulate: mask |= 1u << 7; break;
      default: break;
    }
    if (rotate_90) mask |= 1u << 2;
    if (scissor_active) mask |= 1u << 3;
    return mask;
  };

  auto record_submit = [&](std::uint16_t view,
                           std::string pass,
                           std::string pipeline,
                           std::uint32_t state_mask,
                           std::string mesh,
                           std::string material) {
    if (!impl_->submit_trace.has_value()) return;
    openwow::render::RenderSubmitTraceEvent e;
    e.idx = impl_->submit_trace_idx++;
    e.frame = frame_idx;
    e.view = view;
    e.pass = std::move(pass);
    e.pipeline = std::move(pipeline);
    e.state_mask = state_mask;
    e.mesh = std::move(mesh);
    e.material = std::move(material);
    impl_->submit_trace->get().Add(std::move(e));
  };

  const auto& visible_widgets = widgets.VisibleWidgetsInRenderOrder();

  impl_->models.PruneWidgetInstances(widgets);
  impl_->models.BeginAnimationFrame(widgets, resolved_delta_ms);
  if (widgets.ConsumeModelFFXViewportDirty()) {
    impl_->ClearPostProcessCache();
  }
  auto model_pass_plan = BuildGlueModelPassPlan(
      widgets, visible_widgets, width, height, settings);
  for (auto &pass : model_pass_plan) {
    if (settings.effects_enabled && settings.death_effect_enabled) {
      pass.death_effect_alpha =
          impl_->models.AttachedSceneDeathEffectAlpha(pass.widget_name);
    }
    pass.use_post_process = pass.use_post_process || pass.death_effect_alpha.has_value();
  }
  auto frame_views = BuildGlueFrameGraph(
      frame_graph,
      openwow::render::api::RenderExtent{static_cast<std::uint32_t>(width),
                                         static_cast<std::uint32_t>(height)},
      OrderedGlueModelViewBudget(model_pass_plan) + model_pass_plan.size() + 1u);

  bgfx::setViewRect(frame_views.clear_view, 0, 0,
                    static_cast<std::uint16_t>(width),
                    static_cast<std::uint16_t>(height));
  bgfx::setViewClear(frame_views.clear_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     0x000000ff, 1.0f, 0);
  bgfx::touch(frame_views.clear_view);

  record_submit(frame_views.clear_view, "glue.frame", "models.render", 0, "-", "-");

  const auto render_model_pass = [&](const GlueModelPassPlanItem& pass) {
    const auto widget = widgets.GetResolvedWidget(pass.widget_name);
    if (!widget.has_value()) {
      return;
    }

    const auto view_w = static_cast<std::uint32_t>(pass.width);
    const auto view_h = static_cast<std::uint32_t>(pass.height);

    if (pass.use_post_process && HasModelPostProcessViewBudget(frame_views)) {
      auto* post_process =
          impl_->ResolvePostProcess(view_w, view_h, settings);
      const auto scene_fb = post_process->GetSceneFramebuffer();
      if (bgfx::isValid(scene_fb)) {
        const bool death_active = pass.death_effect_alpha.has_value();
        post_process->SetDeathEffectImmediate(
            death_active, pass.death_effect_alpha.value_or(0.0f));
        post_process->SetGlowEffect(!death_active, pass.glow);
        const auto model_view = TakeView(frame_views.next_model_view, frame_views.model_view_end);
        if (!model_view.has_value()) {
          return;
        }
        if (impl_->models.RenderWidget(
                widgets,
                *widget,
                *model_view,
                static_cast<int>(view_w),
                static_cast<int>(view_h),
                GlueModelRenderer::RenderViewOptions{
                    .framebuffer = scene_fb,
                    .clear_color = true,
                    .clear_color_rgba = 0x00000000u,
                    .render_at_origin = true,
                })) {
          frame_views.next_model_view = post_process->ApplyToRect(
              static_cast<bgfx::ViewId>(frame_views.next_model_view),
              openwow::render::PostProcess::Rect{
                  .x = pass.x,
                  .y = pass.y,
                  .width = pass.width,
                  .height = pass.height,
                  .output_width = width,
                  .output_height = height,
              });
          return;
        }
      }
    }

    const auto model_view = TakeView(frame_views.next_model_view, frame_views.model_view_end);
    if (!model_view.has_value()) {
      return;
    }
    (void)impl_->models.RenderWidget(widgets,
                               *widget,
                               *model_view,
                               width,
                               height,
                               GlueModelRenderer::RenderViewOptions{});
  };

  std::unordered_map<std::string, const GlueModelPassPlanItem*> model_pass_by_widget;
  model_pass_by_widget.reserve(model_pass_plan.size());
  for (const auto& pass : model_pass_plan) {
    model_pass_by_widget.emplace(pass.widget_name, &pass);
  }

  const auto begin_ui_segment = [&]() -> bool {
    const auto ui_view = TakeView(frame_views.next_model_view, frame_views.model_view_end);
    if (!ui_view.has_value()) {
      return false;
    }
    frame_views.ui_view = *ui_view;
    bgfx::setViewClear(frame_views.ui_view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx::touch(frame_views.ui_view);
    impl_->ui->Begin(frame_views.ui_view, width, height);
    return true;
  };

  if (!begin_ui_segment()) {
    return;
  }

  const std::uint64_t linear_clamp =
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIP_POINT;
  const std::uint64_t linear_wrap_v = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_MIP_POINT;

  const std::string focused_editbox = widgets.focused_widget();

  enum class EditBoxDecorationPass : std::uint8_t { kSelection, kCaret };
  const auto submit_editbox_decorations = [&, this](
      const ResolvedEditBoxTextState& text_state,
      const std::optional<GluePixelRect>& outer_clip,
      const EditBoxDecorationPass pass) {
    const auto& editbox = text_state.editbox;

    if (focused_editbox.empty() || editbox.name != focused_editbox ||
        !widgets.IsVisible(editbox.name) ||
        !widgets.IsVisible(text_state.text_widget.name)) {
      return;
    }
    const float decoration_alpha =
        std::clamp(widgets.EffectiveAlpha(editbox.name), 0.0F, 1.0F);
    if (decoration_alpha <= 0.0F) {
      return;
    }
    const auto& resolved = text_state.font_style;
    const std::string& real_text = text_state.real_text;
    const int cursor_byte = std::clamp(
        widgets.GetEditCursorByte(focused_editbox), 0,
        std::max(0, static_cast<int>(real_text.size())));
    const int cursor_cps =
        openwow::ui::glue::VisibleCodepointCountBeforeByteOffset(
            real_text, cursor_byte, editbox.password);
    const int cursor_cps_in_window = std::clamp(
        cursor_cps - text_state.visible_start_codepoints,
        0, text_state.visible_codepoints);
    const std::string prefix =
        openwow::ui::glue::PipeSafeUtf8TakeCodepoints(
            text_state.visible_display_text, cursor_cps_in_window);
    const int caret_scaled_height = text_state.scaled_height_px;
    const int prefix_w = impl_->text.MeasureLineWidthPx(
        resolved.font_file, caret_scaled_height, prefix);

    const float conversion_factor = widgets.conversion_factor();
    const float caret_width_px =
        conversion_factor > 0.0F
            ? (openwow::ui::kRetailEditBoxCaretWidthUiUnits /
               conversion_factor) *
                  static_cast<float>(width)
            : 2.0F;
    const float caret_x = std::floor(
        static_cast<float>(text_state.aligned_text_x + prefix_w) + 0.5F);
    const float caret_y =
        std::floor(static_cast<float>(text_state.aligned_text_y) + 0.5F);
    const float caret_h =
        static_cast<float>(std::max(4, caret_scaled_height));
    std::optional<GluePixelRect> decoration_clip =
        text_state.presented_text_rect;
    if (outer_clip.has_value()) {
      decoration_clip = IntersectGlueRects(*decoration_clip, *outer_clip);
    }
    if (!decoration_clip.has_value()) {
      return;
    }

    const auto [sel_start_byte, sel_end_byte] =
        widgets.GetEditSelectionBytes(focused_editbox);
    if (pass == EditBoxDecorationPass::kSelection && sel_start_byte >= 0 &&
        sel_end_byte >= 0 && sel_start_byte != sel_end_byte) {
      const int s0 = std::min(sel_start_byte, sel_end_byte);
      const int s1 = std::max(sel_start_byte, sel_end_byte);
      const int s0_cps =
          openwow::ui::glue::VisibleCodepointCountBeforeByteOffset(
              real_text, s0, editbox.password);
      const int s1_cps =
          openwow::ui::glue::VisibleCodepointCountBeforeByteOffset(
              real_text, s1, editbox.password);
      const int vis_s0 =
          std::max(s0_cps, text_state.visible_start_codepoints);
      const int vis_s1 = std::min(
          s1_cps, text_state.visible_start_codepoints +
                      text_state.visible_codepoints);
      if (vis_s0 < vis_s1) {
        const std::string prefix_s0 =
            openwow::ui::glue::PipeSafeUtf8TakeCodepoints(
                text_state.visible_display_text,
                vis_s0 - text_state.visible_start_codepoints);
        const std::string prefix_s1 =
            openwow::ui::glue::PipeSafeUtf8TakeCodepoints(
                text_state.visible_display_text,
                vis_s1 - text_state.visible_start_codepoints);
        const int sel_x0_px = impl_->text.MeasureLineWidthPx(
            resolved.font_file, caret_scaled_height, prefix_s0);
        const int sel_x1_px = impl_->text.MeasureLineWidthPx(
            resolved.font_file, caret_scaled_height, prefix_s1);
        const float sel_left = std::floor(
            static_cast<float>(text_state.aligned_text_x + sel_x0_px) +
            0.5F);
        const float sel_right = std::floor(
            static_cast<float>(text_state.aligned_text_x + sel_x1_px) +
            0.5F);
        const float sel_width = sel_right - sel_left;
        if (sel_width > 0.0F) {
          impl_->ui->SetScissor(decoration_clip->x, decoration_clip->y,
                                decoration_clip->width,
                                decoration_clip->height);
          scissor_active = true;
          openwow::render::ui::Quad selection;
          selection.texture = white_texture;
          selection.x = sel_left;
          selection.y = caret_y;
          selection.w = sel_width;
          selection.h = caret_h;

          selection.abgr =
              PackAbgrPremul(openwow::ui::kRetailEditBoxHighlightColorChannel,
                             openwow::ui::kRetailEditBoxHighlightColorChannel,
                             openwow::ui::kRetailEditBoxHighlightColorChannel,
                             openwow::ui::kRetailEditBoxHighlightColorAlpha *
                                 decoration_alpha);
          selection.blend =
              openwow::render::ui::BlendMode::kCoveragePremultiplied;
          selection.sampler_flags = linear_clamp;
          record_submit(frame_views.ui_view, "glue.ui",
                        "ui.selection_highlight",
                        ui_state_mask(selection.blend, selection.rotate_90),
                        focused_editbox + ".selection", "solid:white");
          impl_->ui->Submit(selection);
          impl_->ui->ClearScissor();
          scissor_active = false;
        }
      }
    }

    if (pass == EditBoxDecorationPass::kSelection) {
      return;
    }

    const bool cursor_changed = widgets.ConsumeCursorChanged(focused_editbox);
    if (cursor_changed) {

      const float to_ui_units = text_state.render_scale > 0.0F
                                    ? 1.0F / text_state.render_scale
                                    : 1.0F;
      widgets.QueueCursorChangedEvent(
          focused_editbox,
          (caret_x - static_cast<float>(text_state.presented_editbox_x)) *
              to_ui_units,
          (caret_y - static_cast<float>(text_state.presented_editbox_y)) *
              to_ui_units,
          caret_width_px * to_ui_units, caret_h * to_ui_units);
    }
    widgets.UpdateCaretBlink(
        focused_editbox, static_cast<float>(resolved_delta_ms) / 1000.0F);
    if (!widgets.IsCaretVisible(focused_editbox)) {
      return;
    }

    impl_->ui->SetScissor(decoration_clip->x, decoration_clip->y,
                          decoration_clip->width, decoration_clip->height);
    scissor_active = true;
    openwow::render::ui::Quad caret;
    caret.texture = white_texture;
    caret.x = caret_x;
    caret.y = caret_y;
    caret.w = std::max(1.0F, std::round(caret_width_px));
    caret.h = caret_h;
    caret.abgr =
        PackAbgrPremul(1.0F, 1.0F, 1.0F, decoration_alpha);
    caret.blend = openwow::render::ui::BlendMode::kCoveragePremultiplied;
    caret.sampler_flags = linear_clamp;
    record_submit(frame_views.ui_view, "glue.ui", "ui.caret_quad",
                  ui_state_mask(caret.blend, caret.rotate_90),
                  focused_editbox + ".caret", "solid:white");
    impl_->ui->Submit(caret);
    impl_->ui->ClearScissor();
    scissor_active = false;
  };

  for (const auto& stored_widget : visible_widgets) {
    if (const auto model_pass = model_pass_by_widget.find(stored_widget.name);
        model_pass != model_pass_by_widget.end()) {
      impl_->ui->End();
      render_model_pass(*model_pass->second);
      if (!begin_ui_segment()) {
        return;
      }
      continue;
    }

    const float effective_alpha = widgets.EffectiveAlpha(stored_widget.name);
    if (effective_alpha <= 0.0f) {
      continue;
    }

    const auto presentation =
        widgets.ResolveScrollPresentation(stored_widget);
    if (presentation.clipped_out) {
      continue;
    }
    const auto& widget = presentation.widget;
    const std::string kind = ToLowerAscii(widget.kind);

    std::unique_ptr<ScopedGlueScissor> widget_scissor;
    if (kind != "fontstring" && presentation.clip.has_value()) {
      widget_scissor = std::make_unique<ScopedGlueScissor>(
          &*impl_->ui, &scissor_active, presentation.clip);
    }

    if (kind == "movieframe" && impl_->movie_player != nullptr
        && impl_->movie_player->IsPlaying()
        && impl_->movie_player->CurrentFrameRGBA() != nullptr
        && impl_->movie_player->FrameWidth() > 0
        && impl_->movie_player->FrameHeight() > 0) {
      auto tex = impl_->textures.UploadDynamic(
          "__movie_frame__",
          impl_->movie_player->CurrentFrameRGBA(),
          impl_->movie_player->FrameWidth(),
          impl_->movie_player->FrameHeight(),
          impl_->movie_player->FrameVersion());
      if (bgfx::isValid(tex)) {

        const float x0 = std::floor(static_cast<float>(widget.x) + 0.5f);
        const float y0 = std::floor(static_cast<float>(widget.y) + 0.5f);
        float dst_x = x0;
        float dst_y = y0;
        float dst_w = static_cast<float>(widget.width);
        float dst_h = static_cast<float>(widget.height);
        if (dst_w <= 0.0f || dst_h <= 0.0f) {
          dst_x = 0.0f;
          dst_y = 0.0f;
          dst_w = static_cast<float>(width);
          dst_h = static_cast<float>(height);
        }

        const float vw = static_cast<float>(impl_->movie_player->FrameWidth());
        const float vh = static_cast<float>(impl_->movie_player->FrameHeight());
        const float video_aspect = (vh > 0.0f) ? (vw / vh) : 1.0f;
        const float rect_aspect = (dst_h > 0.0f) ? (dst_w / dst_h) : video_aspect;
        float fit_x = dst_x;
        float fit_y = dst_y;
        float fit_w = dst_w;
        float fit_h = dst_h;
        if (rect_aspect > video_aspect) {
          fit_w = dst_h * video_aspect;
          fit_x = dst_x + (dst_w - fit_w) * 0.5f;
        } else if (rect_aspect < video_aspect) {
          fit_h = dst_w / video_aspect;
          fit_y = dst_y + (dst_h - fit_h) * 0.5f;
        }

        openwow::render::ui::Quad q;
        q.texture = tex;
        q.x = fit_x;
        q.y = fit_y;
        q.w = fit_w;
        q.h = fit_h;
        q.u0 = 0.0f;
        q.v0 = 0.0f;
        q.u1 = 1.0f;
        q.v1 = 1.0f;
        q.abgr = 0xFFFFFFFFu;
        q.blend = openwow::render::ui::BlendMode::kAlpha;
        q.sampler_flags = linear_clamp;
        q.rotation_radians = widget.animation_rotation_radians;
        q.rotation_origin_x = static_cast<float>(widget.x) + static_cast<float>(widget.width) * 0.5f;
        q.rotation_origin_y = static_cast<float>(widget.y) + static_cast<float>(widget.height) * 0.5f;
        q.has_rotation_origin = true;
        record_submit(frame_views.ui_view,
                      "glue.ui",
                      "ui.movie_quad",
                      ui_state_mask(q.blend, q.rotate_90),
                      widget.name,
                      "__movie_frame__");
        impl_->ui->Submit(q);
        continue;
      }

    }

    if ((kind == "texture" || kind == "movieframe") && !widget.texture_file.empty()) {
      const std::string path = NormalizeTexturePath(widget.texture_file);
      if (path.empty()) {
        continue;
      }
      const auto tex_opt = impl_->textures.LoadAsync(path);
      if (!tex_opt.has_value() || !bgfx::isValid(tex_opt->handle) || tex_opt->width <= 0
          || tex_opt->height <= 0) {
        continue;
      }

      const bool has_custom_uv_quad = !widget.tex_coords.IsAxisAlignedRect();
      const auto widget_uvs = widget.tex_coords.ToUiRendererOrder();
      const bool flip_x = widget.tex_right < widget.tex_left;
      const bool flip_y = widget.tex_bottom < widget.tex_top;
      const float base_left = std::min({widget.tex_coords.upper_left.u,
                                        widget.tex_coords.lower_left.u,
                                        widget.tex_coords.upper_right.u,
                                        widget.tex_coords.lower_right.u});
      const float base_right = std::max({widget.tex_coords.upper_left.u,
                                         widget.tex_coords.lower_left.u,
                                         widget.tex_coords.upper_right.u,
                                         widget.tex_coords.lower_right.u});
      const float base_top = std::min({widget.tex_coords.upper_left.v,
                                       widget.tex_coords.lower_left.v,
                                       widget.tex_coords.upper_right.v,
                                       widget.tex_coords.lower_right.v});
      const float base_bottom = std::max({widget.tex_coords.upper_left.v,
                                          widget.tex_coords.lower_left.v,
                                          widget.tex_coords.upper_right.v,
                                          widget.tex_coords.lower_right.v});
      if (base_right <= base_left || base_bottom <= base_top) {
        continue;
      }

      const int iw = tex_opt->width;
      const int ih = tex_opt->height;
      const int base_x0 = static_cast<int>(std::floor(base_left * static_cast<float>(iw)));
      const int base_x1 = static_cast<int>(std::floor(base_right * static_cast<float>(iw)));
      const int base_y0 = static_cast<int>(std::floor(base_top * static_cast<float>(ih)));
      const int base_y1 = static_cast<int>(std::floor(base_bottom * static_cast<float>(ih)));
      const int base_w = std::max(0, base_x1 - base_x0);
      const int base_h = std::max(0, base_y1 - base_y0);
      if (base_w <= 0 || base_h <= 0) {
        continue;
      }

      struct SrcRect {
        int x{0};
        int y{0};
        int w{0};
        int h{0};
      };

      SrcRect src{.x = base_x0, .y = base_y0, .w = base_w, .h = base_h};

      int dst_w = widget.width;
      int dst_h = widget.height;
      if (dst_w <= 0 || dst_h <= 0) {
        continue;
      }

      const float mod_a = std::clamp(effective_alpha * widget.color_a, 0.0f, 1.0f);
      const std::uint32_t abgr = PackAbgrStraight(
          widget.color_r, widget.color_g, widget.color_b, mod_a);

      const auto apply_gradient = [&](openwow::render::ui::Quad* q) {
        if (q == nullptr || !widget.gradient.enabled) {
          return;
        }
        q->has_vertex_colors = true;
        const auto corners = widget.gradient.ToUiRendererCornerColors();
        for (std::size_t i = 0; i < corners.size(); ++i) {
          q->vertex_abgr[i] = PackAbgrStraight(
              corners[i].r, corners[i].g, corners[i].b,
              std::clamp(effective_alpha * corners[i].a, 0.0f, 1.0f));
        }
        q->abgr = q->vertex_abgr[0];
      };

      std::optional<openwow::ui::framexml::BackdropSliceUvQuad> strip_edge_uv;
      if (!has_custom_uv_quad &&
          widget.slice != openwow::ui::framexml::TextureSlice::kNone &&
          widget.slice_edge_size_px > 0) {
        const int e = widget.slice_edge_size_px;
        const float repeat_tiles = openwow::ui::framexml::BackdropEdgeRepeatTiles(
            widget.slice, static_cast<float>(dst_w), static_cast<float>(dst_h));
        strip_edge_uv = openwow::ui::framexml::ComputeBackdropStripEdgeUvQuad(
            widget.slice, e, base_w, base_h, repeat_tiles);
        if (!strip_edge_uv.has_value()) {
          const auto slice_rect = openwow::ui::framexml::ComputeBackdropSlicePixelRect(
              widget.slice, e, base_w, base_h);
          if (slice_rect.has_value()) {
            src = SrcRect{.x = base_x0 + slice_rect->x,
                          .y = base_y0 + slice_rect->y,
                          .w = slice_rect->width,
                          .h = slice_rect->height};
          }
        }
      }

      if (!strip_edge_uv.has_value() && (src.w <= 0 || src.h <= 0)) {
        continue;
      }

      auto uv_for_src = [&](const SrcRect& r, bool apply_inset = true) -> std::array<float, 4> {
        float u_left = static_cast<float>(r.x) / static_cast<float>(iw);
        float u_right = static_cast<float>(r.x + r.w) / static_cast<float>(iw);
        float v_top = static_cast<float>(r.y) / static_cast<float>(ih);
        float v_bottom = static_cast<float>(r.y + r.h) / static_cast<float>(ih);

        if (apply_inset) {
          InsetUVHalfTexel(u_left, v_top, u_right, v_bottom,
                           static_cast<float>(iw), static_cast<float>(ih));
        }
        float ou0 = u_left;
        float ou1 = u_right;
        float ov0 = v_top;
        float ov1 = v_bottom;
        if (flip_x) std::swap(ou0, ou1);
        if (flip_y) std::swap(ov0, ov1);
        return {ou0, ov0, ou1, ov1};
      };

      auto submit_quad = [&](float x, float y, float w, float h, const SrcRect& s,
                             bool apply_inset = true) {
        const auto uv = uv_for_src(s, apply_inset);
        openwow::render::ui::Quad q;
        q.texture = tex_opt->handle;
        q.x = x;
        q.y = y;
        q.w = w;
        q.h = h;
        q.u0 = uv[0];
        q.v0 = uv[1];
        q.u1 = uv[2];
        q.v1 = uv[3];
        q.abgr = abgr;
        apply_gradient(&q);
        q.blend = ResolveGlueTextureBlend(widget.alpha_mode);
        q.desaturated = widgets.Desaturated(widget.name);
        q.sampler_flags = linear_clamp;
        q.rotation_radians = widget.animation_rotation_radians;
        q.rotation_origin_x = static_cast<float>(widget.x) + static_cast<float>(widget.width) * 0.5f;
        q.rotation_origin_y = static_cast<float>(widget.y) + static_cast<float>(widget.height) * 0.5f;
        q.has_rotation_origin = true;
        record_submit(frame_views.ui_view,
                      "glue.ui",
                      "ui.texture_quad",
                      ui_state_mask(q.blend, q.rotate_90),
                      widget.name,
                      path);
        impl_->ui->Submit(q);
      };

      const float x0 = std::floor(static_cast<float>(widget.x) + 0.5f);
      const float y0 = std::floor(static_cast<float>(widget.y) + 0.5f);
      if (strip_edge_uv.has_value()) {
        openwow::render::ui::Quad q;
        q.texture = tex_opt->handle;
        q.x = x0;
        q.y = y0;
        q.w = static_cast<float>(dst_w);
        q.h = static_cast<float>(dst_h);
        q.abgr = abgr;
        apply_gradient(&q);
        q.blend = ResolveGlueTextureBlend(widget.alpha_mode);
        q.desaturated = widgets.Desaturated(widget.name);
        q.sampler_flags = linear_wrap_v;
        q.rotation_radians = widget.animation_rotation_radians;
        q.rotation_origin_x = static_cast<float>(widget.x) + static_cast<float>(widget.width) * 0.5f;
        q.rotation_origin_y = static_cast<float>(widget.y) + static_cast<float>(widget.height) * 0.5f;
        q.has_rotation_origin = true;
        const auto uv = strip_edge_uv->ToUiRendererOrder();
        for (std::size_t i = 0; i < uv.size(); ++i) {
          const float local_u = uv[i].u;
          const float local_v = uv[i].v;
          q.uv_quad[i] = {
              base_left + local_u * (base_right - base_left),
              base_top + local_v * (base_bottom - base_top),
          };
        }
        q.has_custom_uv_quad = true;
        record_submit(frame_views.ui_view,
                      "glue.ui",
                      "ui.texture_quad",
                      ui_state_mask(q.blend, q.rotate_90),
                      widget.name,
                      path);
        impl_->ui->Submit(q);
        continue;
      }

      if (widget.tile_x || widget.tile_y) {
        const int step_x = (widget.tile_x && widget.tile_size_x > 0) ? widget.tile_size_x : dst_w;
        const int step_y = (widget.tile_y && widget.tile_size_y > 0) ? widget.tile_size_y : dst_h;
        const int base_step_x = std::max(1, step_x);
        const int base_step_y = std::max(1, step_y);
        const int tile_src_w = src.w;
        const int tile_src_h = src.h;

        for (int yy = 0; yy < dst_h; yy += base_step_y) {
          const int remaining_h = dst_h - yy;
          const int out_dh = std::min(step_y, remaining_h);
          const int out_sh = widget.tile_y
                                 ? std::max(1, static_cast<int>(std::round((static_cast<double>(tile_src_h) * out_dh) / base_step_y)))
                                 : tile_src_h;
          for (int xx = 0; xx < dst_w; xx += base_step_x) {
            const int remaining_w = dst_w - xx;
            const int out_dw = std::min(step_x, remaining_w);
            const int out_sw = widget.tile_x
                                   ? std::max(1, static_cast<int>(std::round((static_cast<double>(tile_src_w) * out_dw) / base_step_x)))
                                   : tile_src_w;
            SrcRect tile_src = src;
            tile_src.w = out_sw;
            tile_src.h = out_sh;
            submit_quad(x0 + static_cast<float>(xx),
                        y0 + static_cast<float>(yy),
                        static_cast<float>(out_dw),
                        static_cast<float>(out_dh),
                        tile_src,
                        false);
          }
        }
      } else if (has_custom_uv_quad) {
        openwow::render::ui::Quad q;
        q.texture = tex_opt->handle;
        q.x = x0;
        q.y = y0;
        q.w = static_cast<float>(dst_w);
        q.h = static_cast<float>(dst_h);
        q.abgr = abgr;
        apply_gradient(&q);
        q.blend = ResolveGlueTextureBlend(widget.alpha_mode);
        q.desaturated = widgets.Desaturated(widget.name);
        q.sampler_flags = linear_clamp;
        q.rotation_radians = widget.animation_rotation_radians;
        q.rotation_origin_x = static_cast<float>(widget.x) + static_cast<float>(widget.width) * 0.5f;
        q.rotation_origin_y = static_cast<float>(widget.y) + static_cast<float>(widget.height) * 0.5f;
        q.has_rotation_origin = true;
        for (std::size_t i = 0; i < widget_uvs.size(); ++i) {
          q.uv_quad[i] = {widget_uvs[i].u, widget_uvs[i].v};
        }
        q.has_custom_uv_quad = true;
        record_submit(frame_views.ui_view,
                      "glue.ui",
                      "ui.texture_quad",
                      ui_state_mask(q.blend, q.rotate_90),
                      widget.name,
                      path);
        impl_->ui->Submit(q);
      } else {
        submit_quad(x0, y0, static_cast<float>(dst_w), static_cast<float>(dst_h), src);
      }
      continue;
    }

    if (kind == "texture" && widget.texture_file.empty() &&
        (widget.has_vertex_color || widget.gradient.enabled) &&
        widget.width > 0 && widget.height > 0) {
      openwow::render::ui::Quad q;
      q.x = std::floor(static_cast<float>(widget.x) + 0.5f);
      q.y = std::floor(static_cast<float>(widget.y) + 0.5f);
      q.w = static_cast<float>(widget.width);
      q.h = static_cast<float>(widget.height);
      q.abgr = PackAbgrStraight(
          widget.color_r, widget.color_g, widget.color_b,
          std::clamp(effective_alpha * widget.color_a, 0.0f, 1.0f));
      q.blend = ResolveGlueTextureBlend(widget.alpha_mode);
      q.desaturated = widgets.Desaturated(widget.name);
      q.rotation_radians = widget.animation_rotation_radians;
      q.rotation_origin_x = static_cast<float>(widget.x) +
                            static_cast<float>(widget.width) * 0.5f;
      q.rotation_origin_y = static_cast<float>(widget.y) +
                            static_cast<float>(widget.height) * 0.5f;
      q.has_rotation_origin = true;
      if (widget.gradient.enabled) {
        q.has_vertex_colors = true;
        const auto corners = widget.gradient.ToUiRendererCornerColors();
        for (std::size_t i = 0; i < corners.size(); ++i) {
          q.vertex_abgr[i] = PackAbgrStraight(
              corners[i].r, corners[i].g, corners[i].b,
              std::clamp(effective_alpha * corners[i].a, 0.0f, 1.0f));
        }
        q.abgr = q.vertex_abgr[0];
      }
      record_submit(frame_views.ui_view,
                    "glue.ui",
                    "ui.solid_quad",
                    ui_state_mask(q.blend, q.rotate_90),
                    widget.name,
                    widget.gradient.enabled ? "solid:gradient" : "solid:vertex-color");
      impl_->ui->SubmitSolid(q);
      continue;
    }

    if (kind == "fontstring" && widget.text.empty() &&
        !focused_editbox.empty() &&
        widget.parent == focused_editbox &&
        widgets.TextRegionForWidget(widget.parent) == widget.name) {
      const auto editbox_text_state = ResolveEditBoxTextState(
          widgets, widget.parent, impl_->font_registry, impl_->text);
      if (editbox_text_state.has_value() &&
          editbox_text_state->editbox.enabled) {

        submit_editbox_decorations(*editbox_text_state, presentation.clip,
                                   EditBoxDecorationPass::kSelection);
        submit_editbox_decorations(*editbox_text_state, presentation.clip,
                                   EditBoxDecorationPass::kCaret);
      }
      continue;
    }

    if (kind == "fontstring" && !widget.text.empty()) {
      const auto resolved_style =
          openwow::ui::glue::ResolveGlueFontStringStyle(
              impl_->font_registry, widget);
      const auto& resolved = resolved_style.font;

      const float a = std::clamp(effective_alpha * widget.color_a * resolved.color_a, 0.0f, 1.0f);

      const std::uint32_t abgr = PackAbgrStraight(widget.color_r * resolved.color_r,
                                                  widget.color_g * resolved.color_g,
                                                  widget.color_b * resolved.color_b,
                                                  a);

      const auto coordinates =
          openwow::ui::glue::ResolveGlueFontCoordinateSpace(widgets, widget.name);
      const float render_scale = coordinates.render_scale();
      const int scaled_height_px = coordinates.RasterFontPixelHeight(
          static_cast<float>(resolved.height_px));

      openwow::render::BgfxTextKey key;
      key.font_path = resolved.font_file;
      key.height_px = scaled_height_px;
      key.text = widget.text;
      key.abgr = abgr;
      bool parent_is_editbox = false;
      openwow::ui::glue::GlueWidgetState parent_widget;
      std::optional<ResolvedEditBoxTextState> editbox_text_state;
      if (!widget.parent.empty()) {
        if (const auto p = widgets.GetResolvedWidget(widget.parent); p.has_value()) {
          parent_is_editbox =
              ToLowerAscii(p->kind) == "editbox" &&
              widgets.TextRegionForWidget(widget.parent) == widget.name;
          if (parent_is_editbox) {
            parent_widget = *p;
            editbox_text_state = ResolveEditBoxTextState(
                widgets, widget.parent, impl_->font_registry, impl_->text);
            if (editbox_text_state.has_value()) {
              if (editbox_text_state->visible_start_codepoints !=
                  widgets.GetEditVisibleStartCodepoints(widget.parent)) {
                widgets.SetEditVisibleStartCodepoints(
                    widget.parent,
                    editbox_text_state->visible_start_codepoints);
                widgets.MarkCursorDirty(widget.parent);
              }
              key.text = editbox_text_state->visible_display_text;
            }
          }
        }
      }
      const bool effective_non_space_wrap = resolved_style.non_space_wrap;
      key.wrap_px = (!parent_is_editbox && widget.width > 0 &&
                     (widget.word_wrap || effective_non_space_wrap))
                        ? widget.width
                        : 0;
      key.max_height_px = key.wrap_px > 0 ? std::max(0, widget.height) : 0;
      key.line_spacing_px =
          static_cast<int>(std::lround(resolved_style.line_spacing_px * render_scale));
      key.line_height_override_px = static_cast<int>(std::lround(
          openwow::ui::StoredUiHorizontalCoordinateToPixels(widget.text_height_stored) *
          render_scale));
      const std::string outline = ToLowerAscii(resolved.outline);

      key.outline_px =
          (outline == "thick" || outline == "thickoutline")
              ? openwow::render::kRetailThickOutlinePixels
              : ((outline == "normal" || outline == "outline")
                     ? openwow::render::kRetailNormalOutlinePixels
                     : 0);
      key.max_lines = static_cast<std::uint32_t>(std::max(0, widget.max_lines));
      key.word_wrap = widget.word_wrap;
      key.non_space_wrap = effective_non_space_wrap;
      key.indented_word_wrap = resolved_style.indented_word_wrap;
      key.monochrome = resolved.monochrome;

      const openwow::render::BgfxTextLayout* rendered = impl_->text.Layout(key);
      if (rendered == nullptr || rendered->width <= 0 || rendered->height <= 0) {
        continue;
      }

      const std::string_view justify_h =
          widget.justify_h.empty() ? std::string_view{resolved.justify_h}
                                   : std::string_view{widget.justify_h};
      const std::string_view justify_v =
          widget.justify_v.empty() ? std::string_view{resolved.justify_v}
                                   : std::string_view{widget.justify_v};
      const auto justification = openwow::ui::ResolveFontStringJustification(
          justify_h, justify_v);

      int dx = widget.x;
      int dy = widget.y;
      if (editbox_text_state.has_value()) {
        dx = editbox_text_state->aligned_text_x;
        dy = editbox_text_state->aligned_text_y;
      } else {
        if (widget.width > 0) {
          if (justification.horizontal == "CENTER") {
            dx = widget.x + (widget.width - rendered->width) / 2;
          } else if (justification.horizontal == "RIGHT") {
            dx = widget.x + widget.width - rendered->width;
          }
        }
        if (widget.height > 0) {
          if (justification.vertical == "CENTER" ||
              justification.vertical == "MIDDLE") {
            dy = widget.y + (widget.height - rendered->height) / 2;
          } else if (justification.vertical == "BOTTOM") {
            dy = widget.y + widget.height - rendered->height;
          }
        }
      }

      if (parent_is_editbox && editbox_text_state.has_value() &&
          widget.parent == focused_editbox &&
          editbox_text_state->editbox.enabled) {
        submit_editbox_decorations(*editbox_text_state, presentation.clip,
                                   EditBoxDecorationPass::kSelection);
      }

      std::optional<GluePixelRect> text_clip = presentation.clip;
      if (editbox_text_state.has_value() &&
          editbox_text_state->presented_text_rect.width > 0 &&
          editbox_text_state->presented_text_rect.height > 0) {
        const GluePixelRect edit_clip =
            editbox_text_state->presented_text_rect;
        text_clip = text_clip.has_value()
                        ? IntersectGlueRects(*text_clip, edit_clip)
                        : std::optional<GluePixelRect>{edit_clip};
      }
      if (text_clip.has_value()) {
        impl_->ui->SetScissor(text_clip->x, text_clip->y,
                              text_clip->width, text_clip->height);
        scissor_active = true;
      } else if (presentation.clip.has_value() ||
                 editbox_text_state.has_value()) {
        continue;
      }

      const float snapped_dx = std::floor(static_cast<float>(dx) + 0.5f);
      const float snapped_dy = std::floor(static_cast<float>(dy) + 0.5f);

      const auto submit_text = [&](const openwow::render::BgfxTextLayout& entry,
                                   const std::uint32_t base_abgr,
                                   const float x, const float y,
                                   const char* label,
                                   const bool shadow_pass = false) {
        constexpr std::size_t kMaxGlyphsPerMesh =
            (static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u) / 4u;
        const auto submit_pass = [&](const bool outline_pass) {
          for (const auto& batch : entry.batches) {
            const bgfx::TextureHandle texture =
                outline_pass ? batch.outline_texture : batch.body_texture;
            if (!bgfx::isValid(texture) || batch.glyphs.empty()) continue;

            for (std::size_t first = 0; first < batch.glyphs.size();
                 first += kMaxGlyphsPerMesh) {
              const std::size_t glyph_count =
                  std::min(kMaxGlyphsPerMesh, batch.glyphs.size() - first);
              impl_->text_mesh_vertices.resize(glyph_count * 4u);
              impl_->text_mesh_indices.resize(glyph_count * 6u);
              for (std::size_t glyph_index = 0; glyph_index < glyph_count;
                   ++glyph_index) {
                const auto& glyph = batch.glyphs[first + glyph_index];
                const std::uint32_t color =
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
                impl_->text_mesh_vertices[vertex + 0u] =
                    {x0, y0, glyph.u0, glyph.v0, color};
                impl_->text_mesh_vertices[vertex + 1u] =
                    {x1, y0, glyph.u1, glyph.v0, color};
                impl_->text_mesh_vertices[vertex + 2u] =
                    {x1, y1, glyph.u1, glyph.v1, color};
                impl_->text_mesh_vertices[vertex + 3u] =
                    {x0, y1, glyph.u0, glyph.v1, color};
                const std::size_t index = glyph_index * 6u;
                impl_->text_mesh_indices[index + 0u] =
                    static_cast<std::uint16_t>(vertex + 0u);
                impl_->text_mesh_indices[index + 1u] =
                    static_cast<std::uint16_t>(vertex + 1u);
                impl_->text_mesh_indices[index + 2u] =
                    static_cast<std::uint16_t>(vertex + 2u);
                impl_->text_mesh_indices[index + 3u] =
                    static_cast<std::uint16_t>(vertex + 0u);
                impl_->text_mesh_indices[index + 4u] =
                    static_cast<std::uint16_t>(vertex + 2u);
                impl_->text_mesh_indices[index + 5u] =
                    static_cast<std::uint16_t>(vertex + 3u);
              }

              record_submit(
                  frame_views.ui_view, "glue.ui",
                  outline_pass ? "ui.text_outline_mesh" : label,
                  ui_state_mask(openwow::render::ui::BlendMode::kCoveragePremultiplied,
                                false),
                  widget.name,
                  "font-atlas:" + resolved.font_file + "@" +
                      std::to_string(scaled_height_px));

              impl_->ui->SubmitMesh({
                  .texture = texture,
                  .vertices = impl_->text_mesh_vertices,
                  .indices = impl_->text_mesh_indices,
                  .blend = openwow::render::ui::BlendMode::kCoveragePremultiplied,
                  .topology = openwow::render::ui::PrimitiveTopology::kTriangleList,
                  .sampler_flags = outline_pass
                                       ? linear_clamp
                                       : (linear_clamp | BGFX_SAMPLER_POINT),
              });
            }
          }
        };

        submit_pass(true);
        submit_pass(false);
      };

      const float shadow_x = widget.has_shadow_offset
                                 ? openwow::ui::StoredUiHorizontalCoordinateToPixels(widget.shadow_x)
                                 : resolved.shadow_x_px;
      const float shadow_y = widget.has_shadow_offset
                                 ? openwow::ui::StoredUiHorizontalCoordinateToPixels(widget.shadow_y)
                                 : resolved.shadow_y_px;
      const float shadow_a = widget.has_shadow_color ? widget.shadow_a : resolved.shadow_a;
      if ((widget.has_shadow_offset || resolved.has_shadow) && shadow_a > 0.0f &&
          (shadow_x != 0.0f || shadow_y != 0.0f)) {
        openwow::render::BgfxTextKey shadow_key = key;
        shadow_key.abgr = PackAbgrStraight(
            widget.has_shadow_color ? widget.shadow_r : resolved.shadow_r,
            widget.has_shadow_color ? widget.shadow_g : resolved.shadow_g,
            widget.has_shadow_color ? widget.shadow_b : resolved.shadow_b,
            std::clamp(effective_alpha * shadow_a, 0.0f, 1.0f));
        submit_text(*rendered, shadow_key.abgr,
                    snapped_dx + shadow_x * render_scale,
                    snapped_dy - shadow_y * render_scale,
                    "ui.text_shadow_mesh", true);
      }
      submit_text(*rendered, key.abgr, snapped_dx, snapped_dy, "ui.text_mesh");

      if (text_clip.has_value()) {
        impl_->ui->ClearScissor();
        scissor_active = false;
      }
      if (parent_is_editbox && editbox_text_state.has_value() &&
          widget.parent == focused_editbox &&
          editbox_text_state->editbox.enabled) {
        submit_editbox_decorations(*editbox_text_state, presentation.clip,
                                   EditBoxDecorationPass::kCaret);
      }
      continue;
    }
  }

  {
    const bool show = widgets.GlobalTransitionOverlayVisible();
    const float t = std::clamp(widgets.GlobalTransitionFactor(), 0.0f, 1.0f);
    if (show) {
      const float screen_aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height)
                                               : kStandardLoadingScreenAspectRatio;
      const bool use_wide_viewport =
          settings.widescreen &&
          screen_aspect > kStandardLoadingScreenAspectRatio
                                + kWideLoadingScreenAspectRatioThreshold;
      const auto content_rect = openwow::ui::game::BuildAspectFittedViewportRect(
          screen_aspect,
          use_wide_viewport ? kWideLoadingScreenAspectRatio : kStandardLoadingScreenAspectRatio);

      for (const auto& base_quad : openwow::ui::game::BuildLoadingScreenProgressBarQuads(
               openwow::ui::game::kLoadingScreenTopProgressBarElements, t)) {
        const auto quad = openwow::ui::game::MapLoadingScreenQuadToViewport(base_quad, content_rect);
        const auto texture = impl_->textures.LoadAsync(base_quad.texture_path);
        if (!texture.has_value() || !bgfx::isValid(texture->handle)) {
          continue;
        }

        const float x = quad.left * static_cast<float>(width);
        const float y = (1.0f - quad.top) * static_cast<float>(height);
        const float w = (quad.right - quad.left) * static_cast<float>(width);
        const float h = (quad.top - quad.bottom) * static_cast<float>(height);
        if (w <= 0.5f || h <= 0.5f) continue;

        openwow::render::ui::Quad overlay_quad;
        overlay_quad.texture = texture->handle;
        overlay_quad.x = x;
        overlay_quad.y = y;
        overlay_quad.w = w;
        overlay_quad.h = h;
        overlay_quad.abgr = PackAbgrPremul(1.0f, 1.0f, 1.0f, 1.0f);
        overlay_quad.blend = openwow::render::ui::BlendMode::kAlpha;
        overlay_quad.sampler_flags = linear_clamp;
        const char* submit_name =
            std::string_view(base_quad.texture_path) == openwow::ui::game::kLoadingBarFillTexturePath
                ? "ui.streaming_overlay_bar_fill"
                : "ui.streaming_overlay_bar_border";
        record_submit(frame_views.ui_view,
                      "glue.ui",
                      submit_name,
                      ui_state_mask(overlay_quad.blend, overlay_quad.rotate_90),
                      "loading_bar",
                      NormalizeTexturePath(base_quad.texture_path));
        impl_->ui->Submit(overlay_quad);
      }
    }
  }

  impl_->ui->End();
  bgfx::setViewClear(frame_views.present_view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
  bgfx::touch(frame_views.present_view);

  if (impl_->submit_trace.has_value() && !impl_->submit_trace_written &&
      impl_->submit_trace_path.has_value() && !impl_->submit_trace->get().events().empty()) {
    impl_->submit_trace_written =
        impl_->submit_trace->get().WriteTsvFile(*impl_->submit_trace_path);
  }

  ++impl_->submit_trace_frame;
}

}
