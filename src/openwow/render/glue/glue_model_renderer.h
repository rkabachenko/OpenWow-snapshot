#pragma once

#include "openwow/runtime/scheduling/thread_pool_system.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/render/models/characters/character_appearance_texture_baker.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/materials/model_ffx_context.h"
#include "openwow/render/models/animation/model_render_callback_pipeline.h"
#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/glue/glue_streaming_counters.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/vfs/virtual_file_system.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::glue {
struct CharacterDisplayOwner;
class GlueCharSelectScene;
}

namespace openwow::client {

class GlueModelRenderer {
 public:
  using SoundKitSink = std::function<void(std::uint32_t)>;

  struct RenderViewOptions {
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    bool clear_color{false};
    std::uint32_t clear_color_rgba{0x000000ff};
    bool render_at_origin{false};
  };

  using PreparedModelBundle =
      openwow::render::m2::M2PreparedResourceBundle;

  struct StreamingBackend {
    std::function<PreparedModelBundle(const std::string&)> prepare_model;
    std::function<bool(openwow::render::PreparedTextureUpload)> commit_texture;

    std::function<bool(const std::string&)> texture_resident;
    std::function<openwow::render::m2::M2ModelLoadResult(
        std::unique_ptr<openwow::render::m2::M2PreparedModel>)>
        commit_model;
  };

  GlueModelRenderer(const openwow::vfs::VirtualFileSystem* vfs,
                    openwow::render::TextureManager& texture_manager,
                    openwow::render::m2::M2System& m2_system,
                    SoundKitSink sound_kit_sink);
  GlueModelRenderer(const openwow::vfs::VirtualFileSystem* vfs,
                    StreamingBackend streaming_backend,
                    openwow::render::m2::M2System& m2_system,
                    SoundKitSink sound_kit_sink);
  GlueModelRenderer(const openwow::vfs::VirtualFileSystem* vfs,
                    StreamingBackend streaming_backend,
                    openwow::render::m2::M2System& m2_system,
                    std::uint32_t streaming_worker_count,
                    SoundKitSink sound_kit_sink);
  ~GlueModelRenderer();

  GlueModelRenderer(const GlueModelRenderer&) = delete;
  GlueModelRenderer& operator=(const GlueModelRenderer&) = delete;

  void Shutdown();

  void StartStreaming();

  void PrewarmModels(const openwow::ui::glue::GlueWidgetRuntime& widgets);

  void TickStreaming(openwow::ui::glue::GlueWidgetRuntime& widgets,
                     std::uint32_t step_budget);
  [[nodiscard]] openwow::ui::glue::GlueStreamingCounters StreamingCounters() const;

  void SetDbcLoader(const openwow::data::dbc::DbcLoader* dbc_loader);

  void BindRenderSubmitTrace(std::optional<openwow::render::RenderSubmitTraceBinding> binding);
  void SetSharedWhiteTexture(bgfx::TextureHandle texture);
  void PruneWidgetInstances(const openwow::ui::glue::GlueWidgetRuntime& widgets);

  void BeginAnimationFrame(openwow::ui::glue::GlueWidgetRuntime& widgets,
                           std::uint32_t delta_ms);

  void BindAttachedCharacterScene(openwow::ui::glue::GlueCharSelectScene* scene,
                                  std::string model_frame_widget_name);
  void BindAttachedCharacterScenes(openwow::ui::glue::GlueCharSelectScene* select_scene,
                                   std::string select_model_frame_widget_name,
                                   openwow::ui::glue::GlueCharSelectScene* create_scene,
                                   std::string create_model_frame_widget_name);

  [[nodiscard]] static std::string BuildCharacterDisplayInstancePrefix(
      const std::string& model_frame_widget_name,
      const openwow::ui::glue::CharacterDisplayOwner& owner);
  [[nodiscard]] std::optional<float> AttachedSceneDeathEffectAlpha(
      const std::string& model_frame_widget_name) const;

  bool RenderWidget(openwow::ui::glue::GlueWidgetRuntime& widgets,
                    const openwow::ui::glue::GlueWidgetState& widget,
                    int view_id,
                    int viewport_width,
                    int viewport_height,
                    const RenderViewOptions& options);

 private:

  bool RenderSingleModel(openwow::ui::glue::GlueWidgetRuntime& widgets,
                         const openwow::ui::glue::GlueWidgetState& widget,
                         int view_id,
                         int viewport_width,
                         int viewport_height,
                         const RenderViewOptions& options);
  struct ModelAssets {
    std::string model_path;
    std::uint32_t model_id{0};
    float bounds_center[3]{0.0f, 0.0f, 0.0f};
    float bounds_radius{1.0f};
    bool ok{false};
  };

  struct InstanceState {
    std::string model_path;
    std::uint32_t m2_model_id{0};
    std::uint32_t m2_instance_id{0};
    int sequence{0};
    std::uint64_t sequence_revision{0};
    int camera{0};
    std::uint32_t time_ms{0};
    bool play_sound_events{true};
    std::string completion_widget_name;
    bool sequence_restart_pending{true};
    bool animation_completion_fired{false};
    std::uint32_t resolved_animation_id{0};
    std::uint32_t animation_duration_ms{0};
    std::uint16_t resolved_sequence_index{
        openwow::render::m2::kInvalidM2AnimationSequenceIndex};
    bool animation_info_refresh_pending{true};

    bool camera_source_diag_logged{false};

    float model_alpha{1.0f};

    openwow::render::ModelRenderCallbackSlot render_callback;

    std::optional<openwow::render::ModelFfxContext> model_ffx_ctx;
    std::unordered_map<std::uint32_t, std::string> replaceable_texture_paths;

    std::unordered_map<std::uint32_t, std::string> applied_replaceable_texture_paths;
    struct CharacterBodyPublication {
      bool valid{false};
      std::vector<std::size_t> visible_submesh_indices;
      std::array<float, 4> color_multiplier{1.0f, 1.0f, 1.0f, 1.0f};
    } character_body;
    std::array<std::optional<std::uint32_t>,
               openwow::render::m2::kM2RetailAnimationSlotCount>
        key_bone_animation_ids{};
    bool key_bone_animation_ids_initialized{false};
  };

  struct LoadRecord;

  std::optional<std::string> NormalizeModelPath(const std::string& raw) const;
  ModelAssets* GetAssetsIfReady(const std::string& model_m2_path);
  void EnsureQueued(const std::string& model_m2_path);
  void TrackActiveModelPath(const std::string& model_m2_path);
  void StartRequest(LoadRecord& record);
  void PumpSharedModelResources(bool loading_boost);
  std::string EnsureCharacterAppearanceQueued(
      const openwow::render::CharacterAppearanceTextureSources& sources);

  [[nodiscard]] bool HasEvictedCharacterAppearanceComposite(
      const std::string& cache_key) const;
  void StartCharacterAppearanceRequest(
      const openwow::render::CharacterAppearanceTextureSources& sources,
      const std::string& cache_key);
  void PumpPreparedCompletions();
  bool CommitOnePreparedResource(const std::string& model_m2_path);
  bool CommitOnePreparedCharacterAppearance(const std::string& cache_key);
  [[nodiscard]] const std::unordered_map<std::uint32_t, std::string>*
  GetReadyCharacterAppearanceTexturePaths(
      const openwow::render::CharacterAppearanceTextureSources& sources) const;

  [[nodiscard]] bool CharacterAppearanceLoadFailed(
      const openwow::render::CharacterAppearanceTextureSources& sources) const;
  std::uint32_t EnsureM2Instance(InstanceState& instance, const ModelAssets& assets);
  void DestroyM2Instance(InstanceState& instance);

  std::uint32_t EnsureM2InstanceResolved(InstanceState& instance, const ModelAssets& assets);

  bool PrepareM2InstanceForSubmit(InstanceState& instance,
                                  const ModelAssets& assets,
                                  const openwow::render::RenderMatrix4x4 &model_matrix,
                                  const std::optional<openwow::render::RenderVec4>& tint_color,
                                  const openwow::render::m2::M2BatchUniforms& uniforms,
                                  const std::vector<std::size_t>* visible_submesh_indices);

  void InitSelectionTriangleResources();
  void SubmitSelectionTriangleOverlay(
      int view_id,
      const openwow::render::SelectionTriangleVertices& triangle,
      const std::array<float, 4>& color_multiplier);
  struct AttachedCharacterSceneBinding {
    openwow::ui::glue::GlueCharSelectScene* scene{nullptr};
    std::string model_frame_widget_name;
    std::shared_ptr<const bool> callback_lifetime;
  };
  [[nodiscard]] const AttachedCharacterSceneBinding* FindAttachedCharacterSceneBinding(
      const std::string& widget_name) const;
  void ReleaseAttachedCharacterInstances(const std::string& model_frame_widget_name);
  void PruneAttachedCharacterDisplayInstances(
      const AttachedCharacterSceneBinding& binding);
  static void InvalidateAnimationInfo(InstanceState& instance);
  [[nodiscard]] static bool CharacterBodyReadyForPublication(
      bool instance_valid,
      bool all_submissions_ready,
      std::uint32_t submitted_draw_count,
      std::size_t visible_submesh_count) noexcept;
  void ForgetM2Instance(InstanceState& instance);
  void InvalidateModelResource(std::uint32_t model_id);

  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  StreamingBackend streaming_backend_;
  const openwow::data::dbc::DbcLoader* dbc_loader_{nullptr};
  std::optional<openwow::render::ModelRenderCallbackGhostLightSample>
      character_select_ghost_light_;
  std::unordered_map<std::string, std::unique_ptr<ModelAssets>> assets_by_path_;
  std::unordered_map<std::string, InstanceState> instances_;

  enum class LoadPhase : std::uint8_t {
    kNone = 0,
    kPreparing = 1,
    kCommitTextures = 2,
    kCommitModel = 3,
    kReady = 4,
    kFailed = 5,
  };

  struct LoadRecord {
    std::string model_path;
    LoadPhase phase{LoadPhase::kNone};
    std::uint64_t request_id{0};
    std::uint64_t generation{0};
    std::uint64_t queued_at_ms{0};
    openwow::render::m2::M2StreamTicket shared_ticket;
    std::vector<openwow::render::m2::M2ModelTextureDependency> pending_textures;
    std::size_t next_texture_idx{0};
    std::optional<PreparedModelBundle> prepared;
  };

  struct PreparedCompletion {
    std::string model_path;
    std::uint64_t request_id{0};
    std::uint64_t generation{0};
    PreparedModelBundle prepared;
  };

  enum class CharacterAppearanceLoadPhase : std::uint8_t {
    kPreparing = 0,
    kCommit = 1,
    kReady = 2,
    kFailed = 3,
  };

  struct CharacterAppearanceLoadRecord {
    std::string cache_key;
    CharacterAppearanceLoadPhase phase{CharacterAppearanceLoadPhase::kPreparing};
    std::uint64_t request_id{0};
    std::uint64_t generation{0};
    std::uint64_t queued_at_ms{0};
    std::vector<openwow::render::PreparedTextureUpload> pending_textures;
    std::size_t next_texture_idx{0};
    std::unordered_map<std::uint32_t, std::string> ready_replaceable_paths;
  };

  struct PreparedCharacterAppearanceCompletion {
    std::string cache_key;
    std::uint64_t request_id{0};
    std::uint64_t generation{0};
    openwow::render::PreparedCharacterAppearanceTextures prepared;
  };

  struct CompletionMailbox {
    std::mutex mutex;
    std::deque<PreparedCompletion> completions;
    std::deque<PreparedCharacterAppearanceCompletion> character_appearance_completions;
  };

  std::unordered_map<std::string, LoadRecord> loads_;
  openwow::render::m2::M2System& m2_system_;
  SoundKitSink sound_kit_sink_;
  std::vector<std::string> active_model_paths_;
  std::unordered_map<std::string, CharacterAppearanceLoadRecord>
      character_appearance_loads_;
  std::vector<std::string> active_character_appearance_keys_;
  openwow::core::ThreadPoolSystem streaming_workers_;

  bool shared_model_streaming_{false};
  std::uint32_t streaming_worker_count_{2};
  std::shared_ptr<CompletionMailbox> completion_mailbox_;
  std::uint64_t streaming_generation_{1};
  std::uint64_t next_request_id_{1};
  bool static_model_prewarm_queued_{false};
  bool initial_visible_commit_boost_{false};

  std::vector<AttachedCharacterSceneBinding> attached_character_scenes_;

  bgfx::ProgramHandle selection_triangle_program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle selection_triangle_sampler_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle shared_white_texture_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout selection_triangle_layout_;
  bool selection_triangle_resources_init_{false};

};

[[nodiscard]] std::uint16_t ResolveGlueModelViewClearFlags(
    const GlueModelRenderer::RenderViewOptions& options);
[[nodiscard]] std::uint32_t ResolveGlueModelViewClearColor(
    const GlueModelRenderer::RenderViewOptions& options);

struct GlueModelViewRect {
  std::uint16_t x{0};
  std::uint16_t y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
};

[[nodiscard]] std::optional<GlueModelViewRect> ResolveGlueModelViewRect(
    int x, int y, int width, int height, int output_width, int output_height);

}
