#pragma once

#include "openwow/ui/game/framescript/xml/frame_xml_loader.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace openwow::game {
class BindingProfiles;
}

namespace openwow::ui {
class AddonManager;
class AddOnsData;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

struct lua_State;

namespace openwow::ui::game {

class GameUIManager;
class SecureExecution;
class UiLoadStatusSink;

struct AddonRuntimeIdentity {
  std::string account_name;
  std::string realm_name;
  std::string character_name;
};

struct AddonRuntimeLoadContext {
  const AddonRuntimeIdentity &identity;
  UiLoadStatusSink *status_sink{nullptr};
  TocLoadProgress *progress{nullptr};
  bool allow_load_on_demand{false};
};

class AddonLoadLogSession {
public:
  AddonLoadLogSession();
  ~AddonLoadLogSession();

  AddonLoadLogSession(const AddonLoadLogSession &) = delete;
  AddonLoadLogSession &operator=(const AddonLoadLogSession &) = delete;

  [[nodiscard]] UiLoadStatusSink *sink() const noexcept;

private:
  UiLoadStatusSink *sink_{nullptr};
};

class AddonRuntimeLoader {
public:
  AddonRuntimeLoader(GameUIManager &ui_manager, openwow::ui::AddonManager &addon_manager,
                     openwow::ui::AddOnsData &addons_data,
                     openwow::game::BindingProfiles &binding_profiles,
                     const openwow::vfs::VirtualFileSystem &vfs, lua_State &lua_state,
                     SecureExecution &secure_execution) noexcept;

  [[nodiscard]] static std::optional<AddonRuntimeIdentity> ResolvePersistenceIdentity(
      const AddonRuntimeIdentity &identity,
      const std::filesystem::path &wtf_root = "WTF");
  [[nodiscard]] std::size_t ComputeStartupTocEntryBudget(const char *character_name) const;
  void LoadStartup(const AddonRuntimeLoadContext &context);
  [[nodiscard]] bool Load(std::string_view addon_name,
                          const AddonRuntimeLoadContext &context);

  [[nodiscard]] bool SaveLoadedSavedVariables(const AddonRuntimeIdentity &identity);

private:
  [[nodiscard]] bool LoadInternal(std::string_view addon_name,
                                  const AddonRuntimeLoadContext &context,
                                  std::unordered_set<std::string> &active_chain);

  GameUIManager &ui_manager_;
  openwow::ui::AddonManager &addon_manager_;
  openwow::ui::AddOnsData &addons_data_;
  openwow::game::BindingProfiles &binding_profiles_;
  const openwow::vfs::VirtualFileSystem &vfs_;
  lua_State &lua_state_;
  SecureExecution &secure_execution_;
};

}
