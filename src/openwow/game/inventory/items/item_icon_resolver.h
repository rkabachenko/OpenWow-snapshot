#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/texture_path_util.h"
#include "openwow/game/violence_level.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

inline constexpr std::string_view kFallbackItemInventoryIconName =
    "INV_Misc_QuestionMark";
inline constexpr std::string_view kItemInventoryIconTexturePrefix =
    "Interface\\Icons\\";

[[nodiscard]] inline std::string ResolveItemInventoryIconName(
    const data::dbc::DbcLoader* const dbc,
    const std::uint32_t display_id,
    const std::int32_t violence_level) {
  if (dbc != nullptr) {
    if (const auto* const display =
            dbc->item_display_info().LookupEntry(display_id);
        display != nullptr) {
      std::string_view selected = display->inventory_icon;
      if (violence_level < 2 &&
          !display->inventory_icon_2.empty()) {
        selected = display->inventory_icon_2;
      }
      if (!selected.empty()) {
        const auto extension = data::ClassifyTexturePathExtension(selected);
        if (extension == data::TexturePathExtension::kTga) {
          std::string normalized;
          static_cast<void>(data::SwapTexturePathTgaBlpExtension(
              selected, extension, normalized));
          return normalized;
        }
        return std::string(selected);
      }
    }
  }

  diagnostics::Log(diagnostics::LogLevel::kWarn,
            "NOINVENTORYICON|" + std::to_string(display_id));
  return std::string(kFallbackItemInventoryIconName);
}

[[nodiscard]] inline std::string ResolveItemInventoryIconName(
    const data::dbc::DbcLoader* const dbc,
    const std::uint32_t display_id) {
  return ResolveItemInventoryIconName(dbc, display_id,
                                      GetClientViolenceLevel());
}

[[nodiscard]] inline std::string BuildItemInventoryIconTexturePath(
    const std::string_view icon_name) {
  std::string path(kItemInventoryIconTexturePrefix);
  path.append(icon_name);
  return path;
}

[[nodiscard]] inline std::string ResolveItemInventoryIconTexturePath(
    const data::dbc::DbcLoader* const dbc,
    const std::uint32_t display_id,
    const std::int32_t violence_level) {
  return BuildItemInventoryIconTexturePath(ResolveItemInventoryIconName(
      dbc, display_id, violence_level));
}

[[nodiscard]] inline std::string ResolveItemInventoryIconTexturePath(
    const data::dbc::DbcLoader* const dbc,
    const std::uint32_t display_id) {
  return BuildItemInventoryIconTexturePath(
      ResolveItemInventoryIconName(dbc, display_id));
}

}
