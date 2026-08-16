#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::data::model {

struct M2Array {
  std::uint32_t count{0};
  std::uint32_t offset{0};
};
static_assert(sizeof(M2Array) == 8, "M2Array expected to be 8 bytes.");

struct M2Header {
  char magic[4]{};
  std::uint32_t version{0};

  M2Array name;
  std::uint32_t global_flags{0};

  M2Array global_sequences;
  M2Array animations;
  M2Array animation_lookup;
  M2Array bones;
  M2Array key_bone_lookup;
  M2Array vertices;
  std::uint32_t num_views{0};
  M2Array colors;
  M2Array textures;
  M2Array transparencies;
  M2Array uv_animations;
  M2Array tex_replace;
  M2Array render_flags;
  M2Array bone_lookup;
  M2Array tex_lookup;
  M2Array tex_units;
  M2Array transparency_lookup;
  M2Array uv_anim_lookup;

  float bounding_box_min[3]{};
  float bounding_box_max[3]{};
  float bounding_sphere_radius{0.0f};
  float collision_box_min[3]{};
  float collision_box_max[3]{};
  float collision_sphere_radius{0.0f};

  M2Array bounding_triangles;
  M2Array bounding_vertices;
  M2Array bounding_normals;
  M2Array attachments;
  M2Array attachment_lookup;
  M2Array events;
  M2Array lights;
  M2Array cameras;
  M2Array camera_lookup;
  M2Array ribbon_emitters;
  M2Array particle_emitters;
  M2Array shader_combos;
};

struct M2Vec3 {
  float x{0.0F};
  float y{0.0F};
  float z{0.0F};
};
static_assert(sizeof(M2Vec3) == 12, "M2Vec3 expected to be 12 bytes.");

struct M2Vec2 {
  float x{0.0F};
  float y{0.0F};
};
static_assert(sizeof(M2Vec2) == 8, "M2Vec2 expected to be 8 bytes.");

struct M2Quat16 {
  std::int16_t x{0};
  std::int16_t y{0};
  std::int16_t z{0};
  std::int16_t w{0};
};
static_assert(sizeof(M2Quat16) == 8, "M2Quat16 expected to be 8 bytes.");

struct M2TrackSegment {
  std::uint32_t first_time{0};
  std::uint32_t time_count{0};
  std::uint32_t first_value{0};
  std::uint32_t value_count{0};
};
static_assert(sizeof(M2TrackSegment) == 16, "M2TrackSegment expected to be 16 bytes.");

template <typename T> struct M2Track {
  std::uint16_t interpolation{0};
  std::int16_t global_sequence{-1};

  std::vector<M2TrackSegment> segments;

  std::vector<std::uint32_t> key_times_ms;

  std::vector<T> key_values;

  [[nodiscard]] std::size_t SetCount() const noexcept { return segments.size(); }

  [[nodiscard]] std::span<const std::uint32_t> SetTimes(
      const std::size_t set_index) const noexcept {
    if (set_index >= segments.size()) {
      return {};
    }
    const M2TrackSegment &segment = segments[set_index];
    return {key_times_ms.data() + segment.first_time, segment.time_count};
  }

  [[nodiscard]] std::span<const T> SetValues(const std::size_t set_index) const noexcept {
    if (set_index >= segments.size()) {
      return {};
    }
    const M2TrackSegment &segment = segments[set_index];
    return {key_values.data() + segment.first_value, segment.value_count};
  }

  void AppendSet(const std::span<const std::uint32_t> times,
                 const std::span<const T> values) {
    segments.push_back(M2TrackSegment{
        .first_time = static_cast<std::uint32_t>(key_times_ms.size()),
        .time_count = static_cast<std::uint32_t>(times.size()),
        .first_value = static_cast<std::uint32_t>(key_values.size()),
        .value_count = static_cast<std::uint32_t>(values.size()),
    });
    key_times_ms.insert(key_times_ms.end(), times.begin(), times.end());
    key_values.insert(key_values.end(), values.begin(), values.end());
  }

  void AppendEmptySet() {
    AppendSet(std::span<const std::uint32_t>{}, std::span<const T>{});
  }
};

inline constexpr std::uint32_t kM2BoneFlagBillboardSpherical = 0x08u;
inline constexpr std::uint32_t kM2BoneFlagBillboardCylindricalLockX = 0x10u;
inline constexpr std::uint32_t kM2BoneFlagBillboardCylindricalLockY = 0x20u;
inline constexpr std::uint32_t kM2BoneFlagBillboardCylindricalLockZ = 0x40u;
inline constexpr std::uint32_t kM2BoneFlagBillboardMask =
    kM2BoneFlagBillboardSpherical | kM2BoneFlagBillboardCylindricalLockX |
    kM2BoneFlagBillboardCylindricalLockY | kM2BoneFlagBillboardCylindricalLockZ;

struct M2DiscreteTrack {
  std::uint16_t interpolation{0};
  std::int16_t global_sequence{-1};
  std::vector<std::vector<std::uint32_t>> times_ms;
};

struct M2Bone {
  std::int32_t key_bone_id{0};
  std::uint32_t flags{0};
  std::int16_t parent{-1};
  std::uint16_t submesh_id{0};
  M2Track<M2Vec3> translation;
  M2Track<M2Quat16> rotation;
  M2Track<M2Vec3> scaling;
  float pivot[3]{0.0F, 0.0F, 0.0F};
};

struct M2RenderFlags {
  std::uint16_t flags{0};
  std::uint16_t blend_mode{0};
};

struct M2Transparency {
  M2Track<std::uint16_t> alpha;
};

struct M2ColorAnimation {
  M2Track<M2Vec3> color;
  M2Track<std::uint16_t> alpha;
};

struct M2UvAnimation {
  M2Track<M2Vec3> translation;
  M2Track<M2Quat16> rotation;
  M2Track<M2Vec3> scaling;
};

struct M2Light {
  std::uint16_t type{0};
  std::int16_t bone{-1};
  float position[3]{0.0f, 0.0f, 0.0f};
  M2Track<M2Vec3> ambient_color;
  M2Track<float> ambient_intensity;
  M2Track<M2Vec3> diffuse_color;
  M2Track<float> diffuse_intensity;
  M2Track<float> attenuation_start;
  M2Track<float> attenuation_end;
  M2Track<std::uint16_t> visibility;
};

struct M2Attachment {
  std::uint32_t id{0};
  std::uint16_t bone{0};
  std::uint16_t unknown{0};
  float position[3]{0.0f, 0.0f, 0.0f};
};

struct M2Event {
  std::uint32_t identifier{0};
  std::uint32_t data{0};
  std::int16_t bone{-1};
  std::uint16_t unknown{0};
  float position[3]{0.0f, 0.0f, 0.0f};
  M2DiscreteTrack timings;
};

struct M2Camera {
  std::uint32_t type{0};
  float fov{0.0F};
  float far_clip{0.0F};
  float near_clip{0.0F};
  M2Track<M2Vec3> position;
  M2Vec3 position_base{};
  M2Track<M2Vec3> target;
  M2Vec3 target_base{};
  M2Track<float> roll;
};

struct M2Vertex {
  float position[3];
  std::uint8_t bone_weights[4];
  std::uint8_t bone_indices[4];
  float normal[3];
  float texcoord0[2];
  float texcoord1[2];
};
static_assert(sizeof(M2Vertex) == 48, "WotLK M2Vertex expected to be 48 bytes.");

struct M2Texture {
  std::uint32_t type{0};
  std::uint32_t flags{0};
  M2Array name;
  std::string name_text;
};

struct M2AnimationSequenceRecord {
  static constexpr std::size_t kStride = 64;

  std::uint16_t animation_id{0};
  std::uint16_t sub_animation_id{0};
  std::uint32_t length_ms{0};
  float move_speed{0.0f};
  std::uint32_t flags{0};
  std::int16_t frequency{0};
  std::uint32_t replay_min{0};
  std::uint32_t replay_max{0};
  std::uint32_t blend_time_ms{0};
  float bounding_box_min[3]{};
  float bounding_box_max[3]{};
  float bounding_radius{0.0f};
  std::int16_t next_animation{-1};
  std::uint16_t alias_next{0};
};

inline constexpr std::uint32_t kM2SequenceFlagPlayOnce = 0x1u;
inline constexpr std::uint32_t kM2SequenceFlagLoading = 0x10u;
inline constexpr std::uint32_t kM2SequenceFlagDataResident = 0x20u;
inline constexpr std::uint32_t kM2SequenceFlagAlias = 0x40u;

inline constexpr std::uint32_t kM2SequenceFlagBlendSourceClampedAtEnd = 0x80u;

[[nodiscard]] std::vector<std::uint16_t> BuildM2FirstSequenceIndexByAnimationId(
    const std::vector<M2AnimationSequenceRecord> &animation_sequences);
[[nodiscard]] bool M2SequenceUsesExternalData(
    const M2AnimationSequenceRecord &sequence) noexcept;
[[nodiscard]] std::optional<std::size_t> ResolveM2SequenceDataOwner(
    const std::vector<M2AnimationSequenceRecord> &animation_sequences,
    std::size_t sequence_index);
[[nodiscard]] std::optional<std::string> BuildM2SequenceDataPath(
    std::string_view model_path,
    const std::vector<M2AnimationSequenceRecord> &animation_sequences,
    std::size_t sequence_index);

template <typename T> struct M2ParticleLifetimeTrack {
  std::vector<std::uint16_t> times;
  std::vector<T> values;
};

struct M2ParticleEmitter {
  std::int32_t id{-1};
  std::uint32_t flags{0};
  float position[3]{0.0f, 0.0f, 0.0f};
  std::uint16_t bone{0};
  std::uint16_t texture{0};

  std::string geometry_model_name;
  std::string recursion_model_name;

  std::uint8_t blending_type{0};
  std::uint8_t emitter_type{0};
  std::uint16_t particle_color_index{0};

  std::uint16_t texture_tile_rotation{0};
  std::uint16_t texture_tile_rows{1};
  std::uint16_t texture_tile_cols{1};

  M2Track<float> emission_speed;
  M2Track<float> speed_variation;
  M2Track<float> vertical_range;
  M2Track<float> horizontal_range;
  M2Track<float> gravity;
  M2Track<float> lifespan;
  float lifespan_vary{0.0f};
  M2Track<float> emission_rate;
  float emission_rate_vary{0.0f};
  M2Track<float> emission_area_length;
  M2Track<float> emission_area_width;
  M2Track<float> z_source;

  M2ParticleLifetimeTrack<M2Vec3> color_track;
  M2ParticleLifetimeTrack<std::uint16_t> alpha_track;
  M2ParticleLifetimeTrack<M2Vec2> scale_track;
  M2Vec2 scale_vary{};
  M2ParticleLifetimeTrack<std::uint16_t> head_cell_track;
  M2ParticleLifetimeTrack<std::uint16_t> tail_cell_track;

  float tail_length{0.0f};
  float twinkle_speed{0.0f};
  float twinkle_percent{1.0f};
  float twinkle_scale_min{1.0f};
  float twinkle_scale_max{1.0f};

  float position_delta_multiplier{0.0f};

  float drag{0.0f};
  float base_spin{0.0f};
  float base_spin_vary{0.0f};
  float spin{0.0f};
  float spin_vary{0.0f};
  float tumble_min[3]{0.0f, 0.0f, 0.0f};
  float tumble_max[3]{0.0f, 0.0f, 0.0f};
  float wind_vector[3]{0.0f, 0.0f, 0.0f};
  float wind_time{0.0f};
  float follow_speed1{0.0f};
  float follow_scale1{0.0f};
  float follow_speed2{0.0f};
  float follow_scale2{0.0f};
  std::vector<M2Vec3> spline_points;

  M2Track<std::uint8_t> enabled_in;
};

struct M2RibbonEmitter {
  std::uint32_t ribbon_id{0};
  std::uint32_t bone_index{0};
  float position[3]{0, 0, 0};
  std::vector<std::uint16_t> texture_indices;
  std::vector<std::uint16_t> material_indices;
  M2Track<M2Vec3> color;
  M2Track<std::int16_t> alpha;
  M2Track<float> height_above;
  M2Track<float> height_below;
  float edges_per_second{0.0f};
  float edge_lifetime{0.0f};
  float gravity{0.0f};
  std::uint16_t texture_rows{0};
  std::uint16_t texture_cols{0};
  M2Track<std::uint16_t> tex_slot;
  M2Track<std::uint8_t> visibility;
  std::int16_t priority_plane{0};
  std::uint16_t padding{0};
};

struct M2BonePoseIndex {
  static constexpr std::size_t kBonesPerWord = 64u;

  static constexpr std::size_t kTranslationRow = 0u;
  static constexpr std::size_t kRotationRow = 1u;
  static constexpr std::size_t kScalingRow = 2u;
  static constexpr std::size_t kRowsPerAnimation = 3u;

  std::uint32_t animation_count{0};

  std::uint32_t words_per_row{0};

  std::vector<std::uint64_t> keyless_words;

  std::vector<std::uint64_t> finite_pivot_words;

  [[nodiscard]] const std::uint64_t *KeylessRowsFor(const int animation_index) const noexcept {
    if (keyless_words.empty() || words_per_row == 0u) {
      return nullptr;
    }
    std::size_t row = animation_index > 0 ? static_cast<std::size_t>(animation_index) : 0u;
    if (row >= static_cast<std::size_t>(animation_count)) {
      row = 0u;
    }
    return keyless_words.data() +
           row * kRowsPerAnimation * static_cast<std::size_t>(words_per_row);
  }

  [[nodiscard]] bool TestBit(const std::uint64_t *const rows, const std::size_t row,
                             const std::size_t bone_index) const noexcept {
    const std::uint64_t word =
        rows[row * static_cast<std::size_t>(words_per_row) + bone_index / kBonesPerWord];
    return ((word >> (bone_index % kBonesPerWord)) & 1u) != 0u;
  }

  [[nodiscard]] bool HasFinitePivot(const std::size_t bone_index) const noexcept {
    return !finite_pivot_words.empty() && TestBit(finite_pivot_words.data(), 0u, bone_index);
  }
};

inline constexpr std::uint16_t kM2NoSequenceForAnimationId = 0xFFFFu;

struct M2Model {
  M2Header header;
  std::string name;
  std::vector<std::uint32_t> global_sequences_ms;
  std::vector<std::uint32_t> animation_durations_ms;
  std::vector<M2AnimationSequenceRecord> animation_sequences;
  std::vector<std::uint16_t> animation_lookup;

  std::vector<std::uint16_t> first_sequence_index_by_animation_id;
  std::vector<M2Bone> bones;
  std::vector<std::int16_t> key_bone_lookup;
  std::vector<M2Vertex> vertices;
  std::vector<M2Texture> textures;
  std::vector<M2RenderFlags> render_flags;
  std::vector<M2ColorAnimation> colors;
  std::vector<M2Transparency> transparencies;
  std::vector<M2UvAnimation> uv_animations;
  std::vector<M2Camera> cameras;
  std::vector<M2Attachment> attachments;
  std::vector<std::int16_t> attachment_lookups;
  std::vector<M2Event> events;
  std::vector<M2Light> lights;
  std::vector<M2RibbonEmitter> ribbon_emitters;
  std::vector<M2ParticleEmitter> particle_emitters;
  std::vector<std::uint16_t> bounding_triangles;
  std::vector<M2Vec3> bounding_vertices;
  std::vector<M2Vec3> bounding_normals;

  std::vector<std::uint16_t> bone_lookup;

  std::vector<std::uint16_t> tex_lookup;
  std::vector<std::uint16_t> tex_units;
  std::vector<std::uint16_t> transparency_lookup;
  std::vector<std::uint16_t> uv_anim_lookup;
  std::vector<std::uint16_t> camera_lookups;
  std::vector<std::uint16_t> shader_combos;

  M2BonePoseIndex bone_pose_index;
};

void RebuildM2BonePoseIndex(M2Model &model);

struct SkinHeader {
  char magic[4]{};
  std::uint32_t n_indices{0};
  std::uint32_t ofs_indices{0};
  std::uint32_t n_triangles{0};
  std::uint32_t ofs_triangles{0};
  std::uint32_t n_properties{0};
  std::uint32_t ofs_properties{0};
  std::uint32_t n_submeshes{0};
  std::uint32_t ofs_submeshes{0};
  std::uint32_t n_texture_units{0};
  std::uint32_t ofs_texture_units{0};
  std::uint32_t lod{0};
};
static_assert(sizeof(SkinHeader) == 48, "WotLK SkinHeader expected to be 48 bytes.");

struct SkinSubmesh {
  std::uint16_t section_id{0};
  std::uint16_t level{0};
  std::uint16_t vertex_start{0};
  std::uint16_t vertex_count{0};
  std::uint16_t index_start{0};
  std::uint16_t index_count{0};
  std::uint16_t bone_count{0};
  std::uint16_t bone_combo_index{0};
  std::uint16_t bone_influences{0};
  std::uint16_t center_bone_index{0};
  float center_pos[3]{};
  float sort_pos[3]{};
  float sort_radius{0.0F};
};
static_assert(sizeof(SkinSubmesh) == 48, "WotLK SkinSubmesh expected to be 48 bytes.");

struct SkinTextureUnit {

  std::uint8_t flags{0};
  std::int8_t priority_plane{0};
  std::uint16_t shading{0};
  std::uint16_t submesh_index{0};
  std::uint16_t submesh_index2{0};
  std::uint16_t color_index{0};
  std::uint16_t render_flags_index{0};
  std::uint16_t tex_unit_number{0};
  std::uint16_t mode{0};
  std::uint16_t texture_index{0};
  std::uint16_t tex_unit_lookup{0};
  std::uint16_t transparency_index{0};
  std::uint16_t uv_anim_index{0};
};
static_assert(sizeof(SkinTextureUnit) == 24, "WotLK SkinTextureUnit expected to be 24 bytes.");
static_assert(offsetof(SkinTextureUnit, flags) == 0u);
static_assert(offsetof(SkinTextureUnit, priority_plane) == 1u);
static_assert(offsetof(SkinTextureUnit, shading) == 2u);

struct M2Skin {
  SkinHeader header;
  std::vector<std::uint16_t> indices;
  std::vector<std::uint16_t> triangles;
  std::vector<std::uint8_t> properties;
  std::vector<SkinSubmesh> submeshes;
  std::vector<SkinTextureUnit> texture_units;
};

struct M2LoadResult {
  bool ok{false};
  std::string error;
  M2Model model;
};

struct SkinLoadResult {
  bool ok{false};
  std::string error;
  M2Skin skin;
};

struct M2HeaderBoundsResult {
  bool ok{false};
  std::string error;
  float bounding_box_min[3]{};
  float bounding_box_max[3]{};
  float bounding_sphere_radius{0.0f};
};

inline constexpr std::size_t kM2HeaderBoundsPrefixBytes = 216u;

using M2ExternalFileLoader = std::function<std::vector<std::uint8_t>(const std::string &)>;

M2LoadResult LoadM2FromBytes(const std::vector<std::uint8_t> &bytes);
M2LoadResult LoadM2FromBytes(const std::vector<std::uint8_t> &bytes,
                             const std::string &virtual_path,
                             const M2ExternalFileLoader &external_file_loader);
SkinLoadResult LoadSkinFromBytes(const std::vector<std::uint8_t> &bytes);

[[nodiscard]] M2HeaderBoundsResult ParseM2HeaderBounds(
    std::span<const std::uint8_t> prefix_bytes);

M2LoadResult LoadM2(const openwow::vfs::VirtualFileSystem &vfs, const std::string &virtual_path);
SkinLoadResult LoadSkin(const openwow::vfs::VirtualFileSystem &vfs,
                        const std::string &virtual_path);

}
