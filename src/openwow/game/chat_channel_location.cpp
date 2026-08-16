#include "openwow/game/chat_channel_location.h"

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/world_session.h"

#include <cstdint>
#include <string_view>

namespace openwow::game {
namespace {

constexpr std::uint32_t kUseZoneTextInCapitalsFlag = 0x20u;

}

std::optional<std::string> ResolveBuiltinChatChannelName(
    const WorldSession& session,
    const data::dbc::ChatChannelsEntry& definition) {
  std::string area_name = session.scene_state().GetRealZoneText();
  if (area_name.empty()) {
    return std::nullopt;
  }

  const auto* dbc = session.GetDbcLoader();
  const auto current_area_id = session.objects().GetAreaId();
  if (dbc != nullptr && current_area_id != 0 &&
      (definition.flags & kUseZoneTextInCapitalsFlag) != 0) {
    if (const auto* area = dbc->area_table().LookupEntry(current_area_id);
        area != nullptr &&
        (area->flags & data::dbc::kAreaFlagCapital) != 0) {
      const std::string zone_name = session.scene_state().GetZoneText();
      if (!zone_name.empty()) {
        area_name = zone_name;
      }
    }
  }

  std::string channel_name(definition.pattern);
  if (const auto placeholder = channel_name.find("%s");
      placeholder != std::string::npos) {
    channel_name.replace(placeholder, 2, area_name);
  }
  return channel_name;
}

}
