#include "openwow/ui/game/framescript/widgets/texture_asset_probe.h"
#include "openwow/data/image/image_decoder.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/texture_natural_size.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/virtual_file_system.h"
#include <lua.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace openwow::ui::game::frame_api {
const openwow::vfs::VirtualFileSystem *GetTextureValidationVfs(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, openwow::ui::game::detail::kTextureVfsRegistryKey);
  const auto *vfs = static_cast<const openwow::vfs::VirtualFileSystem *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return vfs;
}

std::string NormalizeTextureProbeBase(std::string_view raw_path) {
  std::string candidate(raw_path);
  std::replace(candidate.begin(), candidate.end(), '\\', '/');
  while (!candidate.empty() && std::isspace(static_cast<unsigned char>(candidate.front())) != 0) {
    candidate.erase(candidate.begin());
  }
  while (!candidate.empty() && std::isspace(static_cast<unsigned char>(candidate.back())) != 0) {
    candidate.pop_back();
  }
  if (!candidate.empty() && candidate.front() != '/') {
    candidate.insert(candidate.begin(), '/');
  }
  return candidate;
}

void AppendTextureProbeCandidate(std::vector<std::string> *candidates,
                                 const std::string &candidate) {
  if (candidate.empty()) {
    return;
  }
  if (std::find(candidates->begin(), candidates->end(), candidate) == candidates->end()) {
    candidates->push_back(candidate);
  }
  if (candidate.front() == '/' && candidate.size() > 1) {
    const std::string without_slash = candidate.substr(1);
    if (std::find(candidates->begin(), candidates->end(), without_slash) == candidates->end()) {
      candidates->push_back(without_slash);
    }
  }
}

std::vector<std::string> BuildTextureProbeCandidates(std::string_view requested_path) {
  const std::string base = NormalizeTextureProbeBase(requested_path);
  if (base.empty()) {
    return {};
  }

  std::vector<std::string> candidates;
  const bool has_extension = std::filesystem::path(base).has_extension();
  if (has_extension) {
    AppendTextureProbeCandidate(&candidates, base);
    return candidates;
  }

  static constexpr std::string_view kFallbackExtensions[] = {
      ".blp",
      ".tga",
      ".png",
  };
  for (const auto extension : kFallbackExtensions) {
    AppendTextureProbeCandidate(&candidates, base + std::string(extension));
  }
  return candidates;
}

bool TryReadTextureProbeBytes(const openwow::vfs::VirtualFileSystem *vfs,
                              const std::string &candidate_path,
                              std::vector<std::uint8_t> *out_bytes) {
  if (out_bytes == nullptr) {
    return false;
  }

  if (vfs != nullptr) {
    if (const auto bytes = vfs->ReadFileBytes(candidate_path);
        bytes.has_value() && !bytes->empty()) {
      *out_bytes = *bytes;
      return true;
    }
  }

  void *loaded_data = nullptr;
  std::size_t loaded_size = 0;
  if (openwow::vfs::SFileReadFileToBuffer_Wrapper(candidate_path.c_str(), &loaded_data,
                                                  &loaded_size, 0, 0) == 0) {
    return false;
  }

  const auto cleanup = [&]() {
    if (loaded_data != nullptr) {
      openwow::vfs::SFileFreeLoadedData(loaded_data);
      loaded_data = nullptr;
    }
  };

  if (loaded_data == nullptr || loaded_size == 0) {
    cleanup();
    return false;
  }

  out_bytes->assign(static_cast<const std::uint8_t *>(loaded_data),
                    static_cast<const std::uint8_t *>(loaded_data) + loaded_size);
  cleanup();
  return !out_bytes->empty();
}

bool CanLoadTextureRequest(lua_State *L, std::string_view requested_path) {
  const auto candidates = BuildTextureProbeCandidates(requested_path);
  if (candidates.empty()) {
    return false;
  }

  const auto *vfs = GetTextureValidationVfs(L);
  for (const auto &candidate : candidates) {
    std::vector<std::uint8_t> bytes;
    if (!TryReadTextureProbeBytes(vfs, candidate, &bytes)) {
      continue;
    }

    const auto decoded = openwow::data::image::DecodeImage(bytes);
    if (decoded.ok && decoded.width > 0 && decoded.height > 0 && !decoded.pixels_rgba.empty()) {
      return true;
    }
  }
  return false;
}

void QueueRegionTextureLoad(lua_State *L, const std::string &texture_path) {
  if (texture_path.empty()) {
    return;
  }
  const auto *game_ui = runtime::WorldUiRuntimeContext::FromLua(L);
  if (game_ui == nullptr) {
    return;
  }
  if (auto *const source =
          game_ui->retained_layout().texture_natural_size_source();
      source != nullptr) {
    source->QueueTextureLoad(texture_path);
  }
}

void ClearLuaStringField(lua_State *L, int self_idx, const char *field_name) {
  self_idx = lua_absindex(L, self_idx);
  lua_pushnil(L);
  lua_setfield(L, self_idx, field_name);
}

}
