#include "openwow/data/model/m2_model.h"

#include "openwow/data/model/binary_reader.h"
#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace openwow::data::model {

namespace {

constexpr std::uint32_t kSequenceFlagFixupInputBit = 0x1u;
constexpr std::uint32_t kSequenceFlagFixupPromotedBit = 0x80u;
constexpr std::uint32_t kM2GlobalFlagHasTextureCombinerCombos = 0x8u;

[[nodiscard]] bool ShouldClearSequenceFlagFixupInputBit(const std::uint16_t animation_id) {
  switch (animation_id) {
  case 0u:
  case 4u:
  case 5u:
  case 0x0Du:
  case 0x29u:
  case 0x2Au:
  case 0x2Bu:
  case 0x2Cu:
  case 0x2Du:
  case 0x45u:
  case 0x77u:
  case 0x78u:
  case 0x8Fu:
  case 0xDFu:
    return true;
  default:
    return false;
  }
}

void FixupAnimationSequenceFlags(M2AnimationSequenceRecord *sequence) {
  if (sequence == nullptr) {
    return;
  }

  if ((sequence->flags & kSequenceFlagFixupInputBit) != 0) {
    sequence->flags |= kSequenceFlagFixupPromotedBit;
  }
  if (ShouldClearSequenceFlagFixupInputBit(sequence->animation_id)) {
    sequence->flags &= ~kSequenceFlagFixupInputBit;
  }
}

bool MagicEq(const char magic[4], const char *expected4) {
  return expected4 != nullptr && magic[0] == expected4[0] && magic[1] == expected4[1] &&
         magic[2] == expected4[2] && magic[3] == expected4[3];
}

std::string BuildSequenceDataPathForOwner(std::string_view model_path,
                                          std::uint16_t animation_id,
                                          std::uint16_t sub_animation_id) {
  std::string out(model_path);
  const auto dot = out.find_last_of('.');
  if (dot != std::string::npos) {
    out.resize(dot);
  }

  char suffix[32]{};
  std::snprintf(suffix, sizeof(suffix), "%04u-%02u.anim", static_cast<unsigned int>(animation_id),
                static_cast<unsigned int>(sub_animation_id));
  out += suffix;
  return out;
}

struct ExternalSequenceSources {
  std::vector<BinaryReader> readers;
  std::vector<std::vector<std::uint8_t>> blobs;
  std::vector<int> reader_index_by_sequence;
  std::vector<bool> expects_external_data;
};

ExternalSequenceSources
BuildExternalSequenceSources(const std::vector<M2AnimationSequenceRecord> &animation_sequences,
                             std::string_view model_path,
                             const M2ExternalFileLoader *external_file_loader) {
  ExternalSequenceSources sources;
  sources.reader_index_by_sequence.assign(animation_sequences.size(), -1);
  sources.expects_external_data.assign(animation_sequences.size(), false);
  sources.readers.reserve(animation_sequences.size());
  sources.blobs.reserve(animation_sequences.size());

  if (animation_sequences.empty()) {
    return sources;
  }

  for (std::size_t sequence_index = 0; sequence_index < animation_sequences.size();
       ++sequence_index) {
    if (M2SequenceUsesExternalData(animation_sequences[sequence_index])) {
      sources.expects_external_data[sequence_index] = true;
    }
  }

  if (model_path.empty() || external_file_loader == nullptr || !(*external_file_loader)) {
    return sources;
  }

  std::unordered_map<std::string, int> cached_reader_indices;
  for (std::size_t sequence_index = 0; sequence_index < animation_sequences.size();
       ++sequence_index) {
    const auto &sequence = animation_sequences[sequence_index];
    if (!M2SequenceUsesExternalData(sequence)) {
      continue;
    }
    const auto sequence_path =
        BuildM2SequenceDataPath(model_path, animation_sequences, sequence_index);
    if (!sequence_path.has_value()) {
      continue;
    }

    const auto cache_it = cached_reader_indices.find(*sequence_path);
    if (cache_it != cached_reader_indices.end()) {
      sources.reader_index_by_sequence[sequence_index] = cache_it->second;
      continue;
    }

    auto bytes = (*external_file_loader)(*sequence_path);
    if (bytes.empty()) {
      cached_reader_indices.emplace(*sequence_path, -1);
      continue;
    }

    sources.blobs.push_back(std::move(bytes));
    sources.readers.emplace_back(sources.blobs.back().data(), sources.blobs.back().size());

    const int reader_index = static_cast<int>(sources.readers.size()) - 1;
    cached_reader_indices.emplace(*sequence_path, reader_index);
    sources.reader_index_by_sequence[sequence_index] = reader_index;
  }

  return sources;
}

std::optional<M2Array> ReadM2Array(const BinaryReader &r, std::size_t *off) {
  if (off == nullptr)
    return std::nullopt;
  const auto count = r.ReadU32(*off + 0);
  const auto ofs = r.ReadU32(*off + 4);
  if (!count.has_value() || !ofs.has_value())
    return std::nullopt;
  *off += 8;
  return M2Array{.count = *count, .offset = *ofs};
}

template <typename T>
bool ReadOptionalSkinChunk(const BinaryReader &reader, const std::uint32_t count,
                           const std::uint32_t offset, std::vector<T> *out) {
  if (out == nullptr) {
    return false;
  }

  out->clear();
  if (count == 0) {
    return true;
  }

  const auto span =
      reader.ReadSpan<T>(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
  if (!span.has_value()) {
    return false;
  }

  out->assign(span->begin(), span->end());
  return true;
}

bool ReadOptionalSkinPropertyBytes(const BinaryReader &reader, const std::uint32_t record_count,
                                   const std::uint32_t offset, std::vector<std::uint8_t> *out) {
  if (out == nullptr) {
    return false;
  }

  out->clear();
  if (record_count == 0) {
    return true;
  }

  constexpr std::size_t kBytesPerSkinPropertyRecord = 4u;
  const std::size_t byte_count =
      static_cast<std::size_t>(record_count) * kBytesPerSkinPropertyRecord;
  if (byte_count / kBytesPerSkinPropertyRecord != static_cast<std::size_t>(record_count)) {
    return false;
  }

  const auto bytes = reader.ReadBytes(static_cast<std::size_t>(offset), byte_count);
  if (!bytes.has_value()) {
    return false;
  }

  out->assign(bytes->begin(), bytes->end());
  return true;
}

template <typename T>
bool ValidateOptionalSkinChunk(const BinaryReader &reader, const std::uint32_t count,
                               const std::uint32_t offset) {
  if (count == 0) {
    return true;
  }

  return reader.ReadSpan<T>(static_cast<std::size_t>(offset), static_cast<std::size_t>(count))
      .has_value();
}

struct M2TrackHeader {
  std::uint16_t interpolation{0};
  std::int16_t global_sequence{-1};
  M2Array times;
  M2Array values;
};

struct M2DiscreteTrackHeader {
  std::uint16_t interpolation{0};
  std::int16_t global_sequence{-1};
  M2Array times;
};
static_assert(sizeof(M2DiscreteTrackHeader) == 12,
              "M2DiscreteTrackHeader expected to be 12 bytes.");

std::optional<M2TrackHeader> ReadTrackHeader(const BinaryReader &r, std::size_t off) {
  const auto interp = r.ReadU16(off + 0);
  const auto global_seq = r.ReadI16(off + 2);
  if (!interp.has_value() || !global_seq.has_value())
    return std::nullopt;
  std::size_t cursor = off + 4;
  const auto times = ReadM2Array(r, &cursor);
  const auto values = ReadM2Array(r, &cursor);
  if (!times.has_value() || !values.has_value())
    return std::nullopt;
  return M2TrackHeader{
      .interpolation = *interp, .global_sequence = *global_seq, .times = *times, .values = *values};
}

std::optional<M2DiscreteTrackHeader> ReadDiscreteTrackHeader(const BinaryReader &r,
                                                             std::size_t off) {
  const auto interp = r.ReadU16(off + 0);
  const auto global_seq = r.ReadI16(off + 2);
  if (!interp.has_value() || !global_seq.has_value())
    return std::nullopt;
  std::size_t cursor = off + 4;
  const auto times = ReadM2Array(r, &cursor);
  if (!times.has_value())
    return std::nullopt;
  return M2DiscreteTrackHeader{
      .interpolation = *interp, .global_sequence = *global_seq, .times = *times};
}

template <typename T>
bool ReadTrackData(const BinaryReader &r, const M2TrackHeader &h,
                   const ExternalSequenceSources *external_sources, M2Track<T> *out,
                   std::string *error, std::size_t values_per_key = 1) {
  if (out == nullptr)
    return false;
  if (values_per_key == 0)
    values_per_key = 1;
  out->interpolation = h.interpolation;
  out->global_sequence = h.global_sequence;
  out->segments.clear();
  out->key_times_ms.clear();
  out->key_values.clear();

  if (h.times.count == 0 || h.values.count == 0) {
    return true;
  }
  const std::size_t outer_times = static_cast<std::size_t>(h.times.count);
  const std::size_t outer_values = static_cast<std::size_t>(h.values.count);
  if (outer_times != outer_values) {
    if (error)
      *error = "M2: track outer array count mismatch";
    return false;
  }

  const auto times_outer =
      r.ReadSpan<M2Array>(static_cast<std::size_t>(h.times.offset), outer_times);
  const auto values_outer =
      r.ReadSpan<M2Array>(static_cast<std::size_t>(h.values.offset), outer_values);
  if (!times_outer.has_value() || !values_outer.has_value()) {
    if (error)
      *error = "M2: track outer arrays out of bounds";
    return false;
  }

  out->segments.reserve(outer_times);
  for (std::size_t i = 0; i < outer_times; ++i) {
    const M2Array &t_arr = (*times_outer)[i];
    const M2Array &v_arr = (*values_outer)[i];
    const BinaryReader *source_reader = &r;
    const bool per_sequence_track = h.global_sequence < 0;
    const bool expects_external_data = per_sequence_track && external_sources != nullptr &&
                                       i < external_sources->expects_external_data.size() &&
                                       external_sources->expects_external_data[i];
    if (expects_external_data) {
      const int reader_index = external_sources->reader_index_by_sequence[i];
      if (reader_index < 0 ||
          static_cast<std::size_t>(reader_index) >= external_sources->readers.size()) {
        out->AppendEmptySet();
        continue;
      }
      source_reader = &external_sources->readers[static_cast<std::size_t>(reader_index)];
    }

    const auto times = source_reader->ReadVector<std::uint32_t>(
        static_cast<std::size_t>(t_arr.offset), static_cast<std::size_t>(t_arr.count));
    if (v_arr.count > std::numeric_limits<std::size_t>::max() / values_per_key) {
      if (error)
        *error = "M2: track inner value count overflows";
      return false;
    }
    const std::size_t value_count = static_cast<std::size_t>(v_arr.count) * values_per_key;
    const auto values =
        source_reader->ReadVector<T>(static_cast<std::size_t>(v_arr.offset), value_count);
    if (!times.has_value() || !values.has_value()) {
      if (expects_external_data) {
        if (error)
          *error = "M2: external sequence track data out of bounds";
        return false;
      }
      if (error)
        *error = "M2: track inner arrays out of bounds";
      return false;
    }
    out->AppendSet(*times, *values);
  }
  return true;
}

bool ReadDiscreteTrackTimes(const BinaryReader &r, const M2DiscreteTrackHeader &h,
                            const ExternalSequenceSources *external_sources, M2DiscreteTrack *out,
                            std::string *error) {
  if (out == nullptr)
    return false;
  out->interpolation = h.interpolation;
  out->global_sequence = h.global_sequence;
  out->times_ms.clear();

  if (h.times.count == 0) {
    return true;
  }

  const std::size_t outer_times = static_cast<std::size_t>(h.times.count);
  const auto times_outer =
      r.ReadSpan<M2Array>(static_cast<std::size_t>(h.times.offset), outer_times);
  if (!times_outer.has_value()) {
    if (error)
      *error = "M2: discrete track outer arrays out of bounds";
    return false;
  }

  out->times_ms.reserve(outer_times);
  for (std::size_t i = 0; i < outer_times; ++i) {
    const M2Array &t_arr = (*times_outer)[i];
    const BinaryReader *source_reader = &r;
    const bool per_sequence_track = h.global_sequence < 0;
    const bool expects_external_data = per_sequence_track && external_sources != nullptr &&
                                       i < external_sources->expects_external_data.size() &&
                                       external_sources->expects_external_data[i];
    if (expects_external_data) {
      const int reader_index = external_sources->reader_index_by_sequence[i];
      if (reader_index < 0 ||
          static_cast<std::size_t>(reader_index) >= external_sources->readers.size()) {
        out->times_ms.emplace_back();
        continue;
      }
      source_reader = &external_sources->readers[static_cast<std::size_t>(reader_index)];
    }

    const auto times = source_reader->ReadVector<std::uint32_t>(
        static_cast<std::size_t>(t_arr.offset), static_cast<std::size_t>(t_arr.count));
    if (!times.has_value()) {
      if (expects_external_data) {
        if (error)
          *error = "M2: external sequence event track data out of bounds";
        return false;
      }
      if (error)
        *error = "M2: discrete track inner arrays out of bounds";
      return false;
    }
    out->times_ms.push_back(*times);
  }
  return true;
}

struct M2ParticleLifetimeTrackHeader {
  M2Array times;
  M2Array values;
};

std::optional<M2ParticleLifetimeTrackHeader>
ReadM2ParticleLifetimeTrackHeader(const BinaryReader &r, std::size_t off) {
  std::size_t cursor = off;
  const auto times = ReadM2Array(r, &cursor);
  const auto values = ReadM2Array(r, &cursor);
  if (!times.has_value() || !values.has_value())
    return std::nullopt;
  return M2ParticleLifetimeTrackHeader{.times = *times, .values = *values};
}

template <typename T>
bool ReadM2ParticleLifetimeTrackData(const BinaryReader &r, const M2ParticleLifetimeTrackHeader &h,
                                     M2ParticleLifetimeTrack<T> *out, std::string *error) {
  if (out == nullptr)
    return false;
  out->times.clear();
  out->values.clear();
  if (h.times.count == 0u && h.values.count == 0u)
    return true;
  if (h.times.count != h.values.count) {
    if (error)
      *error = "M2: particle lifetime track timestamp/value count mismatch";
    return false;
  }
  const std::size_t sample_count = static_cast<std::size_t>(h.times.count);
  const auto times =
      r.ReadVector<std::uint16_t>(static_cast<std::size_t>(h.times.offset), sample_count);
  const auto values = r.ReadVector<T>(static_cast<std::size_t>(h.values.offset), sample_count);
  if (!times.has_value() || !values.has_value()) {
    if (error)
      *error = "M2: particle lifetime track arrays out of bounds";
    return false;
  }
  for (std::size_t index = 1; index < times->size(); ++index) {
    if ((*times)[index] > 32767u || (*times)[index] < (*times)[index - 1u]) {
      if (error)
        *error = "M2: particle lifetime timestamps must be monotonic in [0,32767]";
      return false;
    }
  }
  if (!times->empty() && times->front() > 32767u) {
    if (error)
      *error = "M2: particle lifetime timestamps must be monotonic in [0,32767]";
    return false;
  }
  out->times = *times;
  out->values = *values;
  return true;
}

struct M2HeaderFixedPrefixParse {
  M2Header header;
  std::size_t offset_after{0};
};

std::optional<M2HeaderFixedPrefixParse> ReadM2HeaderFixedPrefix(
    const BinaryReader &r, std::string *error) {
  M2Header h{};
  const auto magic = r.ReadBytes(0, 4);
  if (!magic.has_value()) {
    if (error) *error = "M2: missing magic";
    return std::nullopt;
  }
  std::memcpy(h.magic, magic->data(), 4);
  const auto version = r.ReadU32(4);
  if (!version.has_value()) {
    if (error) *error = "M2: missing version";
    return std::nullopt;
  }
  h.version = *version;
  if (!MagicEq(h.magic, "MD20") && !MagicEq(h.magic, "MD21")) {
    if (error) *error = "M2: unsupported magic";
    return std::nullopt;
  }

  if (h.version != 264) {
    if (error) *error = "M2: unsupported version " + std::to_string(h.version);
    return std::nullopt;
  }

  std::size_t off = 8;
  const auto name_arr = ReadM2Array(r, &off);
  if (!name_arr.has_value()) {
    if (error) *error = "M2: missing name array";
    return std::nullopt;
  }
  h.name = *name_arr;

  const auto global_flags = r.ReadU32(off);
  if (!global_flags.has_value()) {
    if (error) *error = "M2: missing global flags";
    return std::nullopt;
  }
  h.global_flags = *global_flags;
  off += 4;

  const auto global_sequences = ReadM2Array(r, &off);
  const auto animations = ReadM2Array(r, &off);
  const auto animation_lookup = ReadM2Array(r, &off);
  const auto bones = ReadM2Array(r, &off);
  const auto key_bone_lookup = ReadM2Array(r, &off);
  const auto vertices = ReadM2Array(r, &off);
  if (!global_sequences || !animations || !animation_lookup || !bones || !key_bone_lookup ||
      !vertices) {
    if (error) *error = "M2: truncated header arrays";
    return std::nullopt;
  }
  h.global_sequences = *global_sequences;
  h.animations = *animations;
  h.animation_lookup = *animation_lookup;
  h.bones = *bones;
  h.key_bone_lookup = *key_bone_lookup;
  h.vertices = *vertices;

  const auto num_views = r.ReadU32(off);
  if (!num_views.has_value()) {
    if (error) *error = "M2: missing num_views";
    return std::nullopt;
  }
  h.num_views = *num_views;
  off += 4;

  const auto colors = ReadM2Array(r, &off);
  const auto textures = ReadM2Array(r, &off);
  const auto transparencies = ReadM2Array(r, &off);
  const auto uv_animations = ReadM2Array(r, &off);
  const auto tex_replace = ReadM2Array(r, &off);
  const auto render_flags = ReadM2Array(r, &off);
  const auto bone_lookup = ReadM2Array(r, &off);
  const auto tex_lookup = ReadM2Array(r, &off);
  const auto tex_units = ReadM2Array(r, &off);
  const auto transparency_lookup = ReadM2Array(r, &off);
  const auto uv_anim_lookup = ReadM2Array(r, &off);
  if (!colors || !textures || !transparencies || !uv_animations || !tex_replace || !render_flags ||
      !bone_lookup || !tex_lookup || !tex_units || !transparency_lookup || !uv_anim_lookup) {
    if (error) *error = "M2: truncated header arrays (tail)";
    return std::nullopt;
  }
  h.colors = *colors;
  h.textures = *textures;
  h.transparencies = *transparencies;
  h.uv_animations = *uv_animations;
  h.tex_replace = *tex_replace;
  h.render_flags = *render_flags;
  h.bone_lookup = *bone_lookup;
  h.tex_lookup = *tex_lookup;
  h.tex_units = *tex_units;
  h.transparency_lookup = *transparency_lookup;
  h.uv_anim_lookup = *uv_anim_lookup;

  if (!r.CanRead(off, 56)) {
    if (error) *error = "M2: truncated bounding/collision box data";
    return std::nullopt;
  }
  for (int i = 0; i < 3; ++i) {
    const auto v = r.ReadF32(off + static_cast<std::size_t>(i) * 4);
    if (v.has_value())
      h.bounding_box_min[i] = *v;
  }
  for (int i = 0; i < 3; ++i) {
    const auto v = r.ReadF32(off + 12 + static_cast<std::size_t>(i) * 4);
    if (v.has_value())
      h.bounding_box_max[i] = *v;
  }
  if (const auto v = r.ReadF32(off + 24); v.has_value())
    h.bounding_sphere_radius = *v;
  for (int i = 0; i < 3; ++i) {
    const auto v = r.ReadF32(off + 28 + static_cast<std::size_t>(i) * 4);
    if (v.has_value())
      h.collision_box_min[i] = *v;
  }
  for (int i = 0; i < 3; ++i) {
    const auto v = r.ReadF32(off + 40 + static_cast<std::size_t>(i) * 4);
    if (v.has_value())
      h.collision_box_max[i] = *v;
  }
  if (const auto v = r.ReadF32(off + 52); v.has_value())
    h.collision_sphere_radius = *v;
  off += 56;

  return M2HeaderFixedPrefixParse{.header = h, .offset_after = off};
}

static_assert(
    kM2HeaderBoundsPrefixBytes ==
        8u
            + sizeof(M2Array)
            + 4u
            + 6u * sizeof(M2Array)
            + 4u
            + 11u * sizeof(M2Array)
            + 56u,

    "kM2HeaderBoundsPrefixBytes must match the WotLK v264 M2Header "
    "fixed-prefix layout ReadM2HeaderFixedPrefix reads.");

}

std::vector<std::uint16_t> BuildM2FirstSequenceIndexByAnimationId(
    const std::vector<M2AnimationSequenceRecord> &animation_sequences) {
  std::vector<std::uint16_t> first_index_by_id;
  if (animation_sequences.empty()) {
    return first_index_by_id;
  }
  std::uint16_t highest_animation_id = 0u;
  for (const auto &sequence : animation_sequences) {
    highest_animation_id = std::max(highest_animation_id, sequence.animation_id);
  }
  first_index_by_id.assign(static_cast<std::size_t>(highest_animation_id) + 1u,
                           kM2NoSequenceForAnimationId);

  for (std::size_t index = 0; index < animation_sequences.size(); ++index) {
    auto &slot = first_index_by_id[animation_sequences[index].animation_id];
    if (slot == kM2NoSequenceForAnimationId) {
      slot = static_cast<std::uint16_t>(index);
    }
  }
  return first_index_by_id;
}

bool M2SequenceUsesExternalData(const M2AnimationSequenceRecord &sequence) noexcept {
  return (sequence.flags & kM2SequenceFlagDataResident) == 0u;
}

std::optional<std::size_t> ResolveM2SequenceDataOwner(
    const std::vector<M2AnimationSequenceRecord> &animation_sequences,
    const std::size_t sequence_index) {
  if (sequence_index >= animation_sequences.size()) {
    return std::nullopt;
  }

  std::vector<bool> visited(animation_sequences.size(), false);
  std::size_t current_index = sequence_index;
  while (current_index < animation_sequences.size()) {
    const auto &sequence = animation_sequences[current_index];
    if ((sequence.flags & kM2SequenceFlagAlias) == 0u) {
      return current_index;
    }
    if (visited[current_index] || sequence.alias_next >= animation_sequences.size()) {
      return std::nullopt;
    }
    visited[current_index] = true;
    current_index = static_cast<std::size_t>(sequence.alias_next);
  }
  return std::nullopt;
}

std::optional<std::string> BuildM2SequenceDataPath(
    const std::string_view model_path,
    const std::vector<M2AnimationSequenceRecord> &animation_sequences,
    const std::size_t sequence_index) {
  if (model_path.empty()) {
    return std::nullopt;
  }
  const auto owner_index = ResolveM2SequenceDataOwner(animation_sequences, sequence_index);
  if (!owner_index.has_value()) {
    return std::nullopt;
  }
  const auto &owner = animation_sequences[*owner_index];
  return BuildSequenceDataPathForOwner(model_path, owner.animation_id, owner.sub_animation_id);
}

M2LoadResult LoadM2FromBytes(const std::vector<std::uint8_t> &bytes,
                             const std::string &virtual_path,
                             const M2ExternalFileLoader &external_file_loader) {
  M2LoadResult out;
  BinaryReader r(bytes.data(), bytes.size());
  if (bytes.size() < 64) {
    out.error = "M2: file too small";
    return out;
  }

  std::string prefix_error;
  auto prefix = ReadM2HeaderFixedPrefix(r, &prefix_error);
  if (!prefix.has_value()) {
    out.error = std::move(prefix_error);
    return out;
  }
  M2Header h = prefix->header;
  std::size_t off = prefix->offset_after;

  const auto bounding_triangles = ReadM2Array(r, &off);
  const auto bounding_vertices = ReadM2Array(r, &off);
  const auto bounding_normals = ReadM2Array(r, &off);
  const auto attachments = ReadM2Array(r, &off);
  const auto attachment_lookup = ReadM2Array(r, &off);
  const auto events = ReadM2Array(r, &off);
  const auto lights = ReadM2Array(r, &off);
  const auto cameras = ReadM2Array(r, &off);
  const auto camera_lookup = ReadM2Array(r, &off);
  const auto ribbon_emitters = ReadM2Array(r, &off);
  const auto particle_emitters = ReadM2Array(r, &off);
  if (!bounding_triangles || !bounding_vertices || !bounding_normals || !attachments ||
      !attachment_lookup || !events || !lights || !cameras || !camera_lookup || !ribbon_emitters ||
      !particle_emitters) {
    out.error = "M2: truncated header arrays (extras)";
    return out;
  }
  h.bounding_triangles = *bounding_triangles;
  h.bounding_vertices = *bounding_vertices;
  h.bounding_normals = *bounding_normals;
  h.attachments = *attachments;
  h.attachment_lookup = *attachment_lookup;
  h.events = *events;
  h.lights = *lights;
  h.cameras = *cameras;
  h.camera_lookup = *camera_lookup;
  h.ribbon_emitters = *ribbon_emitters;
  h.particle_emitters = *particle_emitters;

  if ((h.global_flags & 0x08u) != 0u) {
    const auto shader_combos = ReadM2Array(r, &off);
    if (!shader_combos) {
      out.error = "M2: truncated header arrays (shader_combos)";
      return out;
    }
    h.shader_combos = *shader_combos;
  } else {
    h.shader_combos = M2Array{0, 0};
  }

  std::string name;
  if (h.name.count > 0) {
    if (!r.CanRead(h.name.offset, h.name.count)) {
      out.error = "M2: name out of bounds";
      return out;
    }
    const auto name_bytes = r.ReadString(h.name.offset, h.name.count);
    if (!name_bytes.has_value()) {
      out.error = "M2: failed to read name";
      return out;
    }
    name = *name_bytes;

    while (!name.empty() && name.back() == '\0') {
      name.pop_back();
    }
  }

  if (h.vertices.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.vertices.count);
    const std::size_t ofs = static_cast<std::size_t>(h.vertices.offset);
    const std::size_t bytes_needed = count * sizeof(M2Vertex);
    if (!r.CanRead(ofs, bytes_needed)) {
      out.error = "M2: vertices out of bounds";
      return out;
    }
    const auto span = r.ReadSpan<M2Vertex>(ofs, count);
    if (!span.has_value()) {
      out.error = "M2: failed to map vertices";
      return out;
    }
    out.model.vertices.assign(span->begin(), span->end());
  }

  if (h.textures.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.textures.count);
    const std::size_t ofs = static_cast<std::size_t>(h.textures.offset);
    const std::size_t bytes_needed = count * 16u;
    if (!r.CanRead(ofs, bytes_needed)) {
      out.error = "M2: textures out of bounds";
      return out;
    }
    out.model.textures.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t base = ofs + i * 16u;
      const auto type = r.ReadU32(base + 0);
      const auto flags = r.ReadU32(base + 4);
      const auto name_len = r.ReadU32(base + 8);
      const auto name_ofs = r.ReadU32(base + 12);
      if (!type || !flags || !name_len || !name_ofs) {
        out.error = "M2: truncated texture entry";
        return out;
      }
      M2Texture t;
      t.type = *type;
      t.flags = *flags;
      t.name = M2Array{.count = *name_len, .offset = *name_ofs};
      if (t.name.count > 0) {
        if (!r.CanRead(t.name.offset, t.name.count)) {
          out.error = "M2: texture name out of bounds";
          return out;
        }
        const auto text = r.ReadString(t.name.offset, t.name.count);
        if (!text.has_value()) {
          out.error = "M2: texture name read failed";
          return out;
        }
        t.name_text = *text;
        while (!t.name_text.empty() && t.name_text.back() == '\0') {
          t.name_text.pop_back();
        }
      }
      out.model.textures.push_back(std::move(t));
    }
  }

  if (h.global_sequences.count > 0) {
    const auto seq =
        r.ReadVector<std::uint32_t>(static_cast<std::size_t>(h.global_sequences.offset),
                                    static_cast<std::size_t>(h.global_sequences.count));
    if (!seq.has_value()) {
      out.error = "M2: global_sequences out of bounds";
      return out;
    }
    out.model.global_sequences_ms = *seq;
  }

  if (h.animations.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.animations.count);
    const std::size_t base = static_cast<std::size_t>(h.animations.offset);
    constexpr std::size_t stride = M2AnimationSequenceRecord::kStride;
    if (!r.CanRead(base, count * stride)) {
      out.error = "M2: animations out of bounds";
      return out;
    }
    out.model.animation_durations_ms.resize(count, 0);
    out.model.animation_sequences.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t entry = base + i * stride;
      auto &sequence = out.model.animation_sequences[i];
      const auto animation_id = r.ReadU16(entry + 0);
      const auto sub_animation_id = r.ReadU16(entry + 2);
      const auto length_ms = r.ReadU32(entry + 4);
      const auto move_speed = r.ReadF32(entry + 8);
      const auto flags = r.ReadU32(entry + 12);
      const auto frequency = r.ReadI16(entry + 16);

      const auto replay_min = r.ReadU32(entry + 20);
      const auto replay_max = r.ReadU32(entry + 24);
      const auto blend_time_ms = r.ReadU32(entry + 28);
      const auto bounding_box_min_x = r.ReadF32(entry + 32);
      const auto bounding_box_min_y = r.ReadF32(entry + 36);
      const auto bounding_box_min_z = r.ReadF32(entry + 40);
      const auto bounding_box_max_x = r.ReadF32(entry + 44);
      const auto bounding_box_max_y = r.ReadF32(entry + 48);
      const auto bounding_box_max_z = r.ReadF32(entry + 52);
      const auto bounding_radius = r.ReadF32(entry + 56);
      const auto next_animation = r.ReadI16(entry + 60);
      const auto alias_next = r.ReadU16(entry + 62);
      if (!animation_id || !sub_animation_id || !length_ms || !move_speed || !flags || !frequency ||
          !replay_min || !replay_max || !blend_time_ms || !bounding_box_min_x ||
          !bounding_box_min_y || !bounding_box_min_z || !bounding_box_max_x ||
          !bounding_box_max_y || !bounding_box_max_z || !bounding_radius) {
        out.error = "M2: truncated animation record";
        return out;
      }

      sequence.animation_id = *animation_id;
      sequence.sub_animation_id = *sub_animation_id;
      sequence.length_ms = *length_ms;
      sequence.move_speed = *move_speed;
      sequence.flags = *flags;
      sequence.frequency = *frequency;
      sequence.replay_min = *replay_min;
      sequence.replay_max = *replay_max;
      sequence.blend_time_ms = *blend_time_ms;
      sequence.bounding_box_min[0] = *bounding_box_min_x;
      sequence.bounding_box_min[1] = *bounding_box_min_y;
      sequence.bounding_box_min[2] = *bounding_box_min_z;
      sequence.bounding_box_max[0] = *bounding_box_max_x;
      sequence.bounding_box_max[1] = *bounding_box_max_y;
      sequence.bounding_box_max[2] = *bounding_box_max_z;
      sequence.bounding_radius = *bounding_radius;

      sequence.next_animation = next_animation.value_or(-1);
      sequence.alias_next = alias_next.value_or(0);
      FixupAnimationSequenceFlags(&sequence);
      out.model.animation_durations_ms[i] = *length_ms;
    }
    out.model.first_sequence_index_by_animation_id =
        BuildM2FirstSequenceIndexByAnimationId(out.model.animation_sequences);
  }

  if (h.animation_lookup.count > 0) {
    const auto lookup =
        r.ReadVector<std::uint16_t>(static_cast<std::size_t>(h.animation_lookup.offset),
                                    static_cast<std::size_t>(h.animation_lookup.count));
    if (!lookup.has_value()) {
      out.error = "M2: animation_lookup out of bounds";
      return out;
    }
    out.model.animation_lookup = *lookup;
  }

  const ExternalSequenceSources external_sequence_sources = BuildExternalSequenceSources(
      out.model.animation_sequences, virtual_path, &external_file_loader);

  if (h.render_flags.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.render_flags.count);
    const std::size_t base = static_cast<std::size_t>(h.render_flags.offset);
    if (!r.CanRead(base, count * 4u)) {
      out.error = "M2: render_flags out of bounds";
      return out;
    }
    out.model.render_flags.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto flags = r.ReadU16(base + i * 4u + 0);
      const auto blend = r.ReadU16(base + i * 4u + 2);
      if (!flags.has_value() || !blend.has_value()) {
        out.error = "M2: truncated render_flags entry";
        return out;
      }
      out.model.render_flags.push_back(M2RenderFlags{.flags = *flags, .blend_mode = *blend});
    }
  }

  if (h.bones.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.bones.count);
    const std::size_t base = static_cast<std::size_t>(h.bones.offset);
    constexpr std::size_t stride = 88;
    if (!r.CanRead(base, count * stride)) {
      out.error = "M2: bones out of bounds";
      return out;
    }
    out.model.bones.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t b = base + i * stride;
      const auto key_bone_id = r.ReadI32(b + 0);
      const auto flags = r.ReadU32(b + 4);
      const auto parent = r.ReadI16(b + 8);
      const auto submesh_id = r.ReadU16(b + 10);
      if (!key_bone_id || !flags || !parent || !submesh_id) {
        out.error = "M2: truncated bone header";
        return out;
      }

      auto t_header = ReadTrackHeader(r, b + 16);
      auto r_header = ReadTrackHeader(r, b + 36);
      auto s_header = ReadTrackHeader(r, b + 56);
      if (!t_header.has_value() || !r_header.has_value() || !s_header.has_value()) {
        out.error = "M2: truncated bone tracks";
        return out;
      }

      M2Bone bone;
      bone.key_bone_id = *key_bone_id;
      bone.flags = *flags;
      bone.parent = *parent;
      bone.submesh_id = *submesh_id;
      std::string track_err;
      if (!ReadTrackData(r, *t_header, &external_sequence_sources, &bone.translation, &track_err) ||
          !ReadTrackData(r, *r_header, &external_sequence_sources, &bone.rotation, &track_err) ||
          !ReadTrackData(r, *s_header, &external_sequence_sources, &bone.scaling, &track_err)) {
        out.error = track_err.empty() ? "M2: bone track parse failed" : track_err;
        return out;
      }

      const auto px = r.ReadF32(b + 76);
      const auto py = r.ReadF32(b + 80);
      const auto pz = r.ReadF32(b + 84);
      if (!px || !py || !pz) {
        out.error = "M2: truncated bone pivot";
        return out;
      }
      bone.pivot[0] = *px;
      bone.pivot[1] = *py;
      bone.pivot[2] = *pz;
      out.model.bones.push_back(std::move(bone));
    }
  }

  if (h.colors.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.colors.count);
    const std::size_t base_off = static_cast<std::size_t>(h.colors.offset);

    constexpr std::size_t kTrackHeaderSize =
        20;
    constexpr std::size_t stride = kTrackHeaderSize * 2;
    if (!r.CanRead(base_off, count * stride)) {
      out.error = "M2: colors out of bounds";
      return out;
    }
    out.model.colors.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t entry = base_off + i * stride;
      const auto color_header = ReadTrackHeader(r, entry);
      const auto alpha_header = ReadTrackHeader(r, entry + kTrackHeaderSize);
      if (!color_header.has_value() || !alpha_header.has_value()) {
        out.error = "M2: truncated color track";
        return out;
      }
      M2ColorAnimation ca;
      std::string track_err;
      if (!ReadTrackData(r, *color_header, &external_sequence_sources, &ca.color, &track_err)) {
        out.error = track_err.empty() ? "M2: color track parse failed" : track_err;
        return out;
      }
      if (!ReadTrackData(r, *alpha_header, &external_sequence_sources, &ca.alpha, &track_err)) {
        out.error = track_err.empty() ? "M2: color alpha track parse failed" : track_err;
        return out;
      }
      out.model.colors.push_back(std::move(ca));
    }
  }

  if (h.transparencies.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.transparencies.count);
    const std::size_t base = static_cast<std::size_t>(h.transparencies.offset);
    constexpr std::size_t stride = 20;
    if (!r.CanRead(base, count * stride)) {
      out.error = "M2: transparencies out of bounds";
      return out;
    }
    out.model.transparencies.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto header = ReadTrackHeader(r, base + i * stride);
      if (!header.has_value()) {
        out.error = "M2: truncated transparency track";
        return out;
      }
      M2Transparency t;
      std::string track_err;
      if (!ReadTrackData(r, *header, &external_sequence_sources, &t.alpha, &track_err)) {
        out.error = track_err.empty() ? "M2: transparency track parse failed" : track_err;
        return out;
      }
      out.model.transparencies.push_back(std::move(t));
    }
  }

  if (h.uv_animations.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.uv_animations.count);
    const std::size_t base = static_cast<std::size_t>(h.uv_animations.offset);
    constexpr std::size_t stride = 60;
    if (!r.CanRead(base, count * stride)) {
      out.error = "M2: uv_animations out of bounds";
      return out;
    }
    out.model.uv_animations.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t t = base + i * stride;
      const auto trans_h = ReadTrackHeader(r, t + 0);
      const auto rot_h = ReadTrackHeader(r, t + 20);
      const auto scale_h = ReadTrackHeader(r, t + 40);
      if (!trans_h || !rot_h || !scale_h) {
        out.error = "M2: truncated uv animation tracks";
        return out;
      }
      M2UvAnimation uv;
      std::string track_err;
      if (!ReadTrackData(r, *trans_h, &external_sequence_sources, &uv.translation, &track_err) ||
          !ReadTrackData(r, *rot_h, &external_sequence_sources, &uv.rotation, &track_err) ||
          !ReadTrackData(r, *scale_h, &external_sequence_sources, &uv.scaling, &track_err)) {
        out.error = track_err.empty() ? "M2: uv animation track parse failed" : track_err;
        return out;
      }
      out.model.uv_animations.push_back(std::move(uv));
    }
  }

  if (h.cameras.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.cameras.count);
    const std::size_t base = static_cast<std::size_t>(h.cameras.offset);
    constexpr std::size_t stride = 100;
    if (!r.CanRead(base, count * stride)) {
      out.error = "M2: cameras out of bounds";
      return out;
    }
    out.model.cameras.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t c = base + i * stride;
      const auto type = r.ReadU32(c + 0);
      const auto fov = r.ReadF32(c + 4);
      const auto far_clip = r.ReadF32(c + 8);
      const auto near_clip = r.ReadF32(c + 12);
      if (!type || !fov || !far_clip || !near_clip) {
        out.error = "M2: truncated camera header";
        return out;
      }

      const auto pos_h = ReadTrackHeader(r, c + 16);
      const auto tar_h = ReadTrackHeader(r, c + 48);
      const auto roll_h = ReadTrackHeader(r, c + 80);
      if (!pos_h || !tar_h || !roll_h) {
        out.error = "M2: truncated camera tracks";
        return out;
      }

      M2Camera cam;
      cam.type = *type;
      cam.fov = *fov;
      cam.far_clip = *far_clip;
      cam.near_clip = *near_clip;
      cam.position_base.x = r.ReadF32(c + 36).value_or(0.0F);
      cam.position_base.y = r.ReadF32(c + 40).value_or(0.0F);
      cam.position_base.z = r.ReadF32(c + 44).value_or(0.0F);
      cam.target_base.x = r.ReadF32(c + 68).value_or(0.0F);
      cam.target_base.y = r.ReadF32(c + 72).value_or(0.0F);
      cam.target_base.z = r.ReadF32(c + 76).value_or(0.0F);

      std::string track_err;
      constexpr std::size_t kCameraSplineValuesPerKey = 3;
      if (!ReadTrackData(r, *pos_h, &external_sequence_sources, &cam.position, &track_err,
                         kCameraSplineValuesPerKey) ||
          !ReadTrackData(r, *tar_h, &external_sequence_sources, &cam.target, &track_err,
                         kCameraSplineValuesPerKey) ||
          !ReadTrackData(r, *roll_h, &external_sequence_sources, &cam.roll, &track_err,
                         kCameraSplineValuesPerKey)) {
        out.error = track_err.empty() ? "M2: camera track parse failed" : track_err;
        return out;
      }
      out.model.cameras.push_back(std::move(cam));
    }
  }

  if (h.lights.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.lights.count);
    const std::size_t base = static_cast<std::size_t>(h.lights.offset);
    constexpr std::size_t kTrackHeaderSize = 20;
    constexpr std::size_t kLightStride = 16 + 7 * kTrackHeaderSize;
    if (r.CanRead(base, count * kLightStride)) {
      out.model.lights.reserve(count);
      for (std::size_t i = 0; i < count; ++i) {
        const std::size_t lo = base + i * kLightStride;
        M2Light light;
        light.type = r.ReadU16(lo + 0).value_or(0);
        light.bone = r.ReadI16(lo + 2).value_or(-1);
        light.position[0] = r.ReadF32(lo + 4).value_or(0.0f);
        light.position[1] = r.ReadF32(lo + 8).value_or(0.0f);
        light.position[2] = r.ReadF32(lo + 12).value_or(0.0f);

        const auto amb_color_h = ReadTrackHeader(r, lo + 16);
        const auto amb_intens_h = ReadTrackHeader(r, lo + 36);
        const auto dif_color_h = ReadTrackHeader(r, lo + 56);
        const auto dif_intens_h = ReadTrackHeader(r, lo + 76);
        const auto att_start_h = ReadTrackHeader(r, lo + 96);
        const auto att_end_h = ReadTrackHeader(r, lo + 116);
        const auto visibility_h = ReadTrackHeader(r, lo + 136);
        if (!amb_color_h || !amb_intens_h || !dif_color_h || !dif_intens_h || !att_start_h ||
            !att_end_h || !visibility_h) {
          out.error = "M2: truncated light tracks";
          return out;
        }

        std::string track_err;
        if (!ReadTrackData(r, *amb_color_h, &external_sequence_sources, &light.ambient_color,
                           &track_err) ||
            !ReadTrackData(r, *amb_intens_h, &external_sequence_sources, &light.ambient_intensity,
                           &track_err) ||
            !ReadTrackData(r, *dif_color_h, &external_sequence_sources, &light.diffuse_color,
                           &track_err) ||
            !ReadTrackData(r, *dif_intens_h, &external_sequence_sources, &light.diffuse_intensity,
                           &track_err) ||
            !ReadTrackData(r, *att_start_h, &external_sequence_sources, &light.attenuation_start,
                           &track_err) ||
            !ReadTrackData(r, *att_end_h, &external_sequence_sources, &light.attenuation_end,
                           &track_err) ||
            !ReadTrackData(r, *visibility_h, &external_sequence_sources, &light.visibility,
                           &track_err)) {
          out.error = track_err.empty() ? "M2: light track parse failed" : track_err;
          return out;
        }
        out.model.lights.push_back(std::move(light));
      }
    }
  }

  out.model.header = h;
  out.model.name = std::move(name);

  if (h.attachments.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.attachments.count);
    const std::size_t base_off = static_cast<std::size_t>(h.attachments.offset);
    constexpr std::size_t att_stride = 40;
    if (!r.CanRead(base_off, count * att_stride)) {
      out.error = "M2: attachments out of bounds";
      return out;
    }
    out.model.attachments.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t o = base_off + i * att_stride;
      M2Attachment att;
      att.id = r.ReadU32(o + 0).value_or(0);
      att.bone = r.ReadU16(o + 4).value_or(0);
      att.unknown = r.ReadU16(o + 6).value_or(0);
      att.position[0] = r.ReadF32(o + 8).value_or(0.0f);
      att.position[1] = r.ReadF32(o + 12).value_or(0.0f);
      att.position[2] = r.ReadF32(o + 16).value_or(0.0f);
      out.model.attachments.push_back(att);
    }
  }

  if (h.attachment_lookup.count > 0) {
    const auto lkup =
        r.ReadVector<std::int16_t>(static_cast<std::size_t>(h.attachment_lookup.offset),
                                   static_cast<std::size_t>(h.attachment_lookup.count));
    if (!lkup.has_value()) {
      out.error = "M2: attachment lookup out of bounds";
      return out;
    }
    out.model.attachment_lookups = *lkup;
  }

  if (h.events.count > 0) {
    const std::size_t count = static_cast<std::size_t>(h.events.count);
    const std::size_t base_off = static_cast<std::size_t>(h.events.offset);
    constexpr std::size_t kEventStride = 36;
    if (!r.CanRead(base_off, count * kEventStride)) {
      out.error = "M2: events out of bounds";
      return out;
    }
    out.model.events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t eo = base_off + i * kEventStride;
      M2Event event;
      event.identifier = r.ReadU32(eo + 0).value_or(0);
      event.data = r.ReadU32(eo + 4).value_or(0);
      event.bone = r.ReadI16(eo + 8).value_or(-1);
      event.unknown = r.ReadU16(eo + 10).value_or(0);
      event.position[0] = r.ReadF32(eo + 12).value_or(0.0f);
      event.position[1] = r.ReadF32(eo + 16).value_or(0.0f);
      event.position[2] = r.ReadF32(eo + 20).value_or(0.0f);

      const auto track_header = ReadDiscreteTrackHeader(r, eo + 24);
      if (!track_header.has_value()) {
        out.error = "M2: truncated event timing track";
        return out;
      }

      std::string track_error;
      if (!ReadDiscreteTrackTimes(r, *track_header, &external_sequence_sources, &event.timings,
                                  &track_error)) {
        out.error = track_error.empty() ? "M2: event timing parse failed" : track_error;
        return out;
      }

      out.model.events.push_back(std::move(event));
    }
  }

  {
    constexpr std::size_t kRibbonStride = 176;
    const std::size_t count = static_cast<std::size_t>(h.ribbon_emitters.count);
    const std::size_t base_off = static_cast<std::size_t>(h.ribbon_emitters.offset);
    if ((count == 0 && base_off <= bytes.size()) || r.CanRead(base_off, count * kRibbonStride)) {
      if (count > 0) {
        out.model.ribbon_emitters.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
          const std::size_t e = base_off + i * kRibbonStride;
          M2RibbonEmitter emitter;

          const auto ribbon_id = r.ReadU32(e + 0);
          const auto bone_index = r.ReadU32(e + 4);
          const auto pos = r.ReadSpan<float>(e + 8, 3);
          if (!ribbon_id || !bone_index || !pos) {
            out.error = "M2: truncated ribbon emitter header";
            return out;
          }
          emitter.ribbon_id = *ribbon_id;
          emitter.bone_index = *bone_index;
          emitter.position[0] = (*pos)[0];
          emitter.position[1] = (*pos)[1];
          emitter.position[2] = (*pos)[2];

          {
            std::size_t cursor = e + 20;
            const auto arr = ReadM2Array(r, &cursor);
            if (!arr) {
              out.error = "M2: ribbon tex indices";
              return out;
            }
            if (arr->count > 0) {
              const auto v = r.ReadVector<std::uint16_t>(static_cast<std::size_t>(arr->offset),
                                                         static_cast<std::size_t>(arr->count));
              if (!v) {
                out.error = "M2: ribbon tex indices data";
                return out;
              }
              emitter.texture_indices = *v;
            }
          }

          {
            std::size_t cursor = e + 28;
            const auto arr = ReadM2Array(r, &cursor);
            if (!arr) {
              out.error = "M2: ribbon mat indices";
              return out;
            }
            if (arr->count > 0) {
              const auto v = r.ReadVector<std::uint16_t>(static_cast<std::size_t>(arr->offset),
                                                         static_cast<std::size_t>(arr->count));
              if (!v) {
                out.error = "M2: ribbon mat indices data";
                return out;
              }
              emitter.material_indices = *v;
            }
          }

          std::string track_err;
          auto read_track = [&](std::size_t off, auto *dst) -> bool {
            const auto th = ReadTrackHeader(r, off);
            if (!th) {
              track_err = "M2: ribbon track header";
              return false;
            }
            return ReadTrackData(r, *th, &external_sequence_sources, dst, &track_err);
          };
          if (!read_track(e + 36, &emitter.color)
              || !read_track(e + 56, &emitter.alpha)
              || !read_track(e + 76, &emitter.height_above)
              || !read_track(e + 96, &emitter.height_below)
              || !read_track(e + 132, &emitter.tex_slot)
              || !read_track(e + 152, &emitter.visibility))
          {
            out.error = track_err.empty() ? "M2: ribbon track parse failed" : track_err;
            return out;
          }

          const auto eps = r.ReadF32(e + 116);
          const auto elt = r.ReadF32(e + 120);
          const auto grav = r.ReadF32(e + 124);
          const auto trows = r.ReadU16(e + 128);
          const auto tcols = r.ReadU16(e + 130);
          const auto pplane = r.ReadI16(e + 172);
          if (!eps || !elt || !grav || !trows || !tcols || !pplane) {
            out.error = "M2: ribbon static fields truncated";
            return out;
          }
          emitter.edges_per_second = *eps;
          emitter.edge_lifetime = *elt;
          emitter.gravity = *grav;
          emitter.texture_rows = *trows;
          emitter.texture_cols = *tcols;
          emitter.priority_plane = *pplane;

          out.model.ribbon_emitters.push_back(std::move(emitter));
        }
      }
    } else {
      out.error = "M2: ribbon emitters out of bounds";
      return out;
    }
  }

  if (h.particle_emitters.count > 0) {
    constexpr std::size_t kParticleStride = 476;
    const std::size_t pe_count = static_cast<std::size_t>(h.particle_emitters.count);
    const std::size_t pe_base = static_cast<std::size_t>(h.particle_emitters.offset);
    if (r.CanRead(pe_base, pe_count * kParticleStride)) {
      out.model.particle_emitters.reserve(pe_count);
      for (std::size_t i = 0; i < pe_count; ++i) {
        const std::size_t eo = pe_base + i * kParticleStride;
        M2ParticleEmitter em;
        em.id = static_cast<std::int32_t>(r.ReadU32(eo + 0x00).value_or(0xFFFFFFFF));
        em.flags = r.ReadU32(eo + 0x04).value_or(0);
        em.position[0] = r.ReadF32(eo + 0x08).value_or(0.0f);
        em.position[1] = r.ReadF32(eo + 0x0C).value_or(0.0f);
        em.position[2] = r.ReadF32(eo + 0x10).value_or(0.0f);
        em.bone = r.ReadU16(eo + 0x14).value_or(0);
        em.texture = r.ReadU16(eo + 0x16).value_or(0);

        const auto read_model_name = [&](std::size_t array_off) -> std::string {
          const auto count = r.ReadU32(eo + array_off).value_or(0);
          const auto offset = r.ReadU32(eo + array_off + 4).value_or(0);
          if (count == 0) {
            return {};
          }
          if (!r.CanRead(offset, count)) {
            return {};
          }
          auto text = r.ReadString(offset, count);
          if (!text.has_value()) {
            return {};
          }
          while (!text->empty() && text->back() == '\0') {
            text->pop_back();
          }
          return *text;
        };
        em.geometry_model_name = read_model_name(0x18);
        em.recursion_model_name = read_model_name(0x20);

        em.blending_type = r.ReadU8(eo + 0x28).value_or(0);
        em.emitter_type = r.ReadU8(eo + 0x29).value_or(0);
        em.particle_color_index = r.ReadU16(eo + 0x2A).value_or(0);
        em.texture_tile_rotation = r.ReadU16(eo + 0x2E).value_or(0);
        em.texture_tile_rows = r.ReadU16(eo + 0x30).value_or(1);
        em.texture_tile_cols = r.ReadU16(eo + 0x32).value_or(1);

        auto read_track = [&](std::size_t track_off, M2Track<float> &track) {
          const auto th = ReadTrackHeader(r, eo + track_off);
          if (th.has_value()) {
            ReadTrackData(r, *th, &external_sequence_sources, &track, &out.error);
          }
        };
        read_track(0x34, em.emission_speed);
        read_track(0x48, em.speed_variation);
        read_track(0x5C, em.vertical_range);
        read_track(0x70, em.horizontal_range);
        read_track(0x84, em.gravity);
        read_track(0x98, em.lifespan);
        em.lifespan_vary = r.ReadF32(eo + 0xAC).value_or(0.0f);
        read_track(0xB0, em.emission_rate);
        em.emission_rate_vary = r.ReadF32(eo + 0xC4).value_or(0.0f);
        read_track(0xC8, em.emission_area_length);
        read_track(0xDC, em.emission_area_width);
        read_track(0xF0, em.z_source);

        auto read_lifetime_vec3 = [&](std::size_t track_off,
                                      M2ParticleLifetimeTrack<M2Vec3> &track) -> bool {
          const auto header = ReadM2ParticleLifetimeTrackHeader(r, eo + track_off);
          if (!header.has_value()) {
            out.error = "M2: particle lifetime vec3 header out of bounds";
            return false;
          }
          return ReadM2ParticleLifetimeTrackData(r, *header, &track, &out.error);
        };
        auto read_lifetime_vec2 = [&](std::size_t track_off,
                                      M2ParticleLifetimeTrack<M2Vec2> &track) -> bool {
          const auto header = ReadM2ParticleLifetimeTrackHeader(r, eo + track_off);
          if (!header.has_value()) {
            out.error = "M2: particle lifetime vec2 header out of bounds";
            return false;
          }
          return ReadM2ParticleLifetimeTrackData(r, *header, &track, &out.error);
        };
        auto read_lifetime_u16 = [&](std::size_t track_off,
                                     M2ParticleLifetimeTrack<std::uint16_t> &track) -> bool {
          const auto header = ReadM2ParticleLifetimeTrackHeader(r, eo + track_off);
          if (!header.has_value()) {
            out.error = "M2: particle lifetime u16 header out of bounds";
            return false;
          }
          return ReadM2ParticleLifetimeTrackData(r, *header, &track, &out.error);
        };
        if (!read_lifetime_vec3(0x104, em.color_track) ||
            !read_lifetime_u16(0x114, em.alpha_track) ||
            !read_lifetime_vec2(0x124, em.scale_track)) {
          return out;
        }
        em.scale_vary.x = r.ReadF32(eo + 0x134).value_or(0.0f);
        em.scale_vary.y = r.ReadF32(eo + 0x138).value_or(0.0f);
        if (!read_lifetime_u16(0x13C, em.head_cell_track) ||
            !read_lifetime_u16(0x14C, em.tail_cell_track)) {
          return out;
        }

        em.tail_length = r.ReadF32(eo + 0x15C).value_or(0.0f);
        em.twinkle_speed = r.ReadF32(eo + 0x160).value_or(0.0f);
        em.twinkle_percent = r.ReadF32(eo + 0x164).value_or(0.0f);
        em.twinkle_scale_min = r.ReadF32(eo + 0x168).value_or(0.0f);
        em.twinkle_scale_max = r.ReadF32(eo + 0x16C).value_or(0.0f);
        em.position_delta_multiplier = r.ReadF32(eo + 0x170).value_or(0.0f);
        em.drag = r.ReadF32(eo + 0x174).value_or(0.0f);
        em.base_spin = r.ReadF32(eo + 0x178).value_or(0.0f);
        em.base_spin_vary = r.ReadF32(eo + 0x17C).value_or(0.0f);
        em.spin = r.ReadF32(eo + 0x180).value_or(0.0f);
        em.spin_vary = r.ReadF32(eo + 0x184).value_or(0.0f);
        em.tumble_min[0] = r.ReadF32(eo + 0x188).value_or(0.0f);
        em.tumble_min[1] = r.ReadF32(eo + 0x18C).value_or(0.0f);
        em.tumble_min[2] = r.ReadF32(eo + 0x190).value_or(0.0f);
        em.tumble_max[0] = r.ReadF32(eo + 0x194).value_or(0.0f);
        em.tumble_max[1] = r.ReadF32(eo + 0x198).value_or(0.0f);
        em.tumble_max[2] = r.ReadF32(eo + 0x19C).value_or(0.0f);
        em.wind_vector[0] = r.ReadF32(eo + 0x1A0).value_or(0.0f);
        em.wind_vector[1] = r.ReadF32(eo + 0x1A4).value_or(0.0f);
        em.wind_vector[2] = r.ReadF32(eo + 0x1A8).value_or(0.0f);
        em.wind_time = r.ReadF32(eo + 0x1AC).value_or(0.0f);
        em.follow_speed1 = r.ReadF32(eo + 0x1B0).value_or(0.0f);
        em.follow_scale1 = r.ReadF32(eo + 0x1B4).value_or(0.0f);
        em.follow_speed2 = r.ReadF32(eo + 0x1B8).value_or(0.0f);
        em.follow_scale2 = r.ReadF32(eo + 0x1BC).value_or(0.0f);
        constexpr std::size_t kParticleSplinePointCountOffset = 0x1C0;
        constexpr std::size_t kParticleSplinePointDataOffset = 0x1C4;
        const std::uint32_t spline_point_count =
            r.ReadU32(eo + kParticleSplinePointCountOffset).value_or(0);
        const std::uint32_t spline_point_offset =
            r.ReadU32(eo + kParticleSplinePointDataOffset).value_or(0);
        if (spline_point_count > 0u) {
          const auto points = r.ReadVector<M2Vec3>(static_cast<std::size_t>(spline_point_offset),
                                                   static_cast<std::size_t>(spline_point_count));
          if (!points.has_value()) {
            out.error = "M2: particle spline points out of bounds";
            return out;
          }
          em.spline_points = *points;
        }

        const auto enabled_track = ReadTrackHeader(r, eo + 0x1C8);
        if (enabled_track.has_value()) {
          ReadTrackData(r, *enabled_track, &external_sequence_sources, &em.enabled_in, &out.error);
        }

        out.model.particle_emitters.push_back(std::move(em));
      }
    }
  }

  const auto read_i16_lookup = [&](const M2Array &chunk, const char *error,
                                   std::vector<std::int16_t> *output) {
    if (chunk.count == 0) {
      return true;
    }

    const auto lookup = r.ReadVector<std::int16_t>(static_cast<std::size_t>(chunk.offset),
                                                   static_cast<std::size_t>(chunk.count));
    if (!lookup.has_value()) {
      out.error = error;
      return false;
    }

    *output = *lookup;
    return true;
  };

  const auto read_u16_lookup = [&](const M2Array &chunk, const char *error,
                                   std::vector<std::uint16_t> *output) {
    if (chunk.count == 0) {
      return true;
    }

    const auto lookup = r.ReadVector<std::uint16_t>(static_cast<std::size_t>(chunk.offset),
                                                    static_cast<std::size_t>(chunk.count));
    if (!lookup.has_value()) {
      out.error = error;
      return false;
    }

    *output = *lookup;
    return true;
  };

  if (!read_i16_lookup(h.key_bone_lookup, "M2: key_bone_lookup out of bounds",
                       &out.model.key_bone_lookup) ||
      !read_u16_lookup(h.bone_lookup, "M2: bone_lookup out of bounds",
                       &out.model.bone_lookup) ||
      !read_u16_lookup(h.tex_lookup, "M2: tex_lookup out of bounds", &out.model.tex_lookup) ||
      !read_u16_lookup(h.tex_units, "M2: tex_units out of bounds", &out.model.tex_units) ||
      !read_u16_lookup(h.transparency_lookup, "M2: transparency_lookup out of bounds",
                       &out.model.transparency_lookup) ||
      !read_u16_lookup(h.uv_anim_lookup, "M2: uv_anim_lookup out of bounds",
                       &out.model.uv_anim_lookup) ||
      !read_u16_lookup(h.camera_lookup, "M2: camera_lookup out of bounds",
                       &out.model.camera_lookups)) {
    return out;
  }
  if ((h.global_flags & kM2GlobalFlagHasTextureCombinerCombos) != 0u &&
      !read_u16_lookup(h.shader_combos, "M2: shader_combos out of bounds",
                       &out.model.shader_combos)) {
    return out;
  }

  if (h.bounding_triangles.count > 0) {
    const auto triangles =
        r.ReadVector<std::uint16_t>(static_cast<std::size_t>(h.bounding_triangles.offset),
                                    static_cast<std::size_t>(h.bounding_triangles.count));
    if (!triangles.has_value()) {
      out.error = "M2: bounding triangles out of bounds";
      return out;
    }
    out.model.bounding_triangles = *triangles;
  }

  const auto read_vec3_chunk = [&](const M2Array &chunk, const char *error,
                                   std::vector<M2Vec3> *output) {
    if (chunk.count == 0) {
      return true;
    }

    const auto values = r.ReadVector<M2Vec3>(static_cast<std::size_t>(chunk.offset),
                                             static_cast<std::size_t>(chunk.count));
    if (!values.has_value()) {
      out.error = error;
      return false;
    }

    *output = *values;
    return true;
  };
  if (!read_vec3_chunk(h.bounding_vertices, "M2: bounding vertices out of bounds",
                       &out.model.bounding_vertices) ||
      !read_vec3_chunk(h.bounding_normals, "M2: bounding normals out of bounds",
                       &out.model.bounding_normals)) {
    return out;
  }

  RebuildM2BonePoseIndex(out.model);

  out.ok = true;
  return out;
}

M2LoadResult LoadM2FromBytes(const std::vector<std::uint8_t> &bytes) {
  return LoadM2FromBytes(bytes, std::string{}, M2ExternalFileLoader{});
}

void RebuildM2BonePoseIndex(M2Model &model) {
  M2BonePoseIndex &index = model.bone_pose_index;
  index = M2BonePoseIndex{};
  if (model.bones.empty()) {
    return;
  }

  std::size_t animation_count = 1u;
  for (const M2Bone &bone : model.bones) {
    animation_count = std::max(animation_count, bone.translation.SetCount());
    animation_count = std::max(animation_count, bone.rotation.SetCount());
    animation_count = std::max(animation_count, bone.scaling.SetCount());
  }
  if (animation_count > std::numeric_limits<std::uint32_t>::max()) {
    return;
  }

  const std::size_t words_per_row =
      (model.bones.size() + M2BonePoseIndex::kBonesPerWord - 1u) /
      M2BonePoseIndex::kBonesPerWord;
  index.animation_count = static_cast<std::uint32_t>(animation_count);
  index.words_per_row = static_cast<std::uint32_t>(words_per_row);
  index.keyless_words.assign(
      animation_count * M2BonePoseIndex::kRowsPerAnimation * words_per_row, 0u);
  index.finite_pivot_words.assign(words_per_row, 0u);

  const auto track_is_keyless = [](const auto &track,
                                   const std::size_t animation_index) noexcept {
    if (track.SetCount() == 0u) {
      return true;
    }
    std::size_t set_index = track.global_sequence < 0 ? animation_index : 0u;
    if (set_index >= track.SetCount()) {
      set_index = 0u;
    }
    const M2TrackSegment &segment = track.segments[set_index];
    return segment.time_count == 0u || segment.value_count == 0u;
  };

  for (std::size_t bone_index = 0; bone_index < model.bones.size(); ++bone_index) {
    const M2Bone &bone = model.bones[bone_index];
    const std::size_t word = bone_index / M2BonePoseIndex::kBonesPerWord;
    const std::uint64_t bit = std::uint64_t{1}
                              << (bone_index % M2BonePoseIndex::kBonesPerWord);

    if (std::isfinite(bone.pivot[0]) && std::isfinite(bone.pivot[1]) &&
        std::isfinite(bone.pivot[2])) {
      index.finite_pivot_words[word] |= bit;
    }

    for (std::size_t animation_index = 0; animation_index < animation_count;
         ++animation_index) {
      const std::size_t group =
          animation_index * M2BonePoseIndex::kRowsPerAnimation * words_per_row;
      if (track_is_keyless(bone.translation, animation_index)) {
        index.keyless_words[group +
                            M2BonePoseIndex::kTranslationRow * words_per_row + word] |= bit;
      }
      if (track_is_keyless(bone.rotation, animation_index)) {
        index.keyless_words[group + M2BonePoseIndex::kRotationRow * words_per_row +
                            word] |= bit;
      }
      if (track_is_keyless(bone.scaling, animation_index)) {
        index.keyless_words[group + M2BonePoseIndex::kScalingRow * words_per_row +
                            word] |= bit;
      }
    }
  }
}

M2HeaderBoundsResult ParseM2HeaderBounds(
    const std::span<const std::uint8_t> prefix_bytes) {
  M2HeaderBoundsResult out;
  if (prefix_bytes.size() < 64) {
    out.error = "M2: file too small";
    return out;
  }
  const BinaryReader r(prefix_bytes.data(), prefix_bytes.size());
  std::string error;
  const auto prefix = ReadM2HeaderFixedPrefix(r, &error);
  if (!prefix.has_value()) {
    out.error = std::move(error);
    return out;
  }
  out.ok = true;
  std::memcpy(out.bounding_box_min, prefix->header.bounding_box_min,
              sizeof(out.bounding_box_min));
  std::memcpy(out.bounding_box_max, prefix->header.bounding_box_max,
              sizeof(out.bounding_box_max));
  out.bounding_sphere_radius = prefix->header.bounding_sphere_radius;
  return out;
}

SkinLoadResult LoadSkinFromBytes(const std::vector<std::uint8_t> &bytes) {
  SkinLoadResult out;
  BinaryReader r(bytes.data(), bytes.size());
  if (bytes.size() < sizeof(SkinHeader)) {
    out.error = "Skin: file too small";
    return out;
  }
  SkinHeader h{};
  const auto magic = r.ReadBytes(0, 4);
  if (!magic.has_value()) {
    out.error = "Skin: missing magic";
    return out;
  }
  std::memcpy(h.magic, magic->data(), 4);
  if (!MagicEq(h.magic, "SKIN")) {
    out.error = "Skin: unsupported magic";
    return out;
  }

  auto read_u32 = [&](std::size_t off) -> std::optional<std::uint32_t> { return r.ReadU32(off); };
  const auto n_indices = read_u32(4);
  const auto ofs_indices = read_u32(8);
  const auto n_triangles = read_u32(12);
  const auto ofs_triangles = read_u32(16);
  const auto n_properties = read_u32(20);
  const auto ofs_properties = read_u32(24);
  const auto n_submeshes = read_u32(28);
  const auto ofs_submeshes = read_u32(32);
  const auto n_texture_units = read_u32(36);
  const auto ofs_texture_units = read_u32(40);
  const auto lod = read_u32(44);
  if (!n_indices.has_value() || !ofs_indices.has_value() || !n_triangles.has_value() ||
      !ofs_triangles.has_value() || !n_properties.has_value() || !ofs_properties.has_value() ||
      !n_submeshes.has_value() || !ofs_submeshes.has_value() || !n_texture_units.has_value() ||
      !ofs_texture_units.has_value() || !lod.has_value()) {
    out.error = "Skin: truncated header";
    return out;
  }
  h.n_indices = *n_indices;
  h.ofs_indices = *ofs_indices;
  h.n_triangles = *n_triangles;
  h.ofs_triangles = *ofs_triangles;
  h.n_properties = *n_properties;
  h.ofs_properties = *ofs_properties;
  h.n_submeshes = *n_submeshes;
  h.ofs_submeshes = *ofs_submeshes;
  h.n_texture_units = *n_texture_units;
  h.ofs_texture_units = *ofs_texture_units;
  h.lod = *lod;

  out.skin.header = h;

  if (!ReadOptionalSkinChunk<std::uint16_t>(r, h.n_indices, h.ofs_indices, &out.skin.indices)) {
    out.error = "Skin: indices out of bounds";
    return out;
  }

  if (!ReadOptionalSkinChunk<std::uint16_t>(r, h.n_triangles, h.ofs_triangles,
                                            &out.skin.triangles)) {
    out.error = "Skin: triangles out of bounds";
    return out;
  }

  if (!ReadOptionalSkinPropertyBytes(r, h.n_properties, h.ofs_properties, &out.skin.properties)) {
    out.error = "Skin: properties out of bounds";
    return out;
  }

  if (!ReadOptionalSkinChunk<SkinSubmesh>(r, h.n_submeshes, h.ofs_submeshes, &out.skin.submeshes)) {
    out.error = "Skin: submeshes out of bounds";
    return out;
  }

  if (!ReadOptionalSkinChunk<SkinTextureUnit>(r, h.n_texture_units, h.ofs_texture_units,
                                              &out.skin.texture_units)) {
    out.error = "Skin: texture units out of bounds";
    return out;
  }

  out.ok = true;
  return out;
}

M2LoadResult LoadM2(const openwow::vfs::VirtualFileSystem &vfs, const std::string &virtual_path) {
  M2LoadResult out;
  const auto bytes = vfs.ReadFileBytes(virtual_path);
  if (!bytes.has_value()) {
    out.error = "M2: VFS read failed: " + virtual_path;
    return out;
  }
  out = LoadM2FromBytes(*bytes, virtual_path,
                        [&vfs](const std::string &path) -> std::vector<std::uint8_t> {
                          const auto sidecar_bytes = vfs.ReadFileBytes(path);
                          return sidecar_bytes.value_or(std::vector<std::uint8_t>{});
                        });
  if (!out.ok && out.error.empty()) {
    out.error = "M2: parse failed: " + virtual_path;
  }
  return out;
}

SkinLoadResult LoadSkin(const openwow::vfs::VirtualFileSystem &vfs,
                        const std::string &virtual_path) {
  SkinLoadResult out;
  const auto bytes = vfs.ReadFileBytes(virtual_path);
  if (!bytes.has_value()) {
    out.error = "Skin: VFS read failed: " + virtual_path;
    return out;
  }
  out = LoadSkinFromBytes(*bytes);
  if (!out.ok && out.error.empty()) {
    out.error = "Skin: parse failed: " + virtual_path;
  }
  return out;
}

}
