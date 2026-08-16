#include "openwow/render/glue/glue_model_renderer.h"

#include "openwow/foundation/text/ascii.h"

#include "openwow/render/glue/glue_attachment_transform.h"
#include "openwow/render/glue/glue_ghost_lighting.h"
#include "openwow/render/glue/glue_scene_lighting.h"

#include "openwow/data/texture_cache.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/foundation/math/projection_aspect.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/materials/model_ffx_render_callback.h"
#include "openwow/render/models/animation/model_instance_transform.h"
#include "openwow/render/models/animation/model_light_record.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/world/environment/sky_renderer.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/ui/ui_shaders.h"
#include "openwow/ui/glue/glue_charselect_scene.h"
#include "openwow/ui/glue/glue_streaming_model_paths.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bx/math.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <unordered_set>
#include <vector>

namespace openwow::client {

namespace {

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::string NormalizePath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  value = Trim(std::move(value));
  if (value.empty())
    return {};
  if (value.front() != '/')
    value.insert(value.begin(), '/');
  return value;
}

constexpr std::uint32_t kModelEventPlaySoundKit = 0x4F534424u;
constexpr float kDefaultGlueCameraDistanceScale = 2.6f;
constexpr float kDefaultGlueCameraHeightScale = 0.22f;
constexpr float kDefaultGlueCameraFovRadians = 0.785398163f;
constexpr float kDefaultGlueCameraNearClip = 0.1f;
constexpr float kDefaultGlueCameraMinimumFarClip = 1200.0f;
constexpr float kDefaultGlueCameraFarClipDistanceScale = 12.0f;
constexpr bool kGlueModelFrameDiagnostics = false;
constexpr std::uint32_t kMaxGlueStreamingCommitsPerFrame = 4u;
constexpr std::uint32_t kInitialGlueStreamingCommitsPerFrame = 128u;
constexpr std::uint32_t kDefaultGlueStreamingWorkerCount = 2u;

constexpr std::uint32_t kCharacterBodyReplaceableTextureType = 1u;

std::uint64_t GlueModelStreamingNowMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

bool GlueModelStreamingTraceEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("OPENWOW_GLUE_MODEL_TRACE");
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

GlueModelRenderer::StreamingBackend MakeDefaultStreamingBackend(
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::TextureManager& texture_manager) {
  (void)vfs;
  return {
      .prepare_model = {},
      .commit_texture = [&texture_manager](
                            openwow::render::PreparedTextureUpload texture) {
        if (!texture.valid) {
          return false;
        }
        return bgfx::isValid(
            texture_manager.CommitPreparedTexture(std::move(texture)));
      },
      .texture_resident = [&texture_manager](const std::string& path) {
        return texture_manager.HasResidentTexture(path);
      },
      .commit_model = {},
  };
}

void PlayGlueSoundEventsForInstanceInterval(
    openwow::render::m2::M2System& m2_system,
    std::uint32_t instance_id,
    std::uint32_t previous_time_ms,
    std::uint32_t current_time_ms,
    const GlueModelRenderer::SoundKitSink& sound_kit_sink) {
  if (!sound_kit_sink) {
    return;
  }
  for (const auto &event : m2_system.CollectInstanceTriggeredEvents(
           instance_id, previous_time_ms, current_time_ms)) {
    if (event.identifier != kModelEventPlaySoundKit || event.data == 0) {
      continue;
    }
    sound_kit_sink(event.data);
  }
}

void PopulateModelLightRecordFromEntry(const openwow::ui::glue::ModelLightEntry &entry,
                                       openwow::render::ModelLightRecord &record) {
  record.Initialize();
  if (!entry.enabled) {
    return;
  }

  record.enabled = 1u;
  record.ambient_rgb = {entry.amb_r, entry.amb_g, entry.amb_b};
  record.diffuse_rgb = {entry.dir_r, entry.dir_g, entry.dir_b};
  if (entry.omni) {
    record.light_kind = openwow::render::ModelLightRecord::kRetailPointKind;
    record.point_position = {entry.dir_x, entry.dir_y, entry.dir_z};
  } else {
    record.light_kind = 0u;
    record.direction = {entry.dir_x, entry.dir_y, entry.dir_z};
  }
}

void PopulateModelFfxContextBlockFromLights(
    openwow::render::ModelFfxContextBlock &block,
    const std::vector<openwow::ui::glue::ModelLightEntry> &lights) {

  block.Reset();

  const std::size_t count =
      std::min(lights.size(), openwow::render::ModelFfxContextBlock::kMaxLightRecords);
  for (std::size_t index = 0; index < count; ++index) {
    openwow::render::ModelLightRecord record;
    PopulateModelLightRecordFromEntry(lights[index], record);
    block.SetLightRecord(index, record);
  }
}

void SyncModelFfxContextFromRuntime(openwow::render::ModelFfxContext &context,
                                    const openwow::ui::glue::GlueWidgetRuntime &widgets,
                                    const std::string &widget_name) {
  PopulateModelFfxContextBlockFromLights(
      context.blocks[0],
      widgets.GetModelLights(widget_name, openwow::ui::glue::ModelLightCategory::kGeneral, 0));
  PopulateModelFfxContextBlockFromLights(
      context.blocks[1],
      widgets.GetModelLights(widget_name, openwow::ui::glue::ModelLightCategory::kGeneral, 1));
  PopulateModelFfxContextBlockFromLights(
      context.blocks[2],
      widgets.GetModelLights(widget_name, openwow::ui::glue::ModelLightCategory::kCharacter, 0));
  PopulateModelFfxContextBlockFromLights(
      context.blocks[3],
      widgets.GetModelLights(widget_name, openwow::ui::glue::ModelLightCategory::kCharacter, 1));
  PopulateModelFfxContextBlockFromLights(
      context.blocks[4],
      widgets.GetModelLights(widget_name, openwow::ui::glue::ModelLightCategory::kPet, 0));
  PopulateModelFfxContextBlockFromLights(
      context.blocks[5],
      widgets.GetModelLights(widget_name, openwow::ui::glue::ModelLightCategory::kPet, 1));
}

bx::Vec3 ToBxVec3(const openwow::render::RenderVec3 &value) {
  return bx::Vec3{value[0], value[1], value[2]};
}

openwow::render::m2::M2CameraPose BuildM2CameraState(const bx::Vec3 &eye, const bx::Vec3 &target,
                                                     const float fov_rad, const float roll_rad,
                                                     const float near_clip,
                                                     const float far_clip) {
  openwow::render::m2::M2CameraPose state;
  state.position[0] = eye.x;
  state.position[1] = eye.y;
  state.position[2] = eye.z;
  state.target[0] = target.x;
  state.target[1] = target.y;
  state.target[2] = target.z;
  state.fov_rad = fov_rad;
  state.roll_rad = roll_rad;
  state.near_clip = near_clip;
  state.far_clip = far_clip;
  return state;
}

openwow::render::RenderMatrix4x4 BuildM2CameraInverseViewRotation(
    const openwow::render::m2::M2CameraPose &state) {
  const auto basis = openwow::render::m2::M2System::BuildCameraBasis(state);
  openwow::render::RenderMatrix4x4 out{openwow::render::kRenderIdentityMatrix4x4};
  out[0] = basis.right[0];
  out[1] = basis.right[1];
  out[2] = basis.right[2];
  out[4] = basis.up[0];
  out[5] = basis.up[1];
  out[6] = basis.up[2];
  out[8] = basis.forward[0];
  out[9] = basis.forward[1];
  out[10] = basis.forward[2];
  return out;
}

struct M2GlueViewProjection {
  openwow::render::RenderMatrix4x4 view{openwow::render::kRenderIdentityMatrix4x4};
  openwow::render::RenderMatrix4x4 projection{openwow::render::kRenderIdentityMatrix4x4};
  float vertical_fov_degrees{0.0f};
};

M2GlueViewProjection BuildM2GlueViewProjection(
    const openwow::render::m2::M2CameraPose &state,
    const float projection_aspect) {
  M2GlueViewProjection result;
  result.view = openwow::render::m2::M2System::BuildCameraViewMatrix(state);
  const float fov_vert =
      openwow::render::m2::M2System::BuildCameraVerticalFov(state.fov_rad, projection_aspect);
  result.vertical_fov_degrees = bx::toDeg(fov_vert);
  bx::mtxProj(result.projection.data(), result.vertical_fov_degrees, projection_aspect,
              std::max(0.01f, state.near_clip), std::max(1.0f, state.far_clip),
              bgfx::getCaps()->homogeneousDepth, bx::Handedness::Left);
  return result;
}

struct SelectionTriangleVertex {
  float x;
  float y;
  float z;
  float u;
  float v;
  std::uint32_t abgr;
};

struct GlueFogParameters {
  float near_distance{0.0f};
  float far_distance{0.0f};
  float red{0.0f};
  float green{0.0f};
  float blue{0.0f};
};

GlueFogParameters ResolveGlueFogParameters(
    GlueFogParameters inherited,
    const openwow::render::ModelRenderCallbackLightingState* callback_lighting) {
  if (callback_lighting == nullptr ||
      callback_lighting->fog_mode ==
          openwow::render::ModelRenderCallbackFogMode::kInherit) {
    return inherited;
  }
  if (callback_lighting->fog_mode ==
      openwow::render::ModelRenderCallbackFogMode::kDisabled) {
    inherited.near_distance = 0.0f;
    inherited.far_distance = 0.0f;
    return inherited;
  }

  inherited.near_distance = callback_lighting->fog_start;
  inherited.far_distance = callback_lighting->fog_end;
  inherited.red = callback_lighting->fog_color_rgb[0];
  inherited.green = callback_lighting->fog_color_rgb[1];
  inherited.blue = callback_lighting->fog_color_rgb[2];
  return inherited;
}

std::uint32_t PackSelectionTriangleColor(const std::array<float, 4> &color_multiplier) {
  const auto clamp_channel = [](const float value) -> std::uint8_t {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
  };

  const std::uint8_t r = clamp_channel(color_multiplier[0]);
  const std::uint8_t g = clamp_channel(color_multiplier[1]);
  const std::uint8_t b = clamp_channel(color_multiplier[2]);
  const std::uint8_t a = clamp_channel(color_multiplier[3]);
  return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
}

}

GlueModelRenderer::GlueModelRenderer(
    const openwow::vfs::VirtualFileSystem *vfs,
    openwow::render::TextureManager& texture_manager,
    openwow::render::m2::M2System& m2_system,
    SoundKitSink sound_kit_sink)
    : GlueModelRenderer(
          vfs, MakeDefaultStreamingBackend(vfs, texture_manager),
          m2_system, kDefaultGlueStreamingWorkerCount,
          std::move(sound_kit_sink)) {
  shared_model_streaming_ = true;
  m2_system_.SetAsyncFileLoader(
      [vfs](const std::string& path) -> std::vector<std::uint8_t> {
        if (vfs == nullptr) {
          return {};
        }
        auto bytes = vfs->ReadFileBytes(path);
        return bytes.has_value() ? std::move(*bytes)
                                 : std::vector<std::uint8_t>{};
      });
  m2_system_.StartAsyncLoading(kDefaultGlueStreamingWorkerCount);
}

GlueModelRenderer::GlueModelRenderer(const openwow::vfs::VirtualFileSystem *vfs,
                                     StreamingBackend streaming_backend,
                                     openwow::render::m2::M2System& m2_system,
                                     SoundKitSink sound_kit_sink)
    : GlueModelRenderer(vfs, std::move(streaming_backend), m2_system,
                        kDefaultGlueStreamingWorkerCount,
                        std::move(sound_kit_sink)) {}

GlueModelRenderer::GlueModelRenderer(
    const openwow::vfs::VirtualFileSystem *vfs,
    StreamingBackend streaming_backend,
    openwow::render::m2::M2System& m2_system,
    const std::uint32_t streaming_worker_count,
    SoundKitSink sound_kit_sink)
    : vfs_(vfs), streaming_backend_(std::move(streaming_backend)),
      m2_system_(m2_system),
      sound_kit_sink_(std::move(sound_kit_sink)),
      streaming_worker_count_(streaming_worker_count),
      completion_mailbox_(std::make_shared<CompletionMailbox>()) {
  streaming_workers_.Initialize(streaming_worker_count_);
  if (vfs_ != nullptr) {
    m2_system_.SetFileLoader(
        [vfs = vfs_](const std::string &path) -> std::vector<std::uint8_t> {
          const auto bytes = vfs->ReadFileBytes(path);
          return bytes.has_value() ? *bytes : std::vector<std::uint8_t>{};
        });
  }
}

GlueModelRenderer::~GlueModelRenderer() {
  Shutdown();
}

void GlueModelRenderer::SetDbcLoader(
    const openwow::data::dbc::DbcLoader* dbc_loader) {
  dbc_loader_ = dbc_loader;
  character_select_ghost_light_ =
      openwow::render::glue::ResolveCharacterSelectGhostLight(dbc_loader);
}

void GlueModelRenderer::StartStreaming() {
  if (shared_model_streaming_) {
    m2_system_.StartAsyncLoading(streaming_worker_count_);
    return;
  }
  if (!streaming_workers_.IsInitialized()) {
    completion_mailbox_ = std::make_shared<CompletionMailbox>();
    streaming_workers_.Initialize(streaming_worker_count_);
  }
}

std::uint16_t ResolveGlueModelViewClearFlags(
    const GlueModelRenderer::RenderViewOptions& options) {

  return options.clear_color ? static_cast<std::uint16_t>(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH)
                             : static_cast<std::uint16_t>(BGFX_CLEAR_DEPTH);
}

std::uint32_t ResolveGlueModelViewClearColor(
    const GlueModelRenderer::RenderViewOptions& options) {
  return options.clear_color ? options.clear_color_rgba : 0x00000000u;
}

std::optional<GlueModelViewRect> ResolveGlueModelViewRect(
    const int x, const int y, const int width, const int height,
    const int output_width, const int output_height) {
  if (width <= 0 || height <= 0 || output_width <= 0 || output_height <= 0) {
    return std::nullopt;
  }

  const std::int64_t src_x0 = x;
  const std::int64_t src_y0 = y;
  const std::int64_t src_x1 = src_x0 + width;
  const std::int64_t src_y1 = src_y0 + height;

  const std::int64_t clipped_x0 = std::clamp<std::int64_t>(src_x0, 0, output_width);
  const std::int64_t clipped_y0 = std::clamp<std::int64_t>(src_y0, 0, output_height);
  const std::int64_t clipped_x1 = std::clamp<std::int64_t>(src_x1, 0, output_width);
  const std::int64_t clipped_y1 = std::clamp<std::int64_t>(src_y1, 0, output_height);

  if (clipped_x1 <= clipped_x0 || clipped_y1 <= clipped_y0) {
    return std::nullopt;
  }

  return GlueModelViewRect{
      .x = static_cast<std::uint16_t>(clipped_x0),
      .y = static_cast<std::uint16_t>(clipped_y0),
      .width = static_cast<std::uint16_t>(clipped_x1 - clipped_x0),
      .height = static_cast<std::uint16_t>(clipped_y1 - clipped_y0),
  };
}

void GlueModelRenderer::BindRenderSubmitTrace(
    std::optional<openwow::render::RenderSubmitTraceBinding> binding) {
  m2_system_.BindRenderSubmitTrace(std::move(binding));
}

void GlueModelRenderer::SetSharedWhiteTexture(bgfx::TextureHandle texture) {
  shared_white_texture_ = texture;
}

void GlueModelRenderer::BindAttachedCharacterScene(openwow::ui::glue::GlueCharSelectScene *scene,
                                                   std::string model_frame_widget_name) {
  BindAttachedCharacterScenes(scene, std::move(model_frame_widget_name), nullptr, {});
}

std::string GlueModelRenderer::BuildCharacterDisplayInstancePrefix(
    const std::string &model_frame_widget_name,
    const openwow::ui::glue::CharacterDisplayOwner &owner) {
  std::string prefix = model_frame_widget_name + ":display:";
  switch (owner.kind) {
  case openwow::ui::glue::CharacterDisplayOwnerKind::kCharacterListRow:
    if (owner.character_id != 0u) {
      prefix += "guid:" + std::to_string(owner.character_id);
    } else {
      prefix += "row:" + std::to_string(owner.row_index);
    }
    break;
  case openwow::ui::glue::CharacterDisplayOwnerKind::kCreatePreview:
    prefix += "create";
    break;
  case openwow::ui::glue::CharacterDisplayOwnerKind::kNone:
    prefix += "none";
    break;
  }
  return prefix;
}

void GlueModelRenderer::BindAttachedCharacterScenes(
    openwow::ui::glue::GlueCharSelectScene *select_scene,
    std::string select_model_frame_widget_name,
    openwow::ui::glue::GlueCharSelectScene *create_scene,
    std::string create_model_frame_widget_name) {
  std::vector<AttachedCharacterSceneBinding> desired;
  desired.reserve(2);

  auto append = [&](openwow::ui::glue::GlueCharSelectScene *scene, std::string widget_name) {
    if (scene == nullptr || widget_name.empty()) {
      return;
    }
    for (auto &binding : desired) {
      if (binding.model_frame_widget_name == widget_name) {
        binding.scene = scene;
        return;
      }
    }
    desired.push_back(AttachedCharacterSceneBinding{
        .scene = scene,
        .model_frame_widget_name = std::move(widget_name),
    });
  };

  append(select_scene, std::move(select_model_frame_widget_name));
  append(create_scene, std::move(create_model_frame_widget_name));

  if (desired.size() == attached_character_scenes_.size()) {
    bool unchanged = true;
    for (std::size_t i = 0; i < desired.size(); ++i) {
      if (desired[i].scene != attached_character_scenes_[i].scene ||
          desired[i].model_frame_widget_name != attached_character_scenes_[i].model_frame_widget_name) {
        unchanged = false;
        break;
      }
    }
    if (unchanged) {
      return;
    }
  }

  for (const auto &existing : attached_character_scenes_) {
    const bool retained = std::any_of(
        desired.begin(), desired.end(), [&](const AttachedCharacterSceneBinding &candidate) {
          return candidate.scene == existing.scene &&
                 candidate.model_frame_widget_name == existing.model_frame_widget_name;
        });
    if (!retained) {
      ReleaseAttachedCharacterInstances(existing.model_frame_widget_name);
    }
  }

  attached_character_scenes_ = std::move(desired);
  for (auto &binding : attached_character_scenes_) {
    binding.callback_lifetime = std::make_shared<const bool>(true);
    const std::weak_ptr<const bool> callback_lifetime = binding.callback_lifetime;
    const std::string owner_widget = binding.model_frame_widget_name;
    binding.scene->SetContentReleaseCallback(
        [this, callback_lifetime, owner_widget]() {
          if (!callback_lifetime.expired()) {
            ReleaseAttachedCharacterInstances(owner_widget);
          }
        });
  }
}

const GlueModelRenderer::AttachedCharacterSceneBinding *
GlueModelRenderer::FindAttachedCharacterSceneBinding(const std::string &widget_name) const {
  if (widget_name.empty()) {
    return nullptr;
  }
  for (const auto &binding : attached_character_scenes_) {
    if (binding.scene != nullptr && binding.model_frame_widget_name == widget_name) {
      return &binding;
    }
  }
  return nullptr;
}

std::optional<float> GlueModelRenderer::AttachedSceneDeathEffectAlpha(
    const std::string &model_frame_widget_name) const {
  const auto *binding = FindAttachedCharacterSceneBinding(model_frame_widget_name);
  return binding == nullptr || binding->scene == nullptr
             ? std::nullopt
             : binding->scene->viewport_death_effect_alpha();
}

void GlueModelRenderer::Shutdown() {

  ++streaming_generation_;
  if (streaming_workers_.IsInitialized()) {
    streaming_workers_.Shutdown();
  }
  if (completion_mailbox_) {
    std::lock_guard mailbox_lock(completion_mailbox_->mutex);
    completion_mailbox_->completions.clear();
    completion_mailbox_->character_appearance_completions.clear();
  }

  attached_character_scenes_.clear();
  for (auto &[key, instance] : instances_) {
    (void)key;
    DestroyM2Instance(instance);
  }
  if (shared_model_streaming_) {
    for (auto& [path, record] : loads_) {
      (void)path;
      if (record.shared_ticket) {
        m2_system_.ReleaseModelAsync(record.shared_ticket);
        record.shared_ticket = {};
      }
    }
  }
  assets_by_path_.clear();
  instances_.clear();
  loads_.clear();
  active_model_paths_.clear();
  character_appearance_loads_.clear();
  active_character_appearance_keys_.clear();
  static_model_prewarm_queued_ = false;
  initial_visible_commit_boost_ = false;

  openwow::render::ui::DestroyUiProgram(selection_triangle_program_, selection_triangle_sampler_);
  shared_white_texture_ = BGFX_INVALID_HANDLE;
  selection_triangle_resources_init_ = false;
  m2_system_.BindRenderSubmitTrace(std::nullopt);
}

void GlueModelRenderer::ReleaseAttachedCharacterInstances(
    const std::string &model_frame_widget_name) {
  if (model_frame_widget_name.empty()) {
    return;
  }

  const std::string attached_prefix = model_frame_widget_name + ':';
  for (auto it = instances_.begin(); it != instances_.end();) {
    if (!it->first.starts_with(attached_prefix)) {
      ++it;
      continue;
    }
    DestroyM2Instance(it->second);
    it = instances_.erase(it);
  }
}

void GlueModelRenderer::PruneAttachedCharacterDisplayInstances(
    const AttachedCharacterSceneBinding &binding) {
  if (binding.scene == nullptr || binding.model_frame_widget_name.empty()) {
    return;
  }

  std::vector<std::string> retained_prefixes;
  retained_prefixes.reserve(
      binding.scene->character_display_preloads().size() + 1u);
  for (const auto &preload : binding.scene->character_display_preloads()) {
    retained_prefixes.push_back(BuildCharacterDisplayInstancePrefix(
        binding.model_frame_widget_name, preload.owner));
  }
  if (binding.scene->current_display_owner().valid()) {
    const auto active_prefix = BuildCharacterDisplayInstancePrefix(
        binding.model_frame_widget_name, binding.scene->current_display_owner());
    if (std::find(retained_prefixes.begin(), retained_prefixes.end(), active_prefix) ==
        retained_prefixes.end()) {
      retained_prefixes.push_back(active_prefix);
    }
  }

  const std::string display_root = binding.model_frame_widget_name + ":display:";
  for (auto it = instances_.begin(); it != instances_.end();) {
    if (!it->first.starts_with(display_root)) {
      ++it;
      continue;
    }
    const bool retained = std::any_of(
        retained_prefixes.begin(), retained_prefixes.end(),
        [&](const std::string &prefix) {
          return it->first == prefix ||
                 (it->first.starts_with(prefix) && it->first.size() > prefix.size() &&
                  it->first[prefix.size()] == ':');
        });
    if (retained) {
      ++it;
      continue;
    }
    DestroyM2Instance(it->second);
    it = instances_.erase(it);
  }
}

void GlueModelRenderer::PruneWidgetInstances(
    const openwow::ui::glue::GlueWidgetRuntime& widgets) {
  for (auto it = instances_.begin(); it != instances_.end();) {
    const std::size_t attachment_separator = it->first.find(':');
    const bool owner_is_live = attachment_separator == std::string::npos
                                   ? widgets.HasWidget(it->first)
                                   : widgets.HasWidget(
                                         it->first.substr(0, attachment_separator));
    if (owner_is_live) {
      ++it;
      continue;
    }
    DestroyM2Instance(it->second);
    it = instances_.erase(it);
  }
}

std::optional<std::string> GlueModelRenderer::NormalizeModelPath(const std::string &raw) const {
  std::string p = NormalizePath(raw);
  if (p.empty())
    return std::nullopt;
  auto ext = std::filesystem::path(p).extension().string();
  std::string lower_ext = ext;
  std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (lower_ext == ".mdx" || lower_ext == ".mdl") {
    p = std::filesystem::path(p).replace_extension(".m2").generic_string();
  } else if (ext.empty()) {
    p += ".m2";
  }
  return p;
}

GlueModelRenderer::ModelAssets *
GlueModelRenderer::GetAssetsIfReady(const std::string &model_m2_path) {
  if (auto it = assets_by_path_.find(model_m2_path); it != assets_by_path_.end()) {
    if (it->second && it->second->ok && it->second->model_id != 0) {
      const auto readiness =
          m2_system_.QueryModelReadiness(it->second->model_id);
      if (readiness.reason == openwow::render::m2::M2ResultReason::kInvalidHandle) {

        InvalidateModelResource(it->second->model_id);
        return nullptr;
      }
      if (readiness.status != openwow::render::m2::M2ResultStatus::kReady ||
          !readiness.render_ready) {
        return nullptr;
      }
      return it->second.get();
    }
  }
  return nullptr;
}

void GlueModelRenderer::InvalidateAnimationInfo(InstanceState &instance) {
  instance.resolved_animation_id = 0;
  instance.animation_duration_ms = 0;
  instance.resolved_sequence_index =
      openwow::render::m2::kInvalidM2AnimationSequenceIndex;
  instance.animation_info_refresh_pending = true;
}

bool GlueModelRenderer::CharacterBodyReadyForPublication(
    const bool instance_valid,
    const bool all_submissions_ready,
    const std::uint32_t submitted_draw_count,
    const std::size_t visible_submesh_count) noexcept {
  return instance_valid && all_submissions_ready && submitted_draw_count != 0u &&
          visible_submesh_count != 0u;
}

void GlueModelRenderer::ForgetM2Instance(InstanceState &instance) {
  instance.m2_instance_id = 0;
  instance.m2_model_id = 0;
  instance.applied_replaceable_texture_paths.clear();
  instance.sequence_restart_pending = true;
  instance.key_bone_animation_ids_initialized = false;
  InvalidateAnimationInfo(instance);
}

void GlueModelRenderer::InvalidateModelResource(const std::uint32_t model_id) {
  if (model_id == 0) {
    return;
  }

  for (auto &[path, assets] : assets_by_path_) {
    if (!assets || assets->model_id != model_id) {
      continue;
    }
    assets->model_id = 0;
    assets->ok = false;
    assets->bounds_center[0] = 0.0f;
    assets->bounds_center[1] = 0.0f;
    assets->bounds_center[2] = 0.0f;
    assets->bounds_radius = 1.0f;
    if (auto load = loads_.find(path); load != loads_.end()) {
      if (shared_model_streaming_ && load->second.shared_ticket) {
        m2_system_.ReleaseModelAsync(load->second.shared_ticket);
      }
      loads_.erase(load);
    }
  }

  for (auto &[key, instance] : instances_) {
    (void)key;
    if (instance.m2_model_id == model_id) {
      ForgetM2Instance(instance);
    }
  }
}

void GlueModelRenderer::EnsureQueued(const std::string &model_m2_path) {
  if (model_m2_path.empty())
    return;
  if (GetAssetsIfReady(model_m2_path) != nullptr)
    return;
  if (loads_.find(model_m2_path) != loads_.end())
    return;

  LoadRecord rec;
  rec.model_path = model_m2_path;
  rec.phase = LoadPhase::kPreparing;
  rec.request_id = next_request_id_++;
  rec.generation = streaming_generation_;
  rec.queued_at_ms = GlueModelStreamingNowMs();
  auto [record_it, inserted] = loads_.insert_or_assign(model_m2_path, std::move(rec));
  (void)inserted;

  if (assets_by_path_.find(model_m2_path) == assets_by_path_.end()) {
    auto assets = std::make_unique<ModelAssets>();
    assets->model_path = model_m2_path;
    assets->ok = false;
    assets_by_path_.insert_or_assign(model_m2_path, std::move(assets));
  }
  if (GlueModelStreamingTraceEnabled()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "GlueModelRenderer trace: queued model=" + model_m2_path);
  }
  if (shared_model_streaming_) {
    record_it->second.shared_ticket =
        m2_system_.AcquireModelAsync(model_m2_path);
    if (!record_it->second.shared_ticket) {
      record_it->second.phase = LoadPhase::kFailed;
    }
  } else {
    if (!streaming_workers_.IsInitialized()) {
      completion_mailbox_ = std::make_shared<CompletionMailbox>();
      streaming_workers_.Initialize(streaming_worker_count_);
    }
    StartRequest(record_it->second);
  }
}

void GlueModelRenderer::PumpSharedModelResources(const bool loading_boost) {
  if (!shared_model_streaming_) {
    return;
  }

  openwow::render::m2::M2StreamCommitBudget budget;
  if (loading_boost) {
    budget.wall_time = std::chrono::milliseconds{6};
    budget.upload_bytes = 16u * 1024u * 1024u;
    budget.textures = 16u;
    budget.models = 8u;
  }
  static_cast<void>(m2_system_.PumpAsyncLoading(budget));

  for (auto& [path, record] : loads_) {
    if (!record.shared_ticket || record.phase == LoadPhase::kReady ||
        record.phase == LoadPhase::kFailed) {
      continue;
    }
    const auto query = m2_system_.QueryModelAsync(record.shared_ticket);
    record.pending_textures = query.texture_dependencies;
    record.next_texture_idx = query.committed_texture_count;
    if (query.state == openwow::render::m2::M2StreamState::kFailed ||
        query.state == openwow::render::m2::M2StreamState::kInvalid) {
      record.phase = LoadPhase::kFailed;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "Glue model load failed: path=" + path +
              " state=" + std::to_string(static_cast<int>(query.state)) +
              " status=" + std::to_string(static_cast<int>(query.status)) +
              " reason=" + std::to_string(static_cast<int>(query.reason)) +
              (query.detail.empty() ? std::string{} :
                                      " detail=" + query.detail));
      if (auto assets = assets_by_path_.find(path);
          assets != assets_by_path_.end() && assets->second) {
        assets->second->ok = false;
      }
      continue;
    }
    if (query.state != openwow::render::m2::M2StreamState::kReady ||
        query.model_id == 0u) {
      record.phase = query.state ==
                             openwow::render::m2::M2StreamState::kCommitPending
                         ? LoadPhase::kCommitTextures
                         : LoadPhase::kPreparing;
      continue;
    }

    auto assets = assets_by_path_.find(path);
    if (assets == assets_by_path_.end() || !assets->second) {
      record.phase = LoadPhase::kFailed;
      continue;
    }
    assets->second->model_id = query.model_id;
    assets->second->bounds_center[0] =
        query.spatial_info.local_bounding_sphere[0];
    assets->second->bounds_center[1] =
        query.spatial_info.local_bounding_sphere[1];
    assets->second->bounds_center[2] =
        query.spatial_info.local_bounding_sphere[2];
    assets->second->bounds_radius =
        std::max(1.0f, query.spatial_info.local_bounding_sphere[3]);
    assets->second->ok = true;
    record.next_texture_idx = record.pending_textures.size();
    record.phase = LoadPhase::kReady;
  }
}

void GlueModelRenderer::PrewarmModels(
    const openwow::ui::glue::GlueWidgetRuntime& widgets) {
  std::unordered_set<std::string> queued_paths;
  const auto queue_widget = [&](const openwow::ui::glue::GlueWidgetState& widget) {
    const std::string kind = openwow::text::ToLowerAscii(widget.kind);
    if ((kind != "model" && kind != "modelffx") || widget.model_file.empty()) {
      return;
    }
    const auto normalized = NormalizeModelPath(widget.model_file);
    if (!normalized.has_value()) {
      return;
    }
    const std::string identity = openwow::text::ToLowerAscii(*normalized);
    if (!queued_paths.insert(identity).second) {
      return;
    }
    EnsureQueued(*normalized);
    if (const auto record = loads_.find(*normalized);
        record != loads_.end() && record->second.phase != LoadPhase::kReady &&
        record->second.phase != LoadPhase::kFailed) {
      initial_visible_commit_boost_ = true;
    }
  };

  if (const auto login = widgets.GetWidget("AccountLogin"); login.has_value()) {
    queue_widget(*login);
  }

  for (const auto& widget : widgets.VisibleWidgetsInRenderOrder()) {
    queue_widget(widget);
  }

  if (static_model_prewarm_queued_) {
    return;
  }
  for (const auto& name : widgets.WidgetNamesInRegistrationOrder()) {
    if (const auto widget = widgets.GetWidget(name); widget.has_value()) {
      queue_widget(*widget);
    }
  }
  static_model_prewarm_queued_ = true;
}

void GlueModelRenderer::TrackActiveModelPath(const std::string &model_m2_path) {
  if (model_m2_path.empty()) {
    return;
  }
  if (std::find(active_model_paths_.begin(), active_model_paths_.end(), model_m2_path) ==
      active_model_paths_.end()) {
    active_model_paths_.push_back(model_m2_path);
  }
  EnsureQueued(model_m2_path);
}

void GlueModelRenderer::StartRequest(LoadRecord& record) {
  const auto prepare = streaming_backend_.prepare_model;
  const auto mailbox = completion_mailbox_;
  const std::string path = record.model_path;
  const std::uint64_t request_id = record.request_id;
  const std::uint64_t generation = record.generation;
  const std::uint64_t queued_at_ms = record.queued_at_ms;
  if (!prepare || !mailbox) {
    record.phase = LoadPhase::kFailed;
    return;
  }

  try {
    (void)streaming_workers_.Submit(
        "glue-model:" + path, openwow::core::TaskPriority::High,
        [prepare, mailbox, path, request_id, generation, queued_at_ms]() mutable {
          const std::uint64_t prepare_started_ms = GlueModelStreamingNowMs();
          if (GlueModelStreamingTraceEnabled()) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kInfo,
                "GlueModelRenderer trace: prepare begin model=" + path +
                    " queue_delay_ms=" +
                    std::to_string(prepare_started_ms - queued_at_ms));
          }
          PreparedModelBundle prepared;
          try {
            prepared = prepare(path);
          } catch (const std::exception& error) {
            prepared.status = openwow::render::m2::M2ResultStatus::kFailed;
            prepared.reason = openwow::render::m2::M2ResultReason::kParseFailed;
            prepared.detail = error.what();
          } catch (...) {
            prepared.status = openwow::render::m2::M2ResultStatus::kFailed;
            prepared.reason = openwow::render::m2::M2ResultReason::kParseFailed;
            prepared.detail = "unknown preparation exception";
          }

          if (GlueModelStreamingTraceEnabled()) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kInfo,
                "GlueModelRenderer trace: prepare end model=" + path +
                    " duration_ms=" +
                    std::to_string(GlueModelStreamingNowMs() - prepare_started_ms) +
                    " textures=" + std::to_string(prepared.textures.size()));
          }

          std::lock_guard lock(mailbox->mutex);
          mailbox->completions.push_back({
              .model_path = path,
              .request_id = request_id,
              .generation = generation,
              .prepared = std::move(prepared),
          });
        });
  } catch (const std::exception& error) {
    record.phase = LoadPhase::kFailed;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueModelRenderer: failed to schedule " + path + ": " +
                           error.what());
  }
}

bool GlueModelRenderer::HasEvictedCharacterAppearanceComposite(
    const std::string& cache_key) const {
  if (!streaming_backend_.texture_resident) {
    return false;
  }
  const auto record_it = character_appearance_loads_.find(cache_key);
  if (record_it == character_appearance_loads_.end() ||
      record_it->second.phase != CharacterAppearanceLoadPhase::kReady) {
    return false;
  }
  const auto body = record_it->second.ready_replaceable_paths.find(
      kCharacterBodyReplaceableTextureType);
  if (body == record_it->second.ready_replaceable_paths.end() ||
      body->second != cache_key) {
    return false;
  }
  return !streaming_backend_.texture_resident(cache_key);
}

std::string GlueModelRenderer::EnsureCharacterAppearanceQueued(
    const openwow::render::CharacterAppearanceTextureSources& sources) {
  const std::string cache_key =
      openwow::render::BuildCharacterAppearanceTextureCacheKey(sources);
  if (cache_key.empty()) {
    return cache_key;
  }
  if (character_appearance_loads_.contains(cache_key)) {
    if (!HasEvictedCharacterAppearanceComposite(cache_key)) {
      return cache_key;
    }

    character_appearance_loads_.erase(cache_key);
  }

  if (!streaming_workers_.IsInitialized()) {
    completion_mailbox_ = std::make_shared<CompletionMailbox>();
    streaming_workers_.Initialize(streaming_worker_count_);
  }

  CharacterAppearanceLoadRecord record;
  record.cache_key = cache_key;
  record.phase = CharacterAppearanceLoadPhase::kPreparing;
  record.request_id = next_request_id_++;
  record.generation = streaming_generation_;
  record.queued_at_ms = GlueModelStreamingNowMs();
  character_appearance_loads_.insert_or_assign(cache_key, std::move(record));

  if (GlueModelStreamingTraceEnabled()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "GlueModelRenderer trace: queued character appearance=" +
                           cache_key);
  }
  StartCharacterAppearanceRequest(sources, cache_key);
  return cache_key;
}

void GlueModelRenderer::StartCharacterAppearanceRequest(
    const openwow::render::CharacterAppearanceTextureSources& sources,
    const std::string& cache_key) {
  auto record_it = character_appearance_loads_.find(cache_key);
  if (record_it == character_appearance_loads_.end()) {
    return;
  }
  CharacterAppearanceLoadRecord& record = record_it->second;
  const auto mailbox = completion_mailbox_;
  const auto* item_display_info =
      dbc_loader_ != nullptr ? &dbc_loader_->item_display_info() : nullptr;
  const auto* vfs = vfs_;
  const std::uint64_t request_id = record.request_id;
  const std::uint64_t generation = record.generation;
  const std::uint64_t queued_at_ms = record.queued_at_ms;
  if (!mailbox || vfs == nullptr) {
    record.phase = CharacterAppearanceLoadPhase::kFailed;
    return;
  }

  try {
    (void)streaming_workers_.Submit(
        "glue-character-appearance:" + cache_key,
        openwow::core::TaskPriority::High,
        [sources, cache_key, mailbox, item_display_info, vfs, request_id,
         generation, queued_at_ms]() mutable {
          const std::uint64_t prepare_started_ms = GlueModelStreamingNowMs();
          if (GlueModelStreamingTraceEnabled()) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kInfo,
                "GlueModelRenderer trace: prepare begin character appearance=" +
                    cache_key + " queue_delay_ms=" +
                    std::to_string(prepare_started_ms - queued_at_ms));
          }

          openwow::render::PreparedCharacterAppearanceTextures prepared;
          try {
            prepared = openwow::render::PrepareCharacterAppearanceTextures(
                sources, item_display_info,
                [vfs](const std::string& path) -> std::vector<std::uint8_t> {
                  auto bytes = vfs->ReadFileBytes(path);
                  return bytes.has_value() ? std::move(*bytes)
                                           : std::vector<std::uint8_t>{};
                });
          } catch (const std::exception& error) {
            prepared.error = error.what();
          } catch (...) {
            prepared.error = "unknown appearance preparation exception";
          }

          if (GlueModelStreamingTraceEnabled()) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kInfo,
                "GlueModelRenderer trace: prepare end character appearance=" +
                    cache_key + " duration_ms=" +
                    std::to_string(GlueModelStreamingNowMs() -
                                   prepare_started_ms) +
                    " textures=" +
                    std::to_string(1u + prepared.direct_textures.size()));
          }

          std::lock_guard lock(mailbox->mutex);
          mailbox->character_appearance_completions.push_back({
              .cache_key = cache_key,
              .request_id = request_id,
              .generation = generation,
              .prepared = std::move(prepared),
          });
        });
  } catch (const std::exception& error) {
    record.phase = CharacterAppearanceLoadPhase::kFailed;
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GlueModelRenderer: failed to schedule character appearance " +
            cache_key + ": " + error.what());
  }
}

void GlueModelRenderer::PumpPreparedCompletions() {
  if (!completion_mailbox_) {
    return;
  }
  std::deque<PreparedCompletion> completions;
  std::deque<PreparedCharacterAppearanceCompletion> appearance_completions;
  {
    std::lock_guard lock(completion_mailbox_->mutex);
    completions.swap(completion_mailbox_->completions);
    appearance_completions.swap(
        completion_mailbox_->character_appearance_completions);
  }

  for (auto& completion : completions) {
    auto record_it = loads_.find(completion.model_path);
    if (record_it == loads_.end()) {
      continue;
    }
    LoadRecord& record = record_it->second;
    if (completion.generation != streaming_generation_ ||
        record.generation != completion.generation ||
        record.request_id != completion.request_id ||
        record.phase != LoadPhase::kPreparing) {
      continue;
    }

    if (completion.prepared.status !=
        openwow::render::m2::M2ResultStatus::kReady) {
      record.pending_textures = completion.prepared.texture_dependencies;
      record.next_texture_idx = 0;
      record.phase = LoadPhase::kFailed;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "GlueModelRenderer: async model preparation failed: " +
              completion.model_path +
              (completion.prepared.detail.empty()
                   ? std::string{}
                   : ": " + completion.prepared.detail));
      continue;
    }
    if (completion.prepared.textures.size() !=
        completion.prepared.texture_dependencies.size()) {
      record.phase = LoadPhase::kFailed;
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "GlueModelRenderer: malformed prepared texture bundle: " +
                             completion.model_path);
      continue;
    }

    std::unordered_set<std::uint32_t> seen_rows;
    std::vector<openwow::render::m2::M2ModelTextureDependency> unique_dependencies;
    std::vector<openwow::render::PreparedTextureUpload> unique_textures;
    unique_dependencies.reserve(completion.prepared.texture_dependencies.size());
    unique_textures.reserve(completion.prepared.textures.size());
    for (std::size_t index = 0;
         index < completion.prepared.texture_dependencies.size(); ++index) {
      const auto& dependency = completion.prepared.texture_dependencies[index];
      if (dependency.texture_path.empty() ||
          !seen_rows
               .insert(openwow::data::HashTextureCachePath(
                   dependency.texture_path))
               .second) {
        continue;
      }
      unique_dependencies.push_back(dependency);
      unique_textures.push_back(std::move(completion.prepared.textures[index]));
    }
    completion.prepared.texture_dependencies = std::move(unique_dependencies);
    completion.prepared.textures = std::move(unique_textures);
    record.pending_textures = completion.prepared.texture_dependencies;
    record.next_texture_idx = 0;

    record.prepared = std::move(completion.prepared);
    record.phase = record.pending_textures.empty() ? LoadPhase::kCommitModel
                                                   : LoadPhase::kCommitTextures;
  }

  for (auto& completion : appearance_completions) {
    auto record_it = character_appearance_loads_.find(completion.cache_key);
    if (record_it == character_appearance_loads_.end()) {
      continue;
    }
    CharacterAppearanceLoadRecord& record = record_it->second;
    if (completion.generation != streaming_generation_ ||
        record.generation != completion.generation ||
        record.request_id != completion.request_id ||
        record.phase != CharacterAppearanceLoadPhase::kPreparing) {
      continue;
    }

    if (!completion.prepared.valid || !completion.prepared.body.valid) {
      record.phase = CharacterAppearanceLoadPhase::kFailed;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "GlueModelRenderer: async character appearance preparation failed: " +
              completion.cache_key +
              (completion.prepared.error.empty()
                   ? std::string{}
                   : ": " + completion.prepared.error));
      continue;
    }

    record.pending_textures.clear();
    record.pending_textures.reserve(
        1u + completion.prepared.direct_textures.size());
    record.pending_textures.push_back(std::move(completion.prepared.body));
    for (auto& texture : completion.prepared.direct_textures) {
      record.pending_textures.push_back(std::move(texture));
    }
    record.next_texture_idx = 0;
    record.ready_replaceable_paths =
        std::move(completion.prepared.replaceable_paths);
    record.phase = record.pending_textures.empty()
                       ? CharacterAppearanceLoadPhase::kReady
                       : CharacterAppearanceLoadPhase::kCommit;
  }
}

bool GlueModelRenderer::CommitOnePreparedResource(
    const std::string& model_m2_path) {
  if (shared_model_streaming_) {
    return false;
  }
  auto record_it = loads_.find(model_m2_path);
  if (record_it == loads_.end()) {
    return false;
  }
  LoadRecord& record = record_it->second;
  if (!record.prepared.has_value()) {
    return false;
  }

  if (record.phase == LoadPhase::kCommitTextures) {
    if (record.next_texture_idx >= record.prepared->textures.size()) {
      record.phase = LoadPhase::kCommitModel;
      return false;
    }
    openwow::render::PreparedTextureUpload texture =
        std::move(record.prepared->textures[record.next_texture_idx]);
    if (!streaming_backend_.commit_texture ||
        !streaming_backend_.commit_texture(std::move(texture))) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "GlueModelRenderer: prepared texture commit failed: " +
                             record.pending_textures[record.next_texture_idx].texture_path);
      record.phase = LoadPhase::kFailed;
      if (auto assets_it = assets_by_path_.find(model_m2_path);
          assets_it != assets_by_path_.end() && assets_it->second) {
        assets_it->second->ok = false;
      }
      return true;
    }
    ++record.next_texture_idx;
    if (record.next_texture_idx == record.pending_textures.size()) {
      record.phase = LoadPhase::kCommitModel;
    }
    return true;
  }

  if (record.phase != LoadPhase::kCommitModel) {
    return false;
  }
  if (!streaming_backend_.commit_model) {
    record.phase = LoadPhase::kFailed;
    return true;
  }

  const auto spatial = record.prepared->spatial_info;
  const auto result = streaming_backend_.commit_model(
      std::move(record.prepared->model));
  record.prepared.reset();
  auto assets_it = assets_by_path_.find(model_m2_path);
  if (result.status != openwow::render::m2::M2ResultStatus::kReady ||
      result.model_id == 0 || assets_it == assets_by_path_.end() ||
      !assets_it->second) {
    record.phase = LoadPhase::kFailed;
    if (assets_it != assets_by_path_.end() && assets_it->second) {
      assets_it->second->ok = false;
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueModelRenderer: prepared model commit failed: " +
                           model_m2_path);
    return true;
  }

  ModelAssets& assets = *assets_it->second;
  assets.model_id = result.model_id;
  assets.bounds_center[0] = spatial.local_bounding_sphere[0];
  assets.bounds_center[1] = spatial.local_bounding_sphere[1];
  assets.bounds_center[2] = spatial.local_bounding_sphere[2];
  assets.bounds_radius = std::max(1.0f, spatial.local_bounding_sphere[3]);
  assets.ok = true;
  record.phase = LoadPhase::kReady;
  if (GlueModelStreamingTraceEnabled()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "GlueModelRenderer trace: ready model=" + model_m2_path +
            " total_ms=" +
            std::to_string(GlueModelStreamingNowMs() - record.queued_at_ms) +
            " textures=" + std::to_string(record.pending_textures.size()));
  }
  return true;
}

bool GlueModelRenderer::CommitOnePreparedCharacterAppearance(
    const std::string& cache_key) {
  auto record_it = character_appearance_loads_.find(cache_key);
  if (record_it == character_appearance_loads_.end()) {
    return false;
  }
  CharacterAppearanceLoadRecord& record = record_it->second;
  if (record.phase != CharacterAppearanceLoadPhase::kCommit) {
    return false;
  }
  if (record.next_texture_idx >= record.pending_textures.size()) {
    record.phase = CharacterAppearanceLoadPhase::kReady;
    return false;
  }

  const std::string texture_path =
      record.pending_textures[record.next_texture_idx].path;
  auto texture = std::move(record.pending_textures[record.next_texture_idx]);
  if (!streaming_backend_.commit_texture ||
      !streaming_backend_.commit_texture(std::move(texture))) {
    record.phase = CharacterAppearanceLoadPhase::kFailed;
    record.pending_textures.clear();
    record.ready_replaceable_paths.clear();
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GlueModelRenderer: prepared character appearance texture commit failed: " +
            texture_path);
    return true;
  }

  ++record.next_texture_idx;
  if (record.next_texture_idx == record.pending_textures.size()) {
    record.phase = CharacterAppearanceLoadPhase::kReady;
    record.pending_textures.clear();
    if (GlueModelStreamingTraceEnabled()) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "GlueModelRenderer trace: ready character appearance=" + cache_key +
              " total_ms=" +
              std::to_string(GlueModelStreamingNowMs() - record.queued_at_ms) +
              " replacements=" +
              std::to_string(record.ready_replaceable_paths.size()));
    }
  }
  return true;
}

const std::unordered_map<std::uint32_t, std::string>*
GlueModelRenderer::GetReadyCharacterAppearanceTexturePaths(
    const openwow::render::CharacterAppearanceTextureSources& sources) const {
  const std::string cache_key =
      openwow::render::BuildCharacterAppearanceTextureCacheKey(sources);
  if (cache_key.empty()) {
    return nullptr;
  }
  const auto record_it = character_appearance_loads_.find(cache_key);
  if (record_it == character_appearance_loads_.end() ||
      record_it->second.phase != CharacterAppearanceLoadPhase::kReady) {
    return nullptr;
  }
  return &record_it->second.ready_replaceable_paths;
}

bool GlueModelRenderer::CharacterAppearanceLoadFailed(
    const openwow::render::CharacterAppearanceTextureSources& sources) const {
  const std::string cache_key =
      openwow::render::BuildCharacterAppearanceTextureCacheKey(sources);
  if (cache_key.empty()) {
    return false;
  }
  const auto record_it = character_appearance_loads_.find(cache_key);
  return record_it != character_appearance_loads_.end() &&
         record_it->second.phase == CharacterAppearanceLoadPhase::kFailed;
}

bool GlueModelRenderer::RenderWidget(openwow::ui::glue::GlueWidgetRuntime &widgets,
                                     const openwow::ui::glue::GlueWidgetState &widget, int view_id,
                                     int viewport_width, int viewport_height,
                                     const RenderViewOptions &options) {
  return RenderSingleModel(widgets, widget, view_id, viewport_width, viewport_height, options);
}

void GlueModelRenderer::BeginAnimationFrame(
    openwow::ui::glue::GlueWidgetRuntime &widgets,
    const std::uint32_t delta_ms) {
  for (auto &[instance_key, instance] : instances_) {
    const std::size_t attachment_separator = instance_key.find(':');
    const std::string_view owner_name = attachment_separator == std::string::npos
                                            ? std::string_view(instance_key)
                                            : std::string_view(instance_key).substr(
                                                  0, attachment_separator);
    if (!widgets.IsVisible(std::string(owner_name))) {
      continue;
    }

    if (attachment_separator != std::string::npos) {
      const auto *binding = FindAttachedCharacterSceneBinding(std::string(owner_name));
      if (binding != nullptr && binding->scene != nullptr) {
        const std::string display_root = std::string(owner_name) + ":display:";
        if (instance_key.starts_with(display_root)) {
          const auto &active_owner = binding->scene->current_display_owner();
          if (!active_owner.valid()) {
            continue;
          }
          const std::string active_prefix = BuildCharacterDisplayInstancePrefix(
              std::string(owner_name), active_owner);
          if (!(instance_key.starts_with(active_prefix) &&
                instance_key.size() > active_prefix.size() &&
                instance_key[active_prefix.size()] == ':')) {
            continue;
          }
        }
      }
    }

    if (attachment_separator == std::string::npos) {
      if (const auto widget = widgets.GetResolvedWidget(instance_key);
          widget.has_value()) {
        if (const auto model_path = NormalizeModelPath(widget->model_file);
            model_path.has_value()) {
          const int sequence = widgets.GetSequence(instance_key);
          const std::uint64_t sequence_revision =
              widgets.GetSequenceRevision(instance_key);
          if (instance.model_path != *model_path ||
              instance.sequence_revision != sequence_revision) {
            instance.model_path = *model_path;
            instance.sequence = sequence;
            instance.sequence_revision = sequence_revision;
            instance.time_ms = 0;
            instance.sequence_restart_pending = true;
            instance.animation_completion_fired = false;
            InvalidateAnimationInfo(instance);
          }
          instance.camera = widgets.GetCamera(instance_key);
        }
      }
    }

    const std::uint32_t previous_time_ms = instance.time_ms;
    bool time_advanced = false;
    if (attachment_separator == std::string::npos) {
      if (const auto override_ms = widgets.ConsumeSequenceTimeMs(instance_key);
          override_ms.has_value()) {
        instance.time_ms = *override_ms;
      } else if (delta_ms != 0u) {
        instance.time_ms += delta_ms;
        time_advanced = true;
      }
    } else if (delta_ms != 0u) {
      instance.time_ms += delta_ms;
      time_advanced = true;
    }

    auto *assets = GetAssetsIfReady(instance.model_path);
    if (assets == nullptr || !assets->ok) {
      continue;
    }
    const std::uint32_t instance_id = EnsureM2InstanceResolved(instance, *assets);
    if (instance_id == 0) {
      continue;
    }
    if (time_advanced && instance.play_sound_events) {
      PlayGlueSoundEventsForInstanceInterval(
          m2_system_, instance_id, previous_time_ms,
          instance.time_ms, sound_kit_sink_);
    }

    if (instance.animation_info_refresh_pending) {
      const auto animation_info =
          m2_system_.QueryInstanceAnimationInfo(instance_id);
      if (animation_info.status ==
              openwow::render::m2::M2ResultStatus::kReady &&
          animation_info.info.sequence_index !=
              openwow::render::m2::kInvalidM2AnimationSequenceIndex) {
        instance.resolved_animation_id =
            animation_info.info.resolved_animation_id;
        instance.animation_duration_ms = animation_info.info.duration_ms;
        instance.resolved_sequence_index = animation_info.info.sequence_index;
        instance.animation_info_refresh_pending = false;
      }
    }
    if (!instance.completion_widget_name.empty() &&
        instance.animation_duration_ms != 0u && time_advanced &&
        !instance.animation_completion_fired &&
        previous_time_ms < instance.animation_duration_ms &&
        instance.time_ms >= instance.animation_duration_ms) {
      widgets.QueueAnimationFinishedEvent(instance.completion_widget_name);
      instance.animation_completion_fired = true;
    }
  }
}

void GlueModelRenderer::TickStreaming(openwow::ui::glue::GlueWidgetRuntime &widgets,
                                      std::uint32_t step_budget) {
  active_model_paths_.clear();
  active_character_appearance_keys_.clear();
  std::vector<std::string> preload_model_paths;
  std::vector<std::string> preload_appearance_keys;

  static std::uint32_t ts_diag_ctr = 0;
  const bool ts_diag = (ts_diag_ctr++ % 300 == 0);

  const auto& visible_widgets = widgets.VisibleWidgetsInRenderOrder();
  std::vector<std::string> streaming_paths =
      openwow::ui::glue::CollectGlueStreamingModelPaths(visible_widgets, nullptr, {});
  for (const auto &binding : attached_character_scenes_) {
    if (binding.scene == nullptr || binding.model_frame_widget_name.empty()) {
      continue;
    }
    PruneAttachedCharacterDisplayInstances(binding);
    auto binding_paths = openwow::ui::glue::CollectGlueStreamingModelPaths(
        visible_widgets, binding.scene, binding.model_frame_widget_name);
    streaming_paths.insert(streaming_paths.end(),
                           std::make_move_iterator(binding_paths.begin()),
                           std::make_move_iterator(binding_paths.end()));

    const bool host_visible = std::any_of(
        visible_widgets.begin(), visible_widgets.end(),
        [&](const openwow::ui::glue::GlueWidgetState& candidate) {
          return candidate.name == binding.model_frame_widget_name;
        });
    if (host_visible &&
        !binding.scene->selected_character_model_path().empty()) {
      const std::string appearance_key = EnsureCharacterAppearanceQueued(
          binding.scene->character_appearance_texture_sources());
      if (!appearance_key.empty() &&
          std::find(active_character_appearance_keys_.begin(),
                    active_character_appearance_keys_.end(),
                    appearance_key) ==
              active_character_appearance_keys_.end()) {
        active_character_appearance_keys_.push_back(appearance_key);
      }
    }
    if (host_visible) {
      for (const auto &preload : binding.scene->character_display_preloads()) {
        if (preload.model_path.empty()) {
          continue;
        }
        EnsureQueued(preload.model_path);
        if (std::find(preload_model_paths.begin(), preload_model_paths.end(),
                      preload.model_path) == preload_model_paths.end()) {
          preload_model_paths.push_back(preload.model_path);
        }
        for (const auto &component_path : preload.component_model_paths) {
          EnsureQueued(component_path);
          if (std::find(preload_model_paths.begin(), preload_model_paths.end(),
                        component_path) == preload_model_paths.end()) {
            preload_model_paths.push_back(component_path);
          }
        }
        const std::string appearance_key =
            EnsureCharacterAppearanceQueued(preload.appearance_sources);
        if (!appearance_key.empty() &&
            std::find(preload_appearance_keys.begin(), preload_appearance_keys.end(),
                      appearance_key) == preload_appearance_keys.end()) {
          preload_appearance_keys.push_back(appearance_key);
        }
      }
    }
  }

  int ts_model_found = 0;
  for (const auto &raw_path : streaming_paths) {
    ++ts_model_found;
    const auto m2_path_opt = NormalizeModelPath(raw_path);
    if (!m2_path_opt.has_value()) {
      continue;
    }
    if (ts_diag) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                         "GlueModelRenderer: tracked model='" + raw_path +
                             "' norm='" + *m2_path_opt + "'");
    }
    TrackActiveModelPath(*m2_path_opt);
  }
  if (ts_diag && ts_model_found == 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                       "GlueModelRenderer: no visible model widgets in streaming set");
  }

  PumpSharedModelResources(initial_visible_commit_boost_);
  PumpPreparedCompletions();
  const std::uint32_t frame_step_budget = initial_visible_commit_boost_
      ? kInitialGlueStreamingCommitsPerFrame
      : std::min(step_budget, kMaxGlueStreamingCommitsPerFrame);
  for (std::uint32_t s = 0; s < frame_step_budget; ++s) {
    bool advanced = false;
    for (const auto& appearance_key : active_character_appearance_keys_) {
      if (CommitOnePreparedCharacterAppearance(appearance_key)) {
        advanced = true;
        break;
      }
    }
    if (advanced) {
      continue;
    }
    for (const auto &path : active_model_paths_) {
      if (CommitOnePreparedResource(path)) {
        advanced = true;
        break;
      }
    }
    if (advanced) {
      continue;
    }
    for (const auto &appearance_key : preload_appearance_keys) {
      if (CommitOnePreparedCharacterAppearance(appearance_key)) {
        advanced = true;
        break;
      }
    }
    if (advanced) {
      continue;
    }
    for (const auto &path : preload_model_paths) {
      if (CommitOnePreparedResource(path)) {
        advanced = true;
        break;
      }
    }
    if (!advanced)
      break;
  }

  if (initial_visible_commit_boost_ &&
      (!active_model_paths_.empty() ||
       !active_character_appearance_keys_.empty())) {
    const bool model_pending = std::any_of(
        active_model_paths_.begin(), active_model_paths_.end(),
        [&](const std::string& path) {
          const auto it = loads_.find(path);
          return it == loads_.end() ||
                 (it->second.phase != LoadPhase::kReady &&
                  it->second.phase != LoadPhase::kFailed);
        });
    const bool appearance_pending = std::any_of(
        active_character_appearance_keys_.begin(),
        active_character_appearance_keys_.end(),
        [&](const std::string& key) {
          const auto it = character_appearance_loads_.find(key);
          return it == character_appearance_loads_.end() ||
                 (it->second.phase != CharacterAppearanceLoadPhase::kReady &&
                  it->second.phase != CharacterAppearanceLoadPhase::kFailed);
        });
    initial_visible_commit_boost_ = model_pending || appearance_pending;
  }
}

openwow::ui::glue::GlueStreamingCounters GlueModelRenderer::StreamingCounters() const {
  openwow::ui::glue::GlueStreamingCounters out;
  const auto model_dependency_ready = [](const LoadRecord &record) {
    return record.phase == LoadPhase::kFailed ||
           record.phase == LoadPhase::kCommitTextures ||
           record.phase == LoadPhase::kCommitModel ||
           record.phase == LoadPhase::kReady;
  };
  const auto texture_dependency_ready = [](const LoadRecord &record,
                                           const std::size_t dependency_index) {
    return record.phase == LoadPhase::kFailed || record.next_texture_idx > dependency_index;
  };
  const auto promotion_ready = [](const LoadRecord &record) {
    return record.phase == LoadPhase::kReady || record.phase == LoadPhase::kFailed;
  };

  for (const auto &path : active_model_paths_) {
    auto it = loads_.find(path);
    if (it == loads_.end()) {
      openwow::ui::glue::AccumulateGlueStreamingPathProgress(path, false, out);
      openwow::ui::glue::AccumulateGlueStreamingReadinessProgress(false, out);
      continue;
    }

    const LoadRecord &record = it->second;
    openwow::ui::glue::AccumulateGlueStreamingPathProgress(path, model_dependency_ready(record),
                                                           out);

    for (std::size_t dependency_index = 0; dependency_index < record.pending_textures.size();
         ++dependency_index) {
      openwow::ui::glue::AccumulateGlueStreamingTextureProgress(
          record.pending_textures[dependency_index].texture_path,
          texture_dependency_ready(record, dependency_index), out);
    }
    openwow::ui::glue::AccumulateGlueStreamingReadinessProgress(promotion_ready(record), out);
  }

  for (const auto& appearance_key : active_character_appearance_keys_) {
    const auto it = character_appearance_loads_.find(appearance_key);
    const bool ready =
        it != character_appearance_loads_.end() &&
        (it->second.phase == CharacterAppearanceLoadPhase::kReady ||
         it->second.phase == CharacterAppearanceLoadPhase::kFailed);
    openwow::ui::glue::AccumulateGlueStreamingReadinessProgress(ready, out);
  }
  return out;
}

void GlueModelRenderer::DestroyM2Instance(InstanceState &instance) {
  if (instance.m2_instance_id != 0) {
    const auto status =
        m2_system_.DestroyInstance(instance.m2_instance_id);
    if (status != openwow::render::m2::M2ResultStatus::kReady) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string("GlueModelRenderer: M2 instance destroy ") +
                             openwow::render::m2::M2ResultStatusName(status));
    }
  }
  ForgetM2Instance(instance);
}

std::uint32_t GlueModelRenderer::EnsureM2Instance(InstanceState &instance,
                                                  const ModelAssets &assets) {
  if (assets.model_id == 0) {
    DestroyM2Instance(instance);
    return 0;
  }

  if (instance.m2_instance_id != 0 && instance.m2_model_id != assets.model_id) {
    DestroyM2Instance(instance);
  }

  if (instance.m2_instance_id == 0) {
    const auto instance_result =
        m2_system_.CreateInstance(assets.model_id);
    instance.m2_instance_id =
        instance_result.status == openwow::render::m2::M2ResultStatus::kReady
            ? instance_result.instance_id
            : 0u;
    instance.m2_model_id = instance.m2_instance_id != 0 ? assets.model_id : 0;
  }

  return instance.m2_instance_id;
}

std::uint32_t GlueModelRenderer::EnsureM2InstanceResolved(InstanceState &instance,
                                                          const ModelAssets &assets) {
  const std::uint32_t instance_id = EnsureM2Instance(instance, assets);
  if (instance_id == 0) {
    return 0;
  }

  auto& m2_system = m2_system_;
  if (instance.sequence_restart_pending) {
    const auto restart_status = m2_system.SetAnimation(
        instance_id, static_cast<std::uint32_t>(std::max(instance.sequence, 0)));
    if (openwow::render::m2::IsTerminalM2ResultStatus(restart_status)) {
      DestroyM2Instance(instance);
      return 0;
    }
    instance.sequence_restart_pending = false;
  }
  const auto status = m2_system.SetAnimationSample(
      instance_id, static_cast<std::uint32_t>(std::max(instance.sequence, 0)),
      instance.time_ms);
  if (openwow::render::m2::IsTerminalM2ResultStatus(status)) {
    DestroyM2Instance(instance);
    return 0;
  }
  return instance_id;
}

bool GlueModelRenderer::PrepareM2InstanceForSubmit(
    InstanceState &instance, const ModelAssets &assets,
    const openwow::render::RenderMatrix4x4 &model_matrix,
    const std::optional<openwow::render::RenderVec4> &tint_color,
    const openwow::render::m2::M2BatchUniforms &uniforms,
    const std::vector<std::size_t> *visible_submesh_indices) {
  const std::uint32_t instance_id = EnsureM2Instance(instance, assets);
  if (instance_id == 0) {
    return false;
  }

  auto &m2_system = m2_system_;
  openwow::render::m2::M2ResultStatus setup_status =
      openwow::render::m2::M2ResultStatus::kReady;
  const auto merge_setup_status =
      [&setup_status](const openwow::render::m2::M2ResultStatus status) {
    setup_status = openwow::render::m2::MergeM2ResultStatus(setup_status, status);
  };
  merge_setup_status(m2_system.SetWorldTransformMatrix(instance_id, model_matrix));

  merge_setup_status(m2_system.SetAlpha(instance_id, instance.model_alpha));
  static constexpr openwow::render::RenderVec4 kIdentityTint{1.0f, 1.0f, 1.0f, 1.0f};
  merge_setup_status(m2_system.SetTintColor(instance_id, tint_color.value_or(kIdentityTint)));
  merge_setup_status(m2_system.SetBatchUniforms(instance_id, uniforms));

  if (visible_submesh_indices != nullptr) {
    merge_setup_status(m2_system.SetVisibleSubmeshIndices(instance_id, *visible_submesh_indices));
  } else {
    merge_setup_status(m2_system.ClearVisibleSubmeshIndices(instance_id));
  }

  if (instance.applied_replaceable_texture_paths != instance.replaceable_texture_paths) {
    for (const auto &[texture_type, texture_path] : instance.replaceable_texture_paths) {
      const auto applied = instance.applied_replaceable_texture_paths.find(texture_type);
      if (!texture_path.empty() &&
          (applied == instance.applied_replaceable_texture_paths.end() ||
           applied->second != texture_path)) {
        merge_setup_status(
            m2_system.SetReplaceableTexturePath(instance_id, texture_type, texture_path));
      }
    }
    if (setup_status == openwow::render::m2::M2ResultStatus::kReady) {
      for (const auto &[texture_type, texture_path] :
           instance.applied_replaceable_texture_paths) {
        (void)texture_path;
        if (!instance.replaceable_texture_paths.contains(texture_type)) {
          merge_setup_status(
              m2_system.ClearReplaceableTexturePath(instance_id, texture_type));
        }
      }
    }
    if (setup_status == openwow::render::m2::M2ResultStatus::kReady) {
      instance.applied_replaceable_texture_paths = instance.replaceable_texture_paths;
    }
  }
  if (openwow::render::m2::IsTerminalM2ResultStatus(setup_status)) {
    DestroyM2Instance(instance);
    return false;
  }
  return setup_status == openwow::render::m2::M2ResultStatus::kReady;
}

bool GlueModelRenderer::RenderSingleModel(openwow::ui::glue::GlueWidgetRuntime &widgets,
                                          const openwow::ui::glue::GlueWidgetState &widget,
                                          int view_id, int viewport_width, int viewport_height,
                                          const RenderViewOptions &options) {
  const auto m2_path_opt = NormalizeModelPath(widget.model_file);
  if (!m2_path_opt.has_value()) {
    return false;
  }
  const std::string m2_path = *m2_path_opt;
  EnsureQueued(m2_path);
  auto *assets = GetAssetsIfReady(m2_path);
  if (assets == nullptr || !assets->ok) {
    return false;
  }
  auto &m2_system = m2_system_;
  const auto model_info = m2_system.QueryModelInfo(assets->model_id);
  if (model_info.status != openwow::render::m2::M2ResultStatus::kReady ||
      !model_info.info.render_ready) {
    return false;
  }

  InstanceState &inst = instances_[widget.name];
  inst.play_sound_events = true;
  inst.completion_widget_name = widget.name;

  inst.model_alpha = std::clamp(widgets.EffectiveAlpha(widget.name), 0.0f, 1.0f);

  {
    std::string k = widget.kind;
    std::transform(k.begin(), k.end(), k.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (k == "modelffx") {
      if (!inst.model_ffx_ctx.has_value()) {
        openwow::render::ModelFfxContext c;
        c.Reset();
        inst.model_ffx_ctx = std::move(c);
      }
      if (const auto ffx_widget = widgets.ResolveModelFFXWidget(widget.name);
          ffx_widget != nullptr) {

        const std::uint8_t ghost_gate = ffx_widget->context0().ghost_branch_gate();
        inst.model_ffx_ctx->blocks[openwow::render::kModelFfxBackgroundLiveBlockIndex]
            .set_ghost_branch_gate(ghost_gate);
        inst.model_ffx_ctx->blocks[openwow::render::kModelFfxCharacterLiveBlockIndex]
            .set_ghost_branch_gate(ghost_gate);
      }
      SyncModelFfxContextFromRuntime(*inst.model_ffx_ctx, widgets, widget.name);
      inst.render_callback.fn = &openwow::render::ApplyModelFfxLightingRenderCallback;

      inst.render_callback.ctx =
          &inst.model_ffx_ctx->blocks[openwow::render::kModelFfxBackgroundLiveBlockIndex];
    } else {
      inst.render_callback = {};
      inst.model_ffx_ctx.reset();
    }
  }
  const int seq = widgets.GetSequence(widget.name);
  const std::uint64_t sequence_revision = widgets.GetSequenceRevision(widget.name);
  const int cam = widgets.GetCamera(widget.name);
  const bool model_changed = inst.model_path != m2_path;
  const bool sequence_requested = inst.sequence_revision != sequence_revision;
  if (model_changed || sequence_requested) {
    inst.model_path = m2_path;
    inst.sequence = seq;
    inst.sequence_revision = sequence_revision;
    inst.time_ms = 0;
    inst.sequence_restart_pending = true;
    inst.animation_completion_fired = false;
    InvalidateAnimationInfo(inst);
  }

  inst.camera = cam;

  if (const auto override_ms = widgets.ConsumeSequenceTimeMs(widget.name);
      override_ms.has_value()) {
    inst.time_ms = *override_ms;
  }

  const std::uint32_t instance_id = EnsureM2InstanceResolved(inst, *assets);
  if (instance_id == 0) {
    return false;
  }

  static std::uint32_t rsm_diag_ctr = 0;
  const bool rsm_diag =
      kGlueModelFrameDiagnostics && (rsm_diag_ctr++ % 300 == 0);

  const int cam_index = std::max(0, inst.camera);
  auto cam_pose =
      m2_system_.QueryInstanceCameraSample(instance_id, cam_index);
  if (cam_pose.status != openwow::render::m2::M2ResultStatus::kReady) {
    cam_pose =
        m2_system_.QueryInstanceCameraSample(instance_id, 0);
  }

  std::optional<openwow::render::RenderMatrix4x4View> billboard_view;
  openwow::render::RenderMatrix4x4 camera_inv_view{};
  if (cam_pose.status == openwow::render::m2::M2ResultStatus::kReady) {
    const bx::Vec3 camera_position = ToBxVec3(cam_pose.pose.position);
    const bx::Vec3 camera_target = ToBxVec3(cam_pose.pose.target);
    const auto billboard_camera_state =
        BuildM2CameraState(camera_position, camera_target, cam_pose.pose.fov_rad,
                           cam_pose.pose.roll_rad, cam_pose.pose.near_clip,
                           cam_pose.pose.far_clip);
    camera_inv_view = BuildM2CameraInverseViewRotation(billboard_camera_state);
    billboard_view = openwow::render::RenderMatrix4x4View{camera_inv_view};
  }

  auto bone_query = m2_system_.QueryInstanceSampleBoneMatrices(
      instance_id, billboard_view);
  if (bone_query.status != openwow::render::m2::M2ResultStatus::kReady) {
    if (rsm_diag) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "RenderSingleModel: ComputeBoneMatrices FAILED for '" + widget.name +
                             "' seq=" + std::to_string(inst.sequence));
    }
    return false;
  }
  std::vector<float> bone_mats = std::move(bone_query.bone_matrices);
  const bx::Vec3 default_camera_target{assets->bounds_center[0], assets->bounds_center[1],
                                       assets->bounds_center[2]};
  const float radius = std::max(1.0f, assets->bounds_radius);
  const float default_camera_distance = radius * kDefaultGlueCameraDistanceScale;
  const float default_camera_fov = kDefaultGlueCameraFovRadians;

  const int wx = options.render_at_origin ? 0 : ((widget.width > 0) ? widget.x : 0);
  const int wy = options.render_at_origin ? 0 : ((widget.height > 0) ? widget.y : 0);
  const int ww = (widget.width > 0) ? widget.width : viewport_width;
  const int wh = (widget.height > 0) ? widget.height : viewport_height;
  const auto view_rect = ResolveGlueModelViewRect(wx, wy, ww, wh,
                                                  options.render_at_origin ? ww : viewport_width,
                                                  options.render_at_origin ? wh : viewport_height);
  if (!view_rect.has_value()) {
    return false;
  }

  bgfx::setViewRect(static_cast<std::uint16_t>(view_id), view_rect->x, view_rect->y,
                    view_rect->width, view_rect->height);
  bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(view_id), options.framebuffer);
  bgfx::setViewMode(static_cast<bgfx::ViewId>(view_id), bgfx::ViewMode::Sequential);
  bgfx::setViewClear(static_cast<bgfx::ViewId>(view_id),
                     ResolveGlueModelViewClearFlags(options),
                     ResolveGlueModelViewClearColor(options), 1.0f, 0);

  bgfx::touch(static_cast<bgfx::ViewId>(view_id));

  const float frame_scale = widgets.GetEffectiveScale(widget.name);
  const float model_scale = widgets.GetModelScale(widget.name) * frame_scale *
                            (openwow::ui::GetCachedUiAspectVerticalScale() / 0.6f);
  const float facing = widgets.GetFacing(widget.name);
  float model_x = 0.0f;
  float model_y = 0.0f;
  float model_z = 0.0f;
  widgets.GetModelPosition(widget.name, model_x, model_y, model_z);
  model_x *= frame_scale;
  model_y *= frame_scale;
  model_z *= frame_scale;
  const openwow::render::RenderMatrix4x4 model_mtx =
      openwow::render::BuildM2ModelInstanceTransform(
          model_x, model_y, model_z, facing, model_scale);

  const bx::Vec3 default_camera_eye{
      default_camera_target.x,
      default_camera_target.y - default_camera_distance,
      default_camera_target.z + default_camera_distance * kDefaultGlueCameraHeightScale};
  const bx::Vec3 default_camera_at = default_camera_target;

  bx::Vec3 eye = default_camera_eye;
  bx::Vec3 at = default_camera_at;
  float fov = default_camera_fov;
  float roll = 0.0f;
  float near_clip = kDefaultGlueCameraNearClip;
  float far_clip = std::max(kDefaultGlueCameraMinimumFarClip,
                           default_camera_distance * kDefaultGlueCameraFarClipDistanceScale);
  const bool using_m2_camera = cam_pose.status == openwow::render::m2::M2ResultStatus::kReady;
  if (using_m2_camera) {
    eye = ToBxVec3(cam_pose.pose.position);
    at = ToBxVec3(cam_pose.pose.target);
    fov = cam_pose.pose.fov_rad;
    roll = cam_pose.pose.roll_rad;
    near_clip = cam_pose.pose.near_clip;
    far_clip = cam_pose.pose.far_clip;
  }

  if (inst.animation_info_refresh_pending) {
    const auto animation_info =
        m2_system_.QueryInstanceAnimationInfo(instance_id);
    if (animation_info.status == openwow::render::m2::M2ResultStatus::kReady &&
        animation_info.info.sequence_index !=
            openwow::render::m2::kInvalidM2AnimationSequenceIndex) {
      inst.resolved_animation_id = animation_info.info.resolved_animation_id;
      inst.animation_duration_ms = animation_info.info.duration_ms;
      inst.resolved_sequence_index = animation_info.info.sequence_index;
      inst.animation_info_refresh_pending = false;
    }
  }

  const bool resolved_seq_valid =
      inst.resolved_sequence_index !=
      openwow::render::m2::kInvalidM2AnimationSequenceIndex;

  const float projection_aspect = openwow::math::projection::ComputeAspectPx(ww, wh);

  auto camera_state = BuildM2CameraState(eye, at, fov, roll, near_clip, far_clip);
  if (using_m2_camera) {

    camera_state = openwow::render::m2::M2System::TransformCameraPoseByModelMatrix(
        camera_state, openwow::render::RenderMatrix4x4View{model_mtx});
    eye = ToBxVec3(camera_state.position);
    at = ToBxVec3(camera_state.target);
  }
  const M2GlueViewProjection view_projection =
      BuildM2GlueViewProjection(camera_state, projection_aspect);
  const openwow::render::RenderMatrix4x4 &view = view_projection.view;
  const openwow::render::RenderMatrix4x4 &proj = view_projection.projection;
  const float fov_vert_deg = view_projection.vertical_fov_degrees;
  bgfx::setViewTransform(static_cast<std::uint16_t>(view_id), view.data(), proj.data());

  if (!inst.camera_source_diag_logged) {
    inst.camera_source_diag_logged = true;
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kDebug,
        "GlueModelRenderer: camera source for '" + widget.name +
            "' model='" + m2_path +
            "': m2cam=" + std::to_string(using_m2_camera ? 1 : 0) +
            " cam_index=" + std::to_string(cam_index) +
            " cam_status=" + std::to_string(static_cast<int>(cam_pose.status)) +
            " behavior_id=" + std::to_string(inst.sequence) +
            " resolved_seq=" +
            std::to_string(resolved_seq_valid
                               ? static_cast<int>(inst.resolved_sequence_index)
                               : -1) +
            " resolved_anim_id=" +
            std::to_string(resolved_seq_valid
                               ? static_cast<int>(inst.resolved_animation_id)
                               : -1) +
            " eye=(" + std::to_string(eye.x) + "," + std::to_string(eye.y) + "," +
            std::to_string(eye.z) + ") at=(" + std::to_string(at.x) + "," +
            std::to_string(at.y) + "," + std::to_string(at.z) + ") fov=" +
            std::to_string(bx::toDeg(fov)) + " bounds_radius=" +
            std::to_string(assets->bounds_radius) + " default_cam_dist=" +
            std::to_string(default_camera_distance));
  }

  openwow::render::glue::GlueSceneLightingSnapshot host_scene_snapshot;
  host_scene_snapshot.directional_lights.reserve(model_info.info.light_count);
  host_scene_snapshot.point_lights.reserve(model_info.info.light_count);
  for (int light_index = 0; light_index < static_cast<int>(model_info.info.light_count);
       ++light_index) {
    const auto light_sample =
        m2_system_.QueryInstanceLightSample(
            instance_id, light_index, bone_mats);
    if (light_sample.status != openwow::render::m2::M2ResultStatus::kReady ||
        !light_sample.light.visible) {
      continue;
    }

    const auto& sample = light_sample.light;
    openwow::render::glue::AppendGlueSceneM2Light(
        host_scene_snapshot, sample,
        openwow::render::RenderMatrix4x4View{model_mtx});
    if (rsm_diag) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "  m2_light[" + std::to_string(light_index) +
              "]: type=" + std::to_string(sample.type) + " local_pos=(" +
              std::to_string(sample.position[0]) + "," +
              std::to_string(sample.position[1]) + "," +
              std::to_string(sample.position[2]) + ") ambient=(" +
              std::to_string(sample.ambient_color[0]) + "," +
              std::to_string(sample.ambient_color[1]) + "," +
              std::to_string(sample.ambient_color[2]) + ")*" +
              std::to_string(sample.ambient_intensity) + " diffuse=(" +
              std::to_string(sample.diffuse_color[0]) + "," +
              std::to_string(sample.diffuse_color[1]) + "," +
              std::to_string(sample.diffuse_color[2]) + ")*" +
              std::to_string(sample.diffuse_intensity) + " attenuation=(" +
              std::to_string(sample.attenuation_start) + "," +
              std::to_string(sample.attenuation_end) + ")");
    }
  }
  const openwow::render::ModelRenderCallbackLightingState host_scene_lighting =
      openwow::render::glue::ProjectGlueSceneLighting(host_scene_snapshot);
  const openwow::render::RenderVec3 host_local_center{
      assets->bounds_center[0], assets->bounds_center[1], assets->bounds_center[2]};
  const openwow::render::RenderVec3 host_world_center =
      openwow::render::glue::TransformGlueSceneLightPoint(
          host_local_center, openwow::render::RenderMatrix4x4View{model_mtx});

  const auto& ghost_light_sample = character_select_ghost_light_;
  const auto *attached_scene_binding = FindAttachedCharacterSceneBinding(widget.name);
  auto *attached_scene = attached_scene_binding != nullptr ? attached_scene_binding->scene : nullptr;
  const bool attached_scene_is_ghost =
      attached_scene != nullptr && attached_scene->SelectedCharacterIsGhost();
  const auto callback_result = openwow::render::RunModelRenderCallbackPipeline(
      &inst, inst.render_callback, model_mtx, attached_scene_is_ghost,
      ghost_light_sample.has_value() ? &*ghost_light_sample : nullptr,
      &host_scene_lighting);
  const auto &transforms = callback_result.transforms;

  if (rsm_diag) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "RenderSingleModel: '" + widget.name + "' transforms=" + std::to_string(transforms.size()) +
            " tex_units=" + std::to_string(model_info.info.texture_unit_count) +
            " submeshes=" + std::to_string(model_info.info.submesh_count) +
            " verts=" + std::to_string(model_info.info.vertex_count) +
            " render_ready=" + std::to_string(model_info.info.render_ready) +
            " textures_loaded=" + std::to_string(model_info.info.gpu_texture_count) +
            " cam_pose=" +
            std::to_string(cam_pose.status == openwow::render::m2::M2ResultStatus::kReady) +
            " eye=(" + std::to_string(eye.x) +
            "," + std::to_string(eye.y) + "," + std::to_string(eye.z) + ")" + " at=(" +
            std::to_string(at.x) + "," + std::to_string(at.y) + "," + std::to_string(at.z) + ")" +
            " fov=" + std::to_string(bx::toDeg(fov)) + " vert=" + std::to_string(fov_vert_deg) +
            " near=" + std::to_string(near_clip) + " far=" + std::to_string(far_clip));

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "  view[0..3]=" + std::to_string(view[0]) + "," + std::to_string(view[1]) +
                           "," + std::to_string(view[2]) + "," + std::to_string(view[3]) +
                           " view[4..7]=" + std::to_string(view[4]) + "," +
                           std::to_string(view[5]) + "," + std::to_string(view[6]) + "," +
                           std::to_string(view[7]) + " view[8..11]=" + std::to_string(view[8]) +
                           "," + std::to_string(view[9]) + "," + std::to_string(view[10]) + "," +
                           std::to_string(view[11]) + " view[12..15]=" + std::to_string(view[12]) +
                           "," + std::to_string(view[13]) + "," + std::to_string(view[14]) + "," +
                           std::to_string(view[15]));
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "  proj[0..3]=" + std::to_string(proj[0]) + "," + std::to_string(proj[1]) +
                           "," + std::to_string(proj[2]) + "," + std::to_string(proj[3]) +
                           " proj[4..7]=" + std::to_string(proj[4]) + "," +
                           std::to_string(proj[5]) + "," + std::to_string(proj[6]) + "," +
                           std::to_string(proj[7]) + " proj[8..11]=" + std::to_string(proj[8]) +
                           "," + std::to_string(proj[9]) + "," + std::to_string(proj[10]) + "," +
                           std::to_string(proj[11]) + " proj[12..15]=" + std::to_string(proj[12]) +
                           "," + std::to_string(proj[13]) + "," + std::to_string(proj[14]) + "," +
                           std::to_string(proj[15]));

    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "  model_mtx[0..3]=" + std::to_string(model_mtx[0]) + "," + std::to_string(model_mtx[1]) +
            "," + std::to_string(model_mtx[2]) + "," + std::to_string(model_mtx[3]) +
            " [4..7]=" + std::to_string(model_mtx[4]) + "," + std::to_string(model_mtx[5]) + "," +
            std::to_string(model_mtx[6]) + "," + std::to_string(model_mtx[7]) +
            " [8..11]=" + std::to_string(model_mtx[8]) + "," + std::to_string(model_mtx[9]) + "," +
            std::to_string(model_mtx[10]) + "," + std::to_string(model_mtx[11]) +
            " [12..15]=" + std::to_string(model_mtx[12]) + "," + std::to_string(model_mtx[13]) +
            "," + std::to_string(model_mtx[14]) + "," + std::to_string(model_mtx[15]));

    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "  homogeneousDepth=" + std::to_string(bgfx::getCaps()->homogeneousDepth) +
            " rendererType=" + std::to_string(static_cast<int>(bgfx::getRendererType())));
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "  model colors=" + std::to_string(model_info.info.color_count) +
            " transparencies=" + std::to_string(model_info.info.transparency_count) +
            " global_sequences=" + std::to_string(model_info.info.global_sequence_count) +
            " anim_durations=" + std::to_string(model_info.info.animation_duration_count) +
            " sequence=" + std::to_string(inst.sequence) +
            " time_ms=" + std::to_string(inst.time_ms));

    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "  camera_source: m2cam=" + std::to_string(using_m2_camera ? 1 : 0) +
            " cam_index=" + std::to_string(cam_index) +
            " cam_status=" + std::to_string(static_cast<int>(cam_pose.status)) +
            " resolved_seq=" +
            std::to_string(resolved_seq_valid
                               ? static_cast<int>(inst.resolved_sequence_index)
                               : -1) +
            " resolved_anim_id=" +
            std::to_string(resolved_seq_valid
                               ? static_cast<int>(inst.resolved_animation_id)
                               : -1) +
            " seq_valid=" + std::to_string(resolved_seq_valid ? 1 : 0) +
            " bounds_radius=" + std::to_string(assets->bounds_radius) +
            " default_cam_dist=" + std::to_string(default_camera_distance));
  }

  const auto* callback_lighting =
      callback_result.lighting_state ? &*callback_result.lighting_state : nullptr;
  openwow::render::m2::M2BatchUniforms light_template;
  bool model_has_lighting = false;
  if (callback_lighting != nullptr) {
    light_template = openwow::render::glue::BuildGlueM2LightingUniforms(
        *callback_lighting, host_world_center);
    model_has_lighting =
        openwow::render::glue::HasRenderableGlueSceneLighting(*callback_lighting);
  }

  GlueFogParameters authored_fog;
  widgets.GetFogColor(widget.name, authored_fog.red, authored_fog.green,
                      authored_fog.blue);
  authored_fog.near_distance = widgets.GetFogNear(widget.name);
  authored_fog.far_distance = widgets.GetFogFar(widget.name);
  const GlueFogParameters widget_fog = ResolveGlueFogParameters(
      authored_fog, callback_lighting);
  const float widget_glow = widgets.GetGlow(widget.name);

  if (rsm_diag) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "  fog: near=" + std::to_string(widget_fog.near_distance) +
                           " far=" + std::to_string(widget_fog.far_distance) + " color=(" +
                           std::to_string(widget_fog.red) + "," + std::to_string(widget_fog.green) + "," +
                           std::to_string(widget_fog.blue) + ")" +
                           " glow=" + std::to_string(widget_glow));
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "  lighting: active=" + std::to_string(model_has_lighting ? 1 : 0) +
                           " count=" + std::to_string(light_template.light_count[0]));
  }

  openwow::render::m2::M2BatchUniforms base_uniforms = light_template;
  if (widget_fog.far_distance > 0.0f) {
    base_uniforms.material_flags[1] = 0.0f;
    base_uniforms.fog_params[0] = widget_fog.near_distance;
    base_uniforms.fog_params[1] = widget_fog.far_distance;
    base_uniforms.fog_color[0] = widget_fog.red;
    base_uniforms.fog_color[1] = widget_fog.green;
    base_uniforms.fog_color[2] = widget_fog.blue;
    base_uniforms.fog_color[3] = 1.0f;
  } else {
    base_uniforms.material_flags[1] = 1.0f;
  }

  int submitted_transform_count = 0;
  for (const auto &tr : transforms) {
    const openwow::render::RenderMatrix4x4 transform_mtx = tr.Matrix4x4();
    if (PrepareM2InstanceForSubmit(inst, *assets, transform_mtx, std::nullopt,
                                   base_uniforms, nullptr)) {
      const auto render_result = m2_system_.RenderInstance(
          static_cast<std::uint16_t>(view_id), inst.m2_instance_id,
          openwow::render::RenderMatrix4x4View{view});
      if (render_result.status == openwow::render::m2::M2ResultStatus::kReady) {
        ++submitted_transform_count;
      } else if (openwow::render::m2::IsTerminalM2ResultStatus(render_result.status)) {
        DestroyM2Instance(inst);
        break;
      }
    }
  }

  if (rsm_diag) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "RenderSingleModel: '" + widget.name +
                           "' submitted shared M2 draw path for transforms=" +
                           std::to_string(submitted_transform_count));
  }

  if (attached_scene != nullptr) {
    auto &scene = *attached_scene;
    std::string host_kind = widget.kind;
    std::transform(host_kind.begin(), host_kind.end(), host_kind.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool host_is_model_ffx = host_kind == "modelffx";

    auto apply_attachment_index = [&](std::uint32_t parent_node, const ModelAssets &source_assets,
                                      const std::vector<float> &source_bone_mats,
                                      std::uint32_t attachment_index) {
      const auto attachment =
          m2_system.QueryModelAttachmentInfo(source_assets.model_id, attachment_index);
      const auto transform =
          openwow::render::glue::ResolveGlueAttachmentLinkTransform(attachment,
                                                                    source_bone_mats);
      if (!transform.has_value()) {
        return;
      }
      scene.attachments().SetAttachmentTransform(parent_node, attachment_index, *transform);
    };

    apply_attachment_index(scene.background_root_node(), *assets, bone_mats, 0u);
    apply_attachment_index(scene.background_root_node(), *assets, bone_mats, 1u);

    const auto character_hierarchy_color_multiplier =
        scene.character_hierarchy_color_multiplier();
    const std::string character_display_prefix =
        BuildCharacterDisplayInstancePrefix(widget.name, scene.current_display_owner());

    enum class ReplaceableTextureUpdate {
      kClear,
      kReplace,
      kPreserve,
    };
    struct AttachedRenderOptions {
      bool apply_appearance_geosets{false};

      std::size_t callback_block_index{
          openwow::render::kModelFfxCharacterLiveBlockIndex};
      std::array<float, 4> color_multiplier{1.0f, 1.0f, 1.0f, 1.0f};
      const std::unordered_map<std::uint32_t, std::string> *replaceable_texture_paths{nullptr};
      ReplaceableTextureUpdate replaceable_texture_update{ReplaceableTextureUpdate::kClear};
      const std::vector<std::size_t> *visible_submesh_override{nullptr};
      std::vector<std::size_t> *rendered_visible_submeshes{nullptr};
      bool render_selection_triangle{false};
      const openwow::ui::glue::GlueCharSelectScene::CharacterEquipmentPose *equipment_pose{
          nullptr};
    };
    struct AttachedRenderResult {
      bool pose_ready{false};
      bool instance_valid{false};
      bool all_submissions_ready{false};
      std::uint32_t submitted_draw_count{0u};
      std::uint32_t submitted_geometry_draw_count{0u};

      [[nodiscard]] bool visible() const noexcept {
        return instance_valid && all_submissions_ready && submitted_draw_count != 0u;
      }
    };

    auto render_attached =
        [&](const std::string &instance_key, const std::string &raw_m2_path,
            openwow::render::Mat4 node_world, int sequence, bool play_sound_events,
            const AttachedRenderOptions &options,
            const std::function<void(const ModelAssets &, const std::vector<float> &, std::size_t)>
                &after_pose) -> AttachedRenderResult {
      AttachedRenderResult result;
      const auto m2opt = NormalizeModelPath(raw_m2_path);
      if (!m2opt.has_value() || m2opt->empty())
        return result;
      const std::string m2_path = *m2opt;

      EnsureQueued(m2_path);
      auto *a = GetAssetsIfReady(m2_path);
      if (a == nullptr || !a->ok)
        return result;
      const auto attached_info = m2_system.QueryModelInfo(a->model_id);
      if (attached_info.status != openwow::render::m2::M2ResultStatus::kReady ||
          !attached_info.info.render_ready) {
        return result;
      }

      InstanceState &inst2 = instances_[instance_key];
      inst2.model_alpha = inst.model_alpha;
      inst2.play_sound_events = play_sound_events;
      inst2.completion_widget_name.clear();
      if (inst2.model_path != m2_path || inst2.sequence != sequence) {
        inst2.model_path = m2_path;
        inst2.sequence = sequence;
        inst2.camera = 0;
        inst2.time_ms = 0;
        inst2.sequence_restart_pending = true;
        inst2.animation_completion_fired = false;
        inst2.key_bone_animation_ids_initialized = false;
        InvalidateAnimationInfo(inst2);
      }

      const std::uint32_t attached_instance_id = EnsureM2InstanceResolved(inst2, *a);
      if (attached_instance_id == 0) {
        return result;
      }
      result.instance_valid = true;
      if (options.equipment_pose != nullptr) {

        constexpr auto kOwnedFingerSlots = std::to_array<std::uint32_t>({
            8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u,
        });
        for (const std::uint32_t slot : kOwnedFingerSlots) {
          const auto desired = options.equipment_pose->key_bone_animation_ids[slot];
          const bool changed = !inst2.key_bone_animation_ids_initialized ||
                               inst2.key_bone_animation_ids[slot] != desired;
          if (changed && desired.has_value()) {
            (void)m2_system.SetAnimationSlotRequest(
                attached_instance_id, slot,
                {
                    .animation_lookup_id = -1,
                    .animation_id = *desired,
                    .sub_animation_index = -1,
                    .loop_count = 0,
                    .speed = 1.0f,
                });
          } else if (changed) {
            (void)m2_system.ClearAnimationSlot(attached_instance_id, slot);
          }
          inst2.key_bone_animation_ids[slot] = desired;
        }
        (void)m2_system.SetAnimationSlotTimes(attached_instance_id, kOwnedFingerSlots,
                                              inst2.time_ms);
        inst2.key_bone_animation_ids_initialized = true;
      }
      if (host_is_model_ffx) {
        if (!inst.model_ffx_ctx.has_value()) {
          openwow::render::ModelFfxContext c;
          c.Reset();
          inst.model_ffx_ctx = std::move(c);
        }

        inst2.render_callback.fn = &openwow::render::ApplyModelFfxLightingRenderCallback;
        inst2.render_callback.ctx =
            &inst.model_ffx_ctx->blocks[options.callback_block_index];
      } else {
        inst2.render_callback = {};
      }
      inst2.model_ffx_ctx.reset();
      if (options.replaceable_texture_update == ReplaceableTextureUpdate::kReplace &&
          options.replaceable_texture_paths != nullptr) {
        inst2.replaceable_texture_paths = *options.replaceable_texture_paths;
      } else if (options.replaceable_texture_update == ReplaceableTextureUpdate::kClear) {
        inst2.replaceable_texture_paths.clear();
      }

      auto attached_bone_query =
          m2_system_.QueryInstanceSampleBoneMatrices(
              attached_instance_id, billboard_view);
      if (attached_bone_query.status != openwow::render::m2::M2ResultStatus::kReady) {
        return result;
      }
      result.pose_ready = true;
      std::vector<float> bone_mats2 = std::move(attached_bone_query.bone_matrices);
      const std::size_t bone_count2 = bone_mats2.size() / 16u;

      std::vector<openwow::render::m2::M2LightSample> attached_model_lights;
      attached_model_lights.reserve(attached_info.info.light_count);
      for (int light_index = 0;
           light_index < static_cast<int>(attached_info.info.light_count);
           ++light_index) {
        const auto light_sample =
            m2_system_.QueryInstanceLightSample(
                attached_instance_id, light_index, bone_mats2);
        if (light_sample.status == openwow::render::m2::M2ResultStatus::kReady &&
            light_sample.light.visible) {
          attached_model_lights.push_back(light_sample.light);
        }
      }

      std::vector<std::size_t> visible_submesh_indices;
      if (options.visible_submesh_override != nullptr) {
        visible_submesh_indices = *options.visible_submesh_override;
      } else if (options.apply_appearance_geosets) {
        const auto section_ids = m2_system.QueryModelSubmeshSectionIds(a->model_id);
        if (section_ids.status != openwow::render::m2::M2ResultStatus::kReady) {
          return result;
        }
        visible_submesh_indices.reserve(section_ids.section_ids.size());
        for (std::size_t submesh_index = 0; submesh_index < section_ids.section_ids.size();
             ++submesh_index) {
          if (scene.IsAppearanceGeosetVisible(section_ids.section_ids[submesh_index])) {
            visible_submesh_indices.push_back(submesh_index);
          }
        }
      }
      if (options.rendered_visible_submeshes != nullptr) {
        *options.rendered_visible_submeshes = visible_submesh_indices;
      }

      bool attempted_submission = false;
      bool every_submission_ready = true;
      std::vector<openwow::render::SelectionTriangleVertices> pending_selection_triangles;
      auto submit_attached_geometry =
          [&](const openwow::render::RenderMatrix4x4 &parent_base_mtx) {
        if (!result.instance_valid) {
          return;
        }
        openwow::render::RenderMatrix4x4 node_mtx{};
        std::copy(std::begin(node_world.m), std::end(node_world.m),
                  node_mtx.begin());
        openwow::render::RenderMatrix4x4 final_base{};
        openwow::math::row_major_mat4x4::Multiply4x4(
            final_base.data(), node_mtx.data(), parent_base_mtx.data());

        const openwow::render::ModelRenderCallbackLightingState*
            attached_scene_lighting = &host_scene_lighting;
        std::optional<openwow::render::ModelRenderCallbackLightingState>
            extended_scene_lighting;
        if (!attached_model_lights.empty()) {
          auto extended_snapshot = host_scene_snapshot;
          extended_snapshot.directional_lights.reserve(
              extended_snapshot.directional_lights.size() + attached_model_lights.size());
          extended_snapshot.point_lights.reserve(
              extended_snapshot.point_lights.size() + attached_model_lights.size());
          for (const auto& light : attached_model_lights) {
            openwow::render::glue::AppendGlueSceneM2Light(
                extended_snapshot, light,
                openwow::render::RenderMatrix4x4View{final_base});
          }
          extended_scene_lighting =
              openwow::render::glue::ProjectGlueSceneLighting(extended_snapshot);
          attached_scene_lighting = &*extended_scene_lighting;
        }

        const openwow::render::RenderVec3 attached_local_center{
            a->bounds_center[0], a->bounds_center[1], a->bounds_center[2]};
        const openwow::render::RenderVec3 attached_world_center =
            openwow::render::glue::TransformGlueSceneLightPoint(
                attached_local_center,
                openwow::render::RenderMatrix4x4View{final_base});
        const auto attached_callback_result = openwow::render::RunModelRenderCallbackPipeline(
            &inst2, inst2.render_callback, final_base, attached_scene_is_ghost,
            ghost_light_sample.has_value() ? &*ghost_light_sample : nullptr,
            attached_scene_lighting);
        const auto &attached_transforms = attached_callback_result.transforms;
        const auto *attached_callback_lighting = attached_callback_result.lighting_state
                                                     ? &*attached_callback_result.lighting_state
                                                     : nullptr;
        openwow::render::m2::M2BatchUniforms callback_light_template;
        if (attached_callback_lighting != nullptr) {
          callback_light_template =
              openwow::render::glue::BuildGlueM2LightingUniforms(
                  *attached_callback_lighting, attached_world_center);
        }
        const GlueFogParameters attached_fog = ResolveGlueFogParameters(
            authored_fog, attached_callback_lighting);

        openwow::render::m2::M2BatchUniforms attached_uniforms = callback_light_template;
        if (attached_fog.far_distance > 0.0f) {
          attached_uniforms.material_flags[1] = 0.0f;
          attached_uniforms.fog_params[0] = attached_fog.near_distance;
          attached_uniforms.fog_params[1] = attached_fog.far_distance;
          attached_uniforms.fog_color[0] = attached_fog.red;
          attached_uniforms.fog_color[1] = attached_fog.green;
          attached_uniforms.fog_color[2] = attached_fog.blue;
          attached_uniforms.fog_color[3] = 1.0f;
        } else {
          attached_uniforms.material_flags[1] = 1.0f;
        }

        const auto *visible_filter =
            options.apply_appearance_geosets ? &visible_submesh_indices : nullptr;

        for (const auto &tr2 : attached_transforms) {
          attempted_submission = true;
          const openwow::render::RenderMatrix4x4 active_model_mtx = tr2.Matrix4x4();
          if (PrepareM2InstanceForSubmit(inst2, *a, active_model_mtx,
                                         options.color_multiplier, attached_uniforms,
                                         visible_filter)) {
            const auto render_result = m2_system_.RenderInstance(
                static_cast<std::uint16_t>(view_id), inst2.m2_instance_id,
                openwow::render::RenderMatrix4x4View{view});
            result.submitted_draw_count += render_result.submitted_draw_count;
            result.submitted_geometry_draw_count +=
                render_result.submitted_geometry_draw_count;
            every_submission_ready =
                every_submission_ready &&
                render_result.status == openwow::render::m2::M2ResultStatus::kReady &&
                render_result.submitted_geometry_draw_count != 0u;
            if (openwow::render::m2::IsTerminalM2ResultStatus(render_result.status)) {
              DestroyM2Instance(inst2);
              result.instance_valid = false;
              break;
            }
          } else if (inst2.m2_instance_id == 0u) {
            result.instance_valid = false;
            every_submission_ready = false;
          } else {
            every_submission_ready = false;
          }

          if (options.render_selection_triangle) {
            if (const auto triangle = m2_system.BuildSelectionTriangleVerticesForSample(
                    a->model_id, bone_mats2, bone_count2, active_model_mtx);
                triangle.has_value()) {
              pending_selection_triangles.push_back(*triangle);
            }
          }
        }
      };

      if (transforms.empty()) {
        submit_attached_geometry(model_mtx);
      } else {
        for (const auto &parent_tr : transforms) {
          submit_attached_geometry(parent_tr.Matrix4x4());
        }
      }
      result.all_submissions_ready = attempted_submission && every_submission_ready;
      if (result.all_submissions_ready) {
        if (after_pose) {
          after_pose(*a, bone_mats2, bone_count2);
        }
        for (const auto &triangle : pending_selection_triangles) {
          SubmitSelectionTriangleOverlay(view_id, triangle, options.color_multiplier);
        }
      }
      return result;
    };

    const std::string &char_path = scene.selected_character_model_path();
    bool character_ready = false;
    if (!char_path.empty()) {
      const std::string character_instance_key =
          character_display_prefix + ":body";
      const auto& appearance_sources =
          scene.character_appearance_texture_sources();
      const bool appearance_declared = appearance_sources.HasBody();
      const auto* appearance_texture_paths =
          GetReadyCharacterAppearanceTexturePaths(appearance_sources);

      const bool appearance_load_failed =
          appearance_declared && appearance_texture_paths == nullptr &&
          CharacterAppearanceLoadFailed(appearance_sources);
      const auto update_character_attachments =
          [&](const ModelAssets &char_assets,
              const std::vector<float> &char_bone_mats,
              std::size_t ) {
        for (const auto &effect : scene.character_effect_models()) {
          if (effect.active) {
            apply_attachment_index(scene.character_node(), char_assets,
                                   char_bone_mats, effect.attachment_index);
          }
        }
        for (const auto &equipment : scene.character_equipment_models()) {
          if (equipment.active) {
            apply_attachment_index(scene.character_node(), char_assets,
                                   char_bone_mats, equipment.attachment_index);
          }
        }
      };

      const auto render_published_fallback = [&]() -> bool {
        const auto published = instances_.find(character_instance_key);
        if (published == instances_.end() || !published->second.character_body.valid ||
            !published->second.replaceable_texture_paths.contains(1u) ||
            published->second.model_path.empty()) {
          return false;
        }
        const auto &publication = published->second.character_body;
        return render_attached(
            character_instance_key, published->second.model_path,
            scene.graph().GetWorldTransform(scene.character_node()),
            published->second.sequence, true,
            AttachedRenderOptions{
                .apply_appearance_geosets = true,
                .callback_block_index = openwow::render::kModelFfxCharacterLiveBlockIndex,
                .color_multiplier = publication.color_multiplier,
                .replaceable_texture_update = ReplaceableTextureUpdate::kPreserve,
                .visible_submesh_override = &publication.visible_submesh_indices,
            },
            {}).visible();
      };

      if (!appearance_declared || appearance_load_failed) {
        character_ready = render_attached(
            character_instance_key, char_path,
            scene.graph().GetWorldTransform(scene.character_node()),
            static_cast<int>(scene.character_equipment_pose().base_animation_id), true,
            AttachedRenderOptions{
                .apply_appearance_geosets = true,
                .callback_block_index = openwow::render::kModelFfxCharacterLiveBlockIndex,
                .color_multiplier = character_hierarchy_color_multiplier,
                .equipment_pose = &scene.character_equipment_pose(),
            },
            update_character_attachments).visible();
      } else if (appearance_texture_paths != nullptr) {
        const auto published = instances_.find(character_instance_key);
        const auto normalized_char_path = NormalizeModelPath(char_path);
        const bool desired_already_published =
            published != instances_.end() && published->second.character_body.valid &&
            normalized_char_path.has_value() &&
            published->second.model_path == *normalized_char_path &&
            published->second.replaceable_texture_paths == *appearance_texture_paths;
        const std::string candidate_key = desired_already_published
                                              ? character_instance_key
                                              : character_instance_key + ":candidate";
        std::vector<std::size_t> published_submeshes;
        const auto desired_render = render_attached(
            candidate_key, char_path,
            scene.graph().GetWorldTransform(scene.character_node()),
            static_cast<int>(scene.character_equipment_pose().base_animation_id), true,
            AttachedRenderOptions{
                .apply_appearance_geosets = true,
                .callback_block_index = openwow::render::kModelFfxCharacterLiveBlockIndex,
                .color_multiplier = character_hierarchy_color_multiplier,
                .replaceable_texture_paths = appearance_texture_paths,
                .replaceable_texture_update =
                    appearance_texture_paths != nullptr
                        ? ReplaceableTextureUpdate::kReplace
                        : ReplaceableTextureUpdate::kClear,
                .rendered_visible_submeshes = &published_submeshes,
                .equipment_pose = &scene.character_equipment_pose(),
            },
            update_character_attachments);
        character_ready = CharacterBodyReadyForPublication(
            desired_render.instance_valid,
            desired_render.all_submissions_ready,
            desired_render.submitted_geometry_draw_count,
            published_submeshes.size());
        if (character_ready) {
          auto &publication = instances_[candidate_key].character_body;
          publication.valid = true;
          publication.visible_submesh_indices = std::move(published_submeshes);
          publication.color_multiplier = character_hierarchy_color_multiplier;
          if (!desired_already_published) {
            auto candidate = instances_.extract(candidate_key);
            auto previous = instances_.extract(character_instance_key);
            if (!previous.empty()) {
              DestroyM2Instance(previous.mapped());
            }
            candidate.key() = character_instance_key;
            instances_.insert(std::move(candidate));
          }
        } else if (!desired_already_published) {
          character_ready = render_published_fallback();
        }
      } else {
        character_ready = render_published_fallback();
      }
    }

    if (character_ready) {
      for (const auto &equipment : scene.character_equipment_models()) {
        if (!equipment.active || equipment.model_path.empty()) {
          continue;
        }
        std::unordered_map<std::uint32_t, std::string>
            equipment_texture_paths;
        if (!equipment.texture_path.empty()) {
          equipment_texture_paths.emplace(2u, equipment.texture_path);
        }
        const std::string equipment_instance_key =
            character_display_prefix + ":equipment:" +
            std::to_string(equipment.equipment_slot) + ":" +
            std::to_string(equipment.attachment_index);
        const bool equipment_ready = render_attached(
            equipment_instance_key, equipment.model_path,
            scene.graph().GetWorldTransform(equipment.node_id), 0, false,
            AttachedRenderOptions{
                .callback_block_index = openwow::render::kModelFfxCharacterLiveBlockIndex,
                .color_multiplier = character_hierarchy_color_multiplier,
                .replaceable_texture_paths =
                    equipment_texture_paths.empty() ? nullptr : &equipment_texture_paths,
                .replaceable_texture_update = equipment_texture_paths.empty()
                                                  ? ReplaceableTextureUpdate::kClear
                                                  : ReplaceableTextureUpdate::kReplace,
                .render_selection_triangle = equipment.selection_triangle_candidate,
            },
            [&](const ModelAssets &equipment_assets,
                const std::vector<float> &equipment_bone_mats,
                const std::size_t ) {
              for (const auto &child : equipment.child_models) {
                if (!child.active) {
                  continue;
                }
                apply_attachment_index(equipment.node_id, equipment_assets,
                                       equipment_bone_mats, child.attachment_index);
              }
            }).visible();
        if (!equipment_ready) {
          continue;
        }
        for (const auto &child : equipment.child_models) {
          if (!child.active || child.model_path.empty()) {
            continue;
          }
          (void)render_attached(
              equipment_instance_key + ":visual:" +
                  std::to_string(child.attachment_index),
              child.model_path, scene.graph().GetWorldTransform(child.node_id), 0,
              false,
              AttachedRenderOptions{
                  .callback_block_index = openwow::render::kModelFfxCharacterLiveBlockIndex,
                  .color_multiplier = character_hierarchy_color_multiplier,
              },
              {});
        }
      }
      for (const auto &effect : scene.character_effect_models()) {
        if (!effect.active || effect.model_path.empty()) {
          continue;
        }
        (void)render_attached(character_display_prefix +
                                  ":effect:" + std::to_string(effect.attachment_index),
                              effect.model_path, scene.graph().GetWorldTransform(effect.node_id), 0,
                              false,
                              AttachedRenderOptions{
                                  .callback_block_index =
                                      openwow::render::kModelFfxCharacterLiveBlockIndex,
                                  .color_multiplier = character_hierarchy_color_multiplier,
                              },
                              {});
      }
    }

    if (scene.current_display_owner().valid()) {
      if (const auto &prop = scene.prop_model_path(); prop.has_value() && !prop->empty()) {
      std::unordered_map<std::uint32_t, std::string> prop_texture_paths;
      for (std::size_t texture_index = 0;
           texture_index < scene.prop_texture_paths().size(); ++texture_index) {
        const auto& texture_path = scene.prop_texture_paths()[texture_index];
        if (!texture_path.empty()) {
          prop_texture_paths.emplace(
              static_cast<std::uint32_t>(11u + texture_index), texture_path);
        }
      }
      (void)render_attached(character_display_prefix + ":prop", *prop,
                            scene.graph().GetWorldTransform(scene.prop_node()), 0, false,
                            AttachedRenderOptions{
                                .callback_block_index = openwow::render::kModelFfxPetLiveBlockIndex,
                                .color_multiplier =
                                    {1.0f, 1.0f, 1.0f, scene.prop_model_alpha()},
                                .replaceable_texture_paths =
                                    prop_texture_paths.empty() ? nullptr : &prop_texture_paths,
                                .replaceable_texture_update = prop_texture_paths.empty()
                                                                  ? ReplaceableTextureUpdate::kClear
                                                                  : ReplaceableTextureUpdate::kReplace,
                            },
                            {});
      }
    }
  }

  return true;
}

void GlueModelRenderer::InitSelectionTriangleResources() {
  if (selection_triangle_resources_init_) {
    return;
  }
  selection_triangle_resources_init_ = true;

  const auto handles = openwow::render::ui::LoadUiProgram();
  if (!openwow::render::ui::IsValidUiProgram(handles)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueModelRenderer: selection triangle UI shader load failed");
    return;
  }

  selection_triangle_program_ = handles.program;
  selection_triangle_sampler_ = handles.s_tex;

  selection_triangle_layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
}

void GlueModelRenderer::SubmitSelectionTriangleOverlay(
    const int view_id, const openwow::render::SelectionTriangleVertices &triangle,
    const std::array<float, 4> &color_multiplier) {
  InitSelectionTriangleResources();
  if (!bgfx::isValid(selection_triangle_program_) || !bgfx::isValid(selection_triangle_sampler_) ||
      !bgfx::isValid(shared_white_texture_)) {
    return;
  }

  constexpr std::uint32_t kVertexCount = 3u;
  constexpr std::uint32_t kIndexCount = 3u;
  if (bgfx::getAvailTransientVertexBuffer(kVertexCount, selection_triangle_layout_) <
          kVertexCount ||
      bgfx::getAvailTransientIndexBuffer(kIndexCount) < kIndexCount) {
    return;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, kVertexCount, selection_triangle_layout_);
  bgfx::allocTransientIndexBuffer(&tib, kIndexCount);

  auto *vertices = reinterpret_cast<SelectionTriangleVertex *>(tvb.data);
  const std::uint32_t color = PackSelectionTriangleColor(color_multiplier);
  for (std::size_t index = 0; index < triangle.positions.size(); ++index) {
    vertices[index] = SelectionTriangleVertex{
        .x = triangle.positions[index][0],
        .y = triangle.positions[index][1],
        .z = triangle.positions[index][2],
        .u = 0.0f,
        .v = 0.0f,
        .abgr = color,
    };
  }

  auto *indices = reinterpret_cast<std::uint16_t *>(tib.data);
  indices[0] = 0u;
  indices[1] = 2u;
  indices[2] = 1u;

  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, selection_triangle_sampler_, shared_white_texture_,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::setState(
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
  bgfx::submit(static_cast<std::uint16_t>(view_id), selection_triangle_program_);
}

}
