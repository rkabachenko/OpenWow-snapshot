#include "realm_addon_handshake_composition.h"

#include "openwow/data/archive_system.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/net/wotlk/addon_handshake.h"
#include "openwow/ui/addon_manager.h"
#include "openwow/ui/addons_data.h"
#include "openwow/vfs/client_path_identity.h"
#include "openwow/vfs/virtual_file_system.h"

#include <atomic>
#include <filesystem>
#include <utility>

namespace openwow::client {
namespace {

std::atomic<const openwow::vfs::VirtualFileSystem*> g_content_vfs{nullptr};

openwow::net::wotlk::RealmAddonClientInfo ConvertAddon(
    const openwow::ui::AddonInfo& addon) {
  openwow::net::wotlk::RealmAddonClientInfo result{};
  result.name = addon.name;
  result.local_version = addon.version;
  result.addon_directory = addon.directory;
  result.enabled = addon.enabled;

  result.is_secure = addon.is_secure;
  result.public_key_state = addon.public_key_state;
  result.has_public_key = addon.has_public_key;
  result.public_key = addon.public_key;
  result.public_key_revision = addon.revision;
  result.update_url = addon.update_url;
  return result;
}

std::vector<openwow::net::wotlk::RealmAddonClientInfo> ConvertAddons(
    const std::vector<openwow::ui::AddonInfo>& addons) {
  std::vector<openwow::net::wotlk::RealmAddonClientInfo> result;
  result.reserve(addons.size());
  for (const auto& addon : addons) {
    result.push_back(ConvertAddon(addon));
  }
  return result;
}

}

RealmAddonHandshakeComposition::RealmAddonHandshakeComposition() {
  openwow::net::wotlk::RealmAddonHandshakeState::Instance().SetRuntimePorts({
      .discover_client_addons = [] {
        return ConvertAddons(openwow::ui::AddonManager::Get().SnapshotAddons());
      },
      .synchronize_server_info = [](const auto& addon) {
        const auto* digest = addon.has_secure_content_digest
                                 ? &addon.secure_content_digest
                                 : nullptr;
        openwow::ui::AddOnsData::Get().ApplyServerInfo(
            addon.name.c_str(), addon.server_state, addon.security_level,
            addon.is_corrupt, addon.server_public_key_newer, digest);
      },
      .resolve_catalog_cache_file_path = [] {
        const std::string cache_dir = openwow::data::GetCacheDirectory(
            openwow::data::ProbeCommonArchiveLayout(),
            openwow::data::GetCurrentLocaleInfo().language);
        return std::filesystem::path(cache_dir) / "baddons.wcf";
      },
      .load_client_file = [](const std::string& client_path,
                             std::vector<std::uint8_t>& bytes) {
        const auto* vfs = g_content_vfs.load(std::memory_order_acquire);
        if (vfs == nullptr) {
          return false;
        }
        const auto identity =
            openwow::vfs::MakeClientPathIdentity(client_path);
        if (identity.empty()) {
          return false;
        }
        auto contents = vfs->ReadFileBytes(identity.display_path);
        if (!contents.has_value()) {
          return false;
        }
        bytes = std::move(*contents);
        return true;
      },
  });
}

void RealmAddonHandshakeComposition::BindContentVfs(
    const openwow::vfs::VirtualFileSystem* const vfs) {
  g_content_vfs.store(vfs, std::memory_order_release);
}

RealmAddonHandshakeComposition::~RealmAddonHandshakeComposition() {
  openwow::net::wotlk::RealmAddonHandshakeState::Instance().ClearRuntimePorts();
}

void RealmAddonHandshakeComposition::PublishClientAddons(
    const std::vector<openwow::ui::AddonInfo>& addons) {
  openwow::net::wotlk::RealmAddonHandshakeState::Instance().SetClientAddons(
      ConvertAddons(addons));
}

}
