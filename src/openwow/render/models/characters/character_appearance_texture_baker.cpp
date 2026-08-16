#include "openwow/render/models/characters/character_appearance_texture_baker.h"

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/image/image_decoder.h"
#include "openwow/game/character_component_backend.h"
#include "openwow/game/tabard_renderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string_view>

namespace openwow::render {
namespace {

constexpr std::uint32_t kBodyTextureType = 1u;
constexpr std::uint32_t kCapeTextureType = 2u;
constexpr std::uint32_t kHairTextureType = 6u;
constexpr std::uint32_t kExtraSkinTextureType = 8u;
constexpr std::uint32_t kRetailAtlasEdge = 256u;

struct AtlasRect {
  std::uint32_t x;
  std::uint32_t y;
  std::uint32_t width;
  std::uint32_t height;
};

constexpr std::array<AtlasRect, 10> kRetailAtlasRegions{{
    {0u, 0u, 128u, 64u},
    {0u, 64u, 128u, 64u},
    {0u, 128u, 128u, 32u},
    {128u, 0u, 128u, 64u},
    {128u, 64u, 128u, 32u},
    {128u, 96u, 128u, 64u},
    {128u, 160u, 128u, 64u},
    {128u, 224u, 128u, 32u},
    {0u, 192u, 128u, 64u},
    {0u, 160u, 128u, 32u},
}};

std::uint64_t Fnv1aAppend(std::uint64_t hash, const std::string_view bytes) {
  constexpr std::uint64_t kPrime = 1099511628211ull;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= kPrime;
  }
  hash ^= 0xFFu;
  return hash * kPrime;
}

std::string NormalizeVirtualPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (!path.empty() && path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  return path;
}

std::string CanonicalCacheIdentityPath(const std::string_view path) {
  std::string canonical(path);
  std::replace(canonical.begin(), canonical.end(), '\\', '/');
  std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                 [](const unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  if (!canonical.empty() && canonical.front() != '/') {
    canonical.insert(canonical.begin(), '/');
  }
  return canonical;
}

class SourceImageCache {
 public:
  explicit SourceImageCache(const CharacterTextureFileLoader &loader) : loader_(loader) {}

  [[nodiscard]] bool Exists(const std::string &path) {
    return !Bytes(path).empty();
  }

  [[nodiscard]] const std::vector<std::uint8_t> &Bytes(const std::string &path) {
    static const std::vector<std::uint8_t> kEmpty;
    if (path.empty() || !loader_) {
      return kEmpty;
    }
    const std::string key = NormalizeVirtualPath(path);
    auto [it, inserted] = bytes_.try_emplace(key);
    if (inserted) {
      it->second = loader_(path);
      if (it->second.empty() && key != path) {
        it->second = loader_(key);
      }
    }
    return it->second;
  }

  [[nodiscard]] const openwow::data::image::DecodedImage *Decode(
      const std::string &path) {
    if (path.empty()) {
      return nullptr;
    }
    const std::string key = NormalizeVirtualPath(path);
    if (const auto found = decoded_.find(key); found != decoded_.end()) {
      return found->second.ok ? &found->second : nullptr;
    }
    const auto &bytes = Bytes(path);
    auto decoded = openwow::data::image::DecodeImage(bytes);
    const auto [it, inserted] = decoded_.emplace(key, std::move(decoded));
    (void)inserted;
    return it->second.ok ? &it->second : nullptr;
  }

 private:
  const CharacterTextureFileLoader &loader_;
  std::unordered_map<std::string, std::vector<std::uint8_t>> bytes_;
  std::unordered_map<std::string, openwow::data::image::DecodedImage> decoded_;
};

AtlasRect ScaleRegion(const AtlasRect region, const std::uint32_t edge) {
  return {
      .x = region.x * edge / kRetailAtlasEdge,
      .y = region.y * edge / kRetailAtlasEdge,
      .width = std::max(1u, region.width * edge / kRetailAtlasEdge),
      .height = std::max(1u, region.height * edge / kRetailAtlasEdge),
  };
}

std::uint8_t BlendRetailChannel(const std::uint8_t destination,
                                const std::uint8_t source,
                                const std::uint8_t alpha) {

  return static_cast<std::uint8_t>(
      (((255u - alpha) * destination) + (static_cast<std::uint32_t>(alpha) * source)) >> 8u);
}

void CompositeImage(std::vector<std::uint8_t> &atlas, const std::uint32_t atlas_edge,
                    const AtlasRect destination,
                    const openwow::data::image::DecodedImage &source) {
  if (source.width <= 0 || source.height <= 0 || source.pixels_rgba.empty() ||
      destination.width == 0 || destination.height == 0 ||
      destination.x + destination.width > atlas_edge ||
      destination.y + destination.height > atlas_edge) {
    return;
  }

  const auto source_width = static_cast<std::uint32_t>(source.width);
  const auto source_height = static_cast<std::uint32_t>(source.height);
  const bool source_is_full_atlas =
      source_width >= atlas_edge && source_height >= atlas_edge;

  for (std::uint32_t y = 0; y < destination.height; ++y) {
    for (std::uint32_t x = 0; x < destination.width; ++x) {
      std::uint32_t source_x = 0;
      std::uint32_t source_y = 0;
      if (source_is_full_atlas) {
        source_x = std::min(
            ((destination.x + x) * source_width) / atlas_edge, source_width - 1u);
        source_y = std::min(
            ((destination.y + y) * source_height) / atlas_edge, source_height - 1u);
      } else {
        source_x = std::min((x * source_width) / destination.width, source_width - 1u);
        source_y = std::min((y * source_height) / destination.height, source_height - 1u);
      }

      const std::size_t source_offset =
          (static_cast<std::size_t>(source_y) * source_width + source_x) * 4u;
      const std::size_t destination_offset =
          (static_cast<std::size_t>(destination.y + y) * atlas_edge + destination.x + x) * 4u;
      const std::uint8_t alpha = source.pixels_rgba[source_offset + 3u];
      if (alpha == 0u) {
        continue;
      }
      if (!source.has_alpha_channel) {
        atlas[destination_offset + 0u] = source.pixels_rgba[source_offset + 0u];
        atlas[destination_offset + 1u] = source.pixels_rgba[source_offset + 1u];
        atlas[destination_offset + 2u] = source.pixels_rgba[source_offset + 2u];
      } else {
        for (std::size_t channel = 0; channel < 3u; ++channel) {
          atlas[destination_offset + channel] = BlendRetailChannel(
              atlas[destination_offset + channel], source.pixels_rgba[source_offset + channel],
              alpha);
        }
      }
      atlas[destination_offset + 3u] = 255u;
    }
  }
}

void CompositeOptional(SourceImageCache &images, std::vector<std::uint8_t> &atlas,
                       const std::uint32_t edge, const std::string &path,
                       const std::size_t region_index) {
  if (region_index >= kRetailAtlasRegions.size()) {
    return;
  }
  if (const auto *source = images.Decode(path); source != nullptr) {
    CompositeImage(atlas, edge, ScaleRegion(kRetailAtlasRegions[region_index], edge), *source);
  }
}

std::shared_ptr<openwow::game::CharacterComponentItemDisplayRecordState>
BuildItemDisplayRecord(const openwow::data::dbc::ItemDisplayInfoEntry &display,
                       const std::uint8_t gender, SourceImageCache &images) {
  auto record =
      std::make_shared<openwow::game::CharacterComponentItemDisplayRecordState>();
  record->geoset_control_1 = display.geoset_control_1;
  record->geoset_control_2 = display.geoset_control_2;
  record->geoset_control_3 = display.geoset_control_3;
  for (std::size_t region = 0; region < display.component_texture_name.size(); ++region) {
    const auto texture_name = display.component_texture_name[region];
    if (texture_name.empty()) {
      continue;
    }
    const std::string path = openwow::game::ResolveComponentTexturePath(
        region, texture_name, gender,
        [&](const std::string &candidate) { return images.Exists(candidate); });
    if (path.empty()) {
      continue;
    }
    auto texture = std::make_shared<openwow::game::CharacterComponentTextureHandle>();
    texture->texture_path = path;
    texture->resolved_texture_path = path;
    record->component_textures[region] = std::move(texture);
  }
  return record;
}

void CompositeEquipment(
    SourceImageCache &images, std::vector<std::uint8_t> &atlas, const std::uint32_t edge,
    const CharacterAppearanceTextureSources &sources,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>
        *item_display_info) {
  if (item_display_info == nullptr) {
    return;
  }

  openwow::game::CharacterModelRefreshState model;
  const openwow::game::CharacterModelItemDisplayLookupFn lookup =
      [item_display_info](const std::uint32_t display_id) {
        return item_display_info->LookupEntry(display_id);
      };
  const openwow::game::CharacterModelItemDisplayRecordFactory factory =
      [&](const openwow::data::dbc::ItemDisplayInfoEntry &display) {
        return BuildItemDisplayRecord(display, sources.gender, images);
      };

  for (std::size_t equipment_slot = 0;
       equipment_slot < sources.equipment_display_ids.size(); ++equipment_slot) {
    const std::uint32_t display_id = sources.equipment_display_ids[equipment_slot];
    if (display_id == 0u) {
      continue;
    }
    (void)openwow::game::SetCharacterModelEquipmentSlotDisplayId(
        model, static_cast<std::uint32_t>(equipment_slot), display_id, lookup, factory);
  }

  for (std::size_t region = 0; region < 8u; ++region) {
    for (const auto &texture : model.texture_pages[region].queued_slots) {
      if (!texture || texture->texture_path.empty()) {
        continue;
      }
      CompositeOptional(images, atlas, edge, texture->texture_path, region);
    }
  }
}

void CompositeGuildTabard(SourceImageCache& images,
                          std::vector<std::uint8_t>& atlas,
                          const std::uint32_t edge,
                          const CharacterAppearanceTextureSources& sources) {
  if (!sources.guild_tabard_emblem.has_value() ||
      !openwow::game::HasResolvedGuildEmblem(
          *sources.guild_tabard_emblem)) {
    return;
  }

  const auto& emblem = *sources.guild_tabard_emblem;
  const auto composite_half =
      [&](const openwow::game::TabardTextureHalf half,
          const std::size_t atlas_region) {
        const std::array<std::string, 3> layers{
            openwow::game::BuildGuildTabardBackgroundTexturePath(
                half, emblem.background_color) +
                ".blp",
            openwow::game::BuildGuildTabardEmblemTexturePath(
                half, emblem.style, emblem.color) +
                ".blp",
            openwow::game::BuildGuildTabardBorderTexturePath(
                half, emblem.border_style, emblem.border_color) +
                ".blp",
        };
        for (const auto& layer : layers) {
          CompositeOptional(images, atlas, edge, layer, atlas_region);
        }
      };

  composite_half(openwow::game::TabardTextureHalf::Upper, 3u);
  composite_half(openwow::game::TabardTextureHalf::Lower, 4u);
}

void AppendDirectTexture(PreparedCharacterAppearanceTextures &out,
                         SourceImageCache &images, const std::uint32_t texture_type,
                         const std::string &path) {
  if (path.empty()) {
    return;
  }
  auto upload = TextureManager::PrepareTextureUpload(path, images.Bytes(path));
  if (!upload.valid) {

    return;
  }
  out.replaceable_paths.insert_or_assign(texture_type, path);
  out.direct_textures.push_back(std::move(upload));
}

}

std::string BuildCharacterAppearanceTextureCacheKey(
    const CharacterAppearanceTextureSources &sources) {
  if (!sources.HasBody()) {
    return {};
  }
  std::uint64_t hash = 14695981039346656037ull;
  const std::array<std::string_view, 12> paths{{
      sources.base_skin,
      sources.face_lower,
      sources.face_upper,
      sources.facial_hair_lower,
      sources.facial_hair_upper,
      sources.hair,
      sources.scalp_lower,
      sources.scalp_upper,
      sources.underwear_pelvis,
      sources.underwear_torso,
      sources.extra_skin,
      sources.cape,
  }};
  for (const auto path : paths) {
    hash = Fnv1aAppend(hash, CanonicalCacheIdentityPath(path));
  }
  hash ^= sources.gender;
  hash *= 1099511628211ull;
  for (const std::uint32_t display_id : sources.equipment_display_ids) {
    for (std::size_t byte = 0; byte < sizeof(display_id); ++byte) {
      hash ^= static_cast<std::uint8_t>(display_id >> (byte * 8u));
      hash *= 1099511628211ull;
    }
  }
  if (sources.guild_tabard_emblem.has_value()) {
    const auto& emblem = *sources.guild_tabard_emblem;
    const std::array<std::uint32_t, 5> values{
        emblem.style, emblem.color, emblem.border_style,
        emblem.border_color, emblem.background_color};
    for (const auto value : values) {
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        hash ^= static_cast<std::uint8_t>(value >> (byte * 8u));
        hash *= 1099511628211ull;
      }
    }
  }

  std::ostringstream key;
  key << "__openwow_character_body__/" << std::hex << std::setfill('0') << std::setw(16)
      << hash;
  return key.str();
}

PreparedCharacterAppearanceTextures PrepareCharacterAppearanceTextures(
    const CharacterAppearanceTextureSources &sources,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>
        *item_display_info,
    const CharacterTextureFileLoader &load_file) {
  PreparedCharacterAppearanceTextures out;
  out.cache_key = BuildCharacterAppearanceTextureCacheKey(sources);
  if (out.cache_key.empty() || !load_file) {
    out.error = "missing character base texture or file loader";
    return out;
  }

  SourceImageCache images(load_file);
  const auto *base = images.Decode(sources.base_skin);
  if (base == nullptr || base->width <= 0 || base->height <= 0 ||
      base->width != base->height) {
    out.error = "invalid character base texture: " + sources.base_skin;
    return out;
  }

  constexpr std::uint32_t edge = kRetailAtlasEdge;
  const std::size_t expected_bytes = static_cast<std::size_t>(edge) * edge * 4u;
  std::vector<std::uint8_t> atlas(expected_bytes, 0u);
  CompositeImage(atlas, edge, {0u, 0u, edge, edge}, *base);

  CompositeOptional(images, atlas, edge, sources.face_lower, 8u);
  CompositeOptional(images, atlas, edge, sources.face_upper, 9u);
  CompositeOptional(images, atlas, edge, sources.facial_hair_lower, 8u);
  CompositeOptional(images, atlas, edge, sources.facial_hair_upper, 9u);
  CompositeOptional(images, atlas, edge, sources.scalp_lower, 8u);
  CompositeOptional(images, atlas, edge, sources.scalp_upper, 9u);
  CompositeOptional(images, atlas, edge, sources.underwear_pelvis, 5u);
  CompositeOptional(images, atlas, edge, sources.underwear_torso, 3u);
  CompositeEquipment(images, atlas, edge, sources, item_display_info);
  CompositeGuildTabard(images, atlas, edge, sources);

  out.body.path = out.cache_key;
  out.body.rgba_bytes = std::move(atlas);
  out.body.width = edge;
  out.body.height = edge;
  out.body.upload_size = static_cast<std::uint32_t>(expected_bytes);
  out.body.complete_mip_chain = false;
  out.body.is_opaque = true;
  out.body.valid = true;
  out.replaceable_paths.insert_or_assign(kBodyTextureType, out.cache_key);

  AppendDirectTexture(out, images, kHairTextureType, sources.hair);
  AppendDirectTexture(out, images, kExtraSkinTextureType, sources.extra_skin);
  AppendDirectTexture(out, images, kCapeTextureType, sources.cape);

  out.valid = true;
  return out;
}

}
