#pragma once

#include "openwow/render/models/characters/character_appearance_texture_sources.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
template <typename T> class DbcStore;
struct ItemDisplayInfoEntry;
}

namespace openwow::render {

using CharacterTextureFileLoader =
    std::function<std::vector<std::uint8_t>(const std::string &path)>;

struct PreparedCharacterAppearanceTextures {
  bool valid{false};
  std::string cache_key;
  std::string error;
  PreparedTextureUpload body;
  std::vector<PreparedTextureUpload> direct_textures;
  std::unordered_map<std::uint32_t, std::string> replaceable_paths;
};

[[nodiscard]] std::string BuildCharacterAppearanceTextureCacheKey(
    const CharacterAppearanceTextureSources &sources);

[[nodiscard]] PreparedCharacterAppearanceTextures PrepareCharacterAppearanceTextures(
    const CharacterAppearanceTextureSources &sources,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>
        *item_display_info,
    const CharacterTextureFileLoader &load_file);

}
