#pragma once

#include "openwow/game/account_data.h"

#include <cstdint>
#include <chrono>
#include <functional>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

class BindingProfiles;
class MacroCatalog;
class WorldSession;
}

namespace openwow::ui::game::runtime {
class RetainedLayout;
}
namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

struct AccountDataUploadContext {
  std::function<bool(const openwow::net::wotlk::WorldPacket&)> send_packet;
  const openwow::data::dbc::DbcLoader* dbc = nullptr;
  std::uint32_t zone_id = 0;
  BindingProfiles* binding_profiles = nullptr;
  const MacroCatalog* macro_catalog = nullptr;
  const openwow::world::WorldCamera* world_camera = nullptr;
  openwow::ui::game::runtime::RetainedLayout* retained_layout = nullptr;
  bool include_config = true;
};

[[nodiscard]] inline bool ShouldApplyAccountDataUpdate(
    const AccountDataType type, const std::uint64_t packet_guid,
    const std::uint64_t current_character_guid) {
  return !IsPerCharacterData(type) || packet_guid == current_character_guid;
}

void ApplyAccountDataPayload(WorldSession& session,
                             AccountDataType type,
                             std::uint32_t timestamp,
                             const std::string& data,
                             BindingProfiles* bindings = nullptr,
                             MacroCatalog* macros = nullptr,
                             openwow::ui::game::runtime::RetainedLayout*
                                 retained_layout = nullptr);

void ApplyCachedAccountDataPayload(WorldSession& session,
                                   AccountDataType type,
                                   const std::string& data,
                                   BindingProfiles* bindings = nullptr,
                                   MacroCatalog* macros = nullptr,
                                   openwow::ui::game::runtime::RetainedLayout*
                                       retained_layout = nullptr);

void SyncRuntimeConfigAccountData(
    const openwow::world::WorldCamera* world_camera);

bool DownloadRuntimeAccountData(
    const std::function<bool(const openwow::net::wotlk::WorldPacket&)>&
        send_packet);

bool UploadRuntimeAccountData(const AccountDataUploadContext& context);

bool PumpRuntimeAccountDataUpload(
    const AccountDataUploadContext& context,
    AccountData::UploadClock::time_point now = AccountData::UploadClock::now());

}
