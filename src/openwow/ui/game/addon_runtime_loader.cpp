#include "openwow/ui/game/addon_runtime_loader.h"

#include "openwow/core/client_init.h"
#include "openwow/core/storm_utils.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/xml/binding_xml_adapter.h"
#include "openwow/net/wotlk/addon_handshake.h"
#include "openwow/ui/addon_manager.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/game/saved_variables.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/ui_load_status.h"
#include "openwow/ui/game/ui_load_status_log.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/vfs/client_path_identity.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/virtual_file_system.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game {

namespace {

constexpr int kSecureAddonDigestMismatchErrorCode = 10;

struct AddonLoadLogState {
  std::unique_ptr<WoWClientLogFile> log_file;
  std::size_t depth{0};
};

AddonLoadLogState &GetAddonLoadLogState() {
  static AddonLoadLogState state;
  return state;
}

bool AsciiCaseInsensitiveEquals(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

bool IsBoundedAddonTocPath(const std::string_view toc_path,
                           const std::string_view addon_name) {
  std::string normalized(toc_path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  std::vector<std::string> components;
  for (const auto &component : std::filesystem::path(normalized)) {
    const std::string value = component.string();
    if (value.empty() || value == "/" || value == ".") {
      continue;
    }
    if (value == "..") {
      return false;
    }
    components.push_back(value);
  }
  return components.size() == 4 && AsciiCaseInsensitiveEquals(components[0], "Interface") &&
         AsciiCaseInsensitiveEquals(components[1], "AddOns") &&
         AsciiCaseInsensitiveEquals(components[2], addon_name) &&
         AsciiCaseInsensitiveEquals(components[3], std::string(addon_name) + ".toc");
}

const char *ResolveCharacterName(const AddonRuntimeIdentity &identity) {
  return identity.character_name.empty() ? nullptr : identity.character_name.c_str();
}

bool AddonRunsInSecureContext(const openwow::ui::AddOnState &addon_state) {

  return addon_state.security == 0;
}

class ScopedAddonExecutionContext {
 public:
  ScopedAddonExecutionContext(SecureExecution &execution, lua_State* const state, const bool secure,
                               const std::string_view addon_name)
      : execution_(execution) {
    if (secure) {
      execution_.PushSecure(state);
    } else {
      execution_.PushInsecure(state, addon_name);
    }
  }

  ~ScopedAddonExecutionContext() {
    execution_.Pop();
  }

  ScopedAddonExecutionContext(const ScopedAddonExecutionContext&) = delete;
  ScopedAddonExecutionContext& operator=(const ScopedAddonExecutionContext&) = delete;

 private:
  SecureExecution &execution_;
};

class ScopedLuaStack final {
public:
  explicit ScopedLuaStack(lua_State &state) noexcept
      : state_(state), top_(lua_gettop(&state)) {}
  ~ScopedLuaStack() { lua_settop(&state_, top_); }

  ScopedLuaStack(const ScopedLuaStack &) = delete;
  ScopedLuaStack &operator=(const ScopedLuaStack &) = delete;

private:
  lua_State &state_;
  int top_;
};

void AppendAddonStatus(UiLoadStatusSink *sink, const std::string &addon_name,
                       const UiLoadStatusBuffer &addon_status) {
  if (sink == nullptr || addon_status.empty()) {
    return;
  }

  UiLoadStatusBuffer labeled_status;
  labeled_status.AppendFrom(addon_status);
  labeled_status.AppendStatus(0, "Loading add-on " + addon_name);
  labeled_status.ReplayInto(*sink);
}

bool ExecuteAddonSavedVariablesFile(lua_State *state, const std::string &addon_name,
                                    const std::filesystem::path &path,
                                    UiLoadStatusSink *status_sink) {
  if (state == nullptr) {
    return false;
  }
  const ScopedLuaStack stack(*state);
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return true;
  }

  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const std::string chunk_name = "@" + path.generic_string();
  InstallLuaAddonMemoryTracker(state);
  const ScopedLuaAddonMemoryOwner owner_scope(state, addon_name);
  if (openwow::ui::LoadClientLuaChunk(state, std::string_view(source), chunk_name.c_str()) != 0) {
    const char *error = lua_tostring(state, -1);
    if (status_sink != nullptr) {
      status_sink->AppendStatus(0, "AddOn SavedVariables compile error in " +
                                       path.generic_string() + ": " +
                                       (error != nullptr ? error : "(null)"));
    }
    return false;
  }

  if (lua_pcall(state, 0, 0, 0) != 0) {
    const char *error = lua_tostring(state, -1);
    if (status_sink != nullptr) {
      status_sink->AppendStatus(0, "AddOn SavedVariables runtime error in " +
                                       path.generic_string() + ": " +
                                       (error != nullptr ? error : "(null)"));
    }
    return false;
  }
  return true;
}

bool LoadAddonSavedVariables(lua_State *state, const std::string &addon_name,
                             const AddonRuntimeIdentity &identity,
                             UiLoadStatusSink *status_sink) {
  if (state == nullptr ||
      !openwow::platform::filesystem::IsSafePathComponent(addon_name)) {
    return false;
  }
  if (identity.account_name.empty()) {
    return true;
  }
  if (!openwow::platform::filesystem::IsSafePathComponent(identity.account_name)) {
    if (status_sink != nullptr) {
      status_sink->AppendStatus(0, "AddOn SavedVariables rejected account path for " +
                                      addon_name);
    }
    return false;
  }

  const std::filesystem::path account_path = std::filesystem::path("WTF") / "Account" /
                                             identity.account_name / "SavedVariables" /
                                             (addon_name + ".lua");
  const bool account_loaded =
      ExecuteAddonSavedVariablesFile(state, addon_name, account_path, status_sink);

  if (identity.realm_name.empty() || identity.character_name.empty()) {
    return account_loaded;
  }
  if (!openwow::platform::filesystem::IsSafePathComponent(identity.realm_name) ||
      !openwow::platform::filesystem::IsSafePathComponent(identity.character_name)) {
    if (status_sink != nullptr) {
      status_sink->AppendStatus(0, "AddOn SavedVariables rejected character path for " +
                                      addon_name);
    }
    return false;
  }

  const std::filesystem::path character_path =
      std::filesystem::path("WTF") / "Account" / identity.account_name / identity.realm_name /
      identity.character_name / "SavedVariables" / (addon_name + ".lua");
  const bool character_loaded =
      ExecuteAddonSavedVariablesFile(state, addon_name, character_path, status_sink);
  return account_loaded && character_loaded;
}

enum class AddonSavedVariablesAction {
  kNoLooseManifestChange,
  kInvalidateLooseManifest,
  kWriteFailed,
};

bool IsSerializableSavedVariableName(const std::string_view name) {
  if (name.empty()) {
    return false;
  }

  for (const unsigned char ch : name) {
    if ((ch & 0x80u) != 0u) {
      continue;
    }
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        ch == '_') {
      continue;
    }
    return false;
  }

  return true;
}

AddonSavedVariablesAction DeleteAddonSavedVariablesFile(const std::filesystem::path &file_path) {
  if (!openwow::platform::filesystem::PathIsRegularFile(file_path)) {
    return AddonSavedVariablesAction::kNoLooseManifestChange;
  }

  return openwow::core::DeleteStormFilePath(file_path.generic_string().c_str())
             ? AddonSavedVariablesAction::kInvalidateLooseManifest
             : AddonSavedVariablesAction::kWriteFailed;
}

AddonSavedVariablesAction
SaveAddonSavedVariablesFile(lua_State *state, const std::filesystem::path &file_path,
                            const std::vector<std::string> &declared_names) {
  if (declared_names.empty()) {
    return DeleteAddonSavedVariablesFile(file_path);
  }

  std::error_code ec;
  std::filesystem::create_directories(file_path.parent_path(), ec);
  if (ec) {
    return AddonSavedVariablesAction::kWriteFailed;
  }

  const std::filesystem::path backup_path = file_path.string() + ".bak";
  const bool had_regular_file = openwow::platform::filesystem::PathIsRegularFile(file_path);
  if (had_regular_file && !openwow::platform::filesystem::PathIsRegularFile(backup_path)) {
    (void)openwow::platform::filesystem::CopyFilePath(file_path, backup_path, false);
  }

  std::string serialized;
  for (const std::string &name : declared_names) {
    if (!IsSerializableSavedVariableName(name)) {
      continue;
    }

    const ScopedLuaStack stack(*state);
    lua_getglobal(state, name.c_str());
    serialized += "\r\n";
    serialized += name;
    serialized += " = ";
    serialized += SerializeLuaValue(state, -1);
  }

  serialized += "\r\n";
  if (!openwow::platform::filesystem::AtomicWriteFile(file_path, serialized)) {
    return AddonSavedVariablesAction::kWriteFailed;
  }
  return had_regular_file ? AddonSavedVariablesAction::kNoLooseManifestChange
                          : AddonSavedVariablesAction::kInvalidateLooseManifest;
}

void EnsureAddonSavedVariablesDirectories(const AddonRuntimeIdentity &identity) {
  if (identity.account_name.empty()) {
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path("WTF") / "Account" / identity.account_name / "SavedVariables", ec);

  if (!identity.realm_name.empty() && !identity.character_name.empty()) {
    ec.clear();
    std::filesystem::create_directories(std::filesystem::path("WTF") / "Account" /
                                            identity.account_name / identity.realm_name /
                                            identity.character_name / "SavedVariables",
                                        ec);
  }
}

std::vector<openwow::ui::AddonInfo>
SnapshotAddonsInRegistrationOrder(const openwow::ui::AddonManager &addon_manager) {
  return addon_manager.SnapshotAddonsByRegistrationOrder();
}

}

AddonRuntimeLoader::AddonRuntimeLoader(
    GameUIManager &ui_manager, openwow::ui::AddonManager &addon_manager,
    openwow::ui::AddOnsData &addons_data,
    openwow::game::BindingProfiles &binding_profiles,
    const openwow::vfs::VirtualFileSystem &vfs, lua_State &lua_state,
    SecureExecution &secure_execution) noexcept
    : ui_manager_(ui_manager), addon_manager_(addon_manager), addons_data_(addons_data),
      binding_profiles_(binding_profiles), vfs_(vfs), lua_state_(lua_state),
      secure_execution_(secure_execution) {}

std::optional<AddonRuntimeIdentity> AddonRuntimeLoader::ResolvePersistenceIdentity(
    const AddonRuntimeIdentity &identity,
    const std::filesystem::path &wtf_root) {
  namespace fs = std::filesystem;

  if (!openwow::platform::filesystem::IsSafePathComponent(identity.account_name) ||
      !openwow::platform::filesystem::IsSafePathComponent(identity.realm_name) ||
      !openwow::platform::filesystem::IsSafePathComponent(identity.character_name)) {
    return std::nullopt;
  }

  const fs::path account_root = wtf_root / "Account";
  const auto account =
      openwow::platform::filesystem::ResolveExistingPathComponentCaseInsensitive(
          account_root, identity.account_name);
  if (!account.has_value()) {
    return std::nullopt;
  }
  const fs::path account_path = account_root / *account;
  const auto realm = openwow::platform::filesystem::ResolveExistingPathComponentCaseInsensitive(
      account_path, identity.realm_name);
  if (!realm.has_value()) {
    return std::nullopt;
  }
  const fs::path realm_path = account_path / *realm;
  const auto character =
      openwow::platform::filesystem::ResolveExistingPathComponentCaseInsensitive(
          realm_path, identity.character_name);
  if (!character.has_value() ||
      !openwow::platform::filesystem::IsSafeChildPath(wtf_root,
                                     realm_path / *character)) {
    return std::nullopt;
  }

  return AddonRuntimeIdentity{
      .account_name = *account,
      .realm_name = *realm,
      .character_name = *character,
  };
}

std::size_t AddonRuntimeLoader::ComputeStartupTocEntryBudget(
    const char *character_name) const {
  std::size_t total = 0;
  for (const auto &addon : addon_manager_.SnapshotAddonsByRegistrationOrder()) {
    if (addons_data_.IsAddonLoadOnDemand(addon.name.c_str())) {
      continue;
    }

    bool handled_by_load_manager = false;
    for (const auto &manager_name : addon.load_managers) {
      const auto manager_loadability =
          addons_data_.EvaluateLoadability(manager_name.c_str(), false, character_name);
      if (manager_loadability.loadable) {
        handled_by_load_manager = true;
        break;
      }
    }
    if (handled_by_load_manager) {
      continue;
    }

    const auto loadability =
        addons_data_.EvaluateLoadability(addon.name.c_str(), false, character_name);
    if (loadability.loadable) {
      total += addon.toc_file_entry_count;
    }
  }
  return total;
}

bool AddonRuntimeLoader::SaveLoadedSavedVariables(const AddonRuntimeIdentity &identity) {
  const auto resolved_identity =
      ResolvePersistenceIdentity(identity);
  if (!resolved_identity.has_value()) {
    for (const auto &addon : SnapshotAddonsInRegistrationOrder(addon_manager_)) {
      if (addons_data_.IsAddonLoaded(addon.name.c_str())) {
        addons_data_.SetAddonLoadedState(addon.name.c_str(), false, false);
      }
    }
    return false;
  }
  const auto& safe_identity = *resolved_identity;

  EnsureAddonSavedVariablesDirectories(safe_identity);

  bool invalidate_loose_manifest = false;
  bool all_saved = true;

  for (const auto &addon : SnapshotAddonsInRegistrationOrder(addon_manager_)) {
    if (!addons_data_.IsAddonLoaded(addon.name.c_str())) {
      continue;
    }
    if (!openwow::platform::filesystem::IsSafePathComponent(addon.name)) {
      all_saved = false;
      addons_data_.SetAddonLoadedState(addon.name.c_str(), false, false);
      continue;
    }

    const auto account_action = SaveAddonSavedVariablesFile(
        &lua_state_,
        std::filesystem::path("WTF") / "Account" / safe_identity.account_name / "SavedVariables" /
            (addon.name + ".lua"),
        addon.saved_variables);
    if (account_action == AddonSavedVariablesAction::kInvalidateLooseManifest) {
      invalidate_loose_manifest = true;
    }
    bool addon_saved = account_action != AddonSavedVariablesAction::kWriteFailed;

    if (!safe_identity.realm_name.empty() && !safe_identity.character_name.empty()) {
      const auto character_action = SaveAddonSavedVariablesFile(
          &lua_state_,
          std::filesystem::path("WTF") / "Account" / safe_identity.account_name /
              safe_identity.realm_name / safe_identity.character_name /
              "SavedVariables" / (addon.name + ".lua"),
          addon.saved_variables_per_char);
      if (character_action == AddonSavedVariablesAction::kInvalidateLooseManifest) {
        invalidate_loose_manifest = true;
      }
      addon_saved = character_action != AddonSavedVariablesAction::kWriteFailed && addon_saved;

      if (addon.saved_variables_per_char.empty() &&
          character_action != AddonSavedVariablesAction::kWriteFailed) {
        const auto legacy_character_action =
            DeleteAddonSavedVariablesFile(std::filesystem::path("WTF") / "Account" /
                                          safe_identity.account_name /
                                          safe_identity.character_name /
                                          "SavedVariables" / (addon.name + ".lua"));
        if (legacy_character_action == AddonSavedVariablesAction::kInvalidateLooseManifest) {
          invalidate_loose_manifest = true;
        }
        addon_saved = legacy_character_action != AddonSavedVariablesAction::kWriteFailed &&
                      addon_saved;
      }
    }

    addons_data_.SetAddonLoadedState(addon.name.c_str(), false, false);
    if (!addon_saved) {
      all_saved = false;
    }
  }

  if (invalidate_loose_manifest) {
    openwow::vfs::InvalidateSFileLooseManifestCache();
  }
  return all_saved;
}

AddonLoadLogSession::AddonLoadLogSession() {
  auto &state = GetAddonLoadLogState();
  if (state.depth == 0) {
    state.log_file =
        std::make_unique<WoWClientLogFile>("Logs\\FrameXML.log", WoWClientLogOpenMode::kAppend);
  }

  ++state.depth;
  sink_ = state.log_file.get();
}

AddonLoadLogSession::~AddonLoadLogSession() {
  auto &state = GetAddonLoadLogState();
  if (state.depth == 0) {
    return;
  }

  --state.depth;
  if (state.depth == 0) {
    state.log_file.reset();
  }
}

UiLoadStatusSink *AddonLoadLogSession::sink() const noexcept {
  return sink_;
}

void AddonRuntimeLoader::LoadStartup(const AddonRuntimeLoadContext &context) {

  for (const auto &addon : SnapshotAddonsInRegistrationOrder(addon_manager_)) {
    for (const auto &manager_name : addon.load_managers) {
      AddonRuntimeLoadContext manager_context = context;
      manager_context.allow_load_on_demand = false;
      if (!Load(manager_name, manager_context)) {
        continue;
      }
      addons_data_.SetAddonLoadOnDemand(addon.name.c_str(), true);
      break;
    }

    if (!addons_data_.IsAddonLoadOnDemand(addon.name.c_str())) {
      AddonRuntimeLoadContext startup_context = context;
      startup_context.allow_load_on_demand = false;
      (void)Load(addon.name, startup_context);
    }
  }
}

bool AddonRuntimeLoader::Load(const std::string_view addon_name,
                              const AddonRuntimeLoadContext &context) {
  std::unordered_set<std::string> active_chain;
  return LoadInternal(addon_name, context, active_chain);
}

bool AddonRuntimeLoader::LoadInternal(
    const std::string_view addon_name, const AddonRuntimeLoadContext &context,
    std::unordered_set<std::string> &active_chain) {
  const std::string lookup_name(addon_name);
  const auto *addon_state = addons_data_.FindAddon(lookup_name.c_str());
  if (addon_state == nullptr) {
    return false;
  }
  if (addon_state->loaded != 0) {
    return true;
  }
  if (!openwow::platform::filesystem::IsSafePathComponent(addon_state->name)) {
    return false;
  }
  if (!IsBoundedAddonTocPath(addon_state->toc_path, addon_state->name)) {
    if (context.status_sink != nullptr) {
      context.status_sink->AppendStatus(0, "Rejected add-on TOC path for " + addon_state->name);
    }
    return false;
  }
  if (!active_chain.insert(addon_state->name).second) {
    return true;
  }

  const auto loadability = addons_data_.EvaluateLoadability(
      addon_state->name.c_str(), context.allow_load_on_demand,
      ResolveCharacterName(context.identity));
  if (!loadability.loadable) {
    active_chain.erase(addon_state->name);
    return false;
  }

  addons_data_.SetAddonLoadedState(addon_state->name.c_str(), true, false);

  for (const auto &dependency_name : addon_state->optional_deps) {
    AddonRuntimeLoadContext dependency_context = context;
    dependency_context.allow_load_on_demand = true;
    (void)LoadInternal(dependency_name, dependency_context, active_chain);
  }
  for (const auto &dependency_name : addon_state->dependencies) {
    if (addons_data_.IsAddonLoaded(dependency_name.c_str())) {
      continue;
    }
    AddonRuntimeLoadContext dependency_context = context;
    dependency_context.allow_load_on_demand = true;
    if (!LoadInternal(dependency_name, dependency_context, active_chain)) {
      addons_data_.SetAddonLoadedState(addon_state->name.c_str(), false, false);
      active_chain.erase(addon_state->name);
      return false;
    }
  }

  UiLoadStatusBuffer addon_status;
  const bool secure_context = AddonRunsInSecureContext(*addon_state);
  {
    const ScopedLuaStack stack(lua_state_);
    const ScopedAddonExecutionContext execution_scope(
        secure_execution_, &lua_state_, secure_context, addon_state->name);

    if (!ui_manager_.LoadToc(addon_state->toc_path, &addon_status,
                             context.progress, addon_state->name, nullptr)) {
      AppendAddonStatus(context.status_sink, addon_state->name, addon_status);
      addons_data_.SetAddonLoadedState(addon_state->name.c_str(), false,
                                       false);
      active_chain.erase(addon_state->name);
      return false;
    }

    const std::string bindings_xml_path =
        "/Interface/AddOns/" + addon_state->name + "/Bindings.xml";
    if (vfs_.Exists(bindings_xml_path)) {
      (void)openwow::game::actions::bindings::adapters::xml::BindingXmlAdapter::
          LoadFile(binding_profiles_, &lua_state_, &vfs_, bindings_xml_path,
                   &addon_status, nullptr);
    }
    (void)LoadAddonSavedVariables(&lua_state_, addon_state->name,
                                  context.identity, &addon_status);

    if (secure_context) {
      const auto actual_digest = openwow::net::wotlk::ComputeAddonContentDigest(
          addon_state->name,
          [this](const std::string& client_path,
                 std::vector<std::uint8_t>& bytes) {
            const auto identity =
                openwow::vfs::MakeClientPathIdentity(client_path);
            if (identity.empty()) {
              return false;
            }
            auto contents = vfs_.ReadFileBytes(identity.display_path);
            if (!contents.has_value()) {
              return false;
            }
            bytes = std::move(*contents);
            return true;
          });
      if (actual_digest != addon_state->secure_content_digest) {
        (void)openwow::core::RequestClientShutdownWithErrorCode(
            kSecureAddonDigestMismatchErrorCode);
        addon_status.AppendStatus(
            kSecureAddonDigestMismatchErrorCode,
            "AddOn secure content digest mismatch for " + addon_state->name);
      }
    }
  }

  AppendAddonStatus(context.status_sink, addon_state->name, addon_status);
  addons_data_.SetAddonLoadedState(addon_state->name.c_str(), true, true);
  ui_manager_.frame_events().OnAddonLoaded(addon_state->name);

  for (const auto &candidate : SnapshotAddonsInRegistrationOrder(addon_manager_)) {
    for (const auto &load_with_target : candidate.load_with) {
      if (!AsciiCaseInsensitiveEquals(load_with_target, addon_state->name)) {
        continue;
      }
      (void)LoadInternal(candidate.name, context, active_chain);
      break;
    }
  }

  active_chain.erase(addon_state->name);
  return true;
}

}
