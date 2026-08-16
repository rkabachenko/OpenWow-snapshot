#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"

#include "openwow/core/file_stack_log_banner.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/world_session.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {
namespace {

enum class GuildRosterSortKey : std::uint8_t {
  kRank = 0,
  kLevel = 1,
  kName = 2,
  kZone = 3,
  kClass = 4,
  kGroup = 5,
  kOnline = 6,
  kNote = 7,
};

struct GuildRosterSortCriterion {
  GuildRosterSortKey key;
  bool descending;
};

class GuildRosterViewState {
 public:
  GuildRosterViewState()
      : sort_order_{{{GuildRosterSortKey::kRank, false},
                     {GuildRosterSortKey::kLevel, false},
                     {GuildRosterSortKey::kName, false},
                     {GuildRosterSortKey::kZone, false},
                     {GuildRosterSortKey::kClass, false},
                     {GuildRosterSortKey::kGroup, false},
                     {GuildRosterSortKey::kOnline, false},
                     {GuildRosterSortKey::kNote, false}}} {}

  void ApplySortKey(GuildRosterSortKey key) {
    const auto it = std::find_if(
        sort_order_.begin(), sort_order_.end(),
        [key](const GuildRosterSortCriterion& criterion) {
          return criterion.key == key;
        });
    if (it == sort_order_.end()) {
      return;
    }

    GuildRosterSortCriterion selected = *it;
    const auto index = static_cast<std::size_t>(
        std::distance(sort_order_.begin(), it));
    if (index == 0) {
      selected.descending = !selected.descending;
    } else {
      std::move_backward(sort_order_.begin(), it, it + 1);
    }
    sort_order_.front() = selected;
  }

  [[nodiscard]] const std::array<GuildRosterSortCriterion, 8>& sort_order()
      const {
    return sort_order_;
  }

  void SetSelection(std::uint64_t guid) { selected_guid_ = guid; }
  [[nodiscard]] std::uint64_t selection() const { return selected_guid_; }

 private:
  std::array<GuildRosterSortCriterion, 8> sort_order_;
  std::uint64_t selected_guid_ = 0;
};

struct GuildRosterExportState {
  bool pending = false;
};

GuildRosterViewState& GetGuildRosterViewState() {
  static GuildRosterViewState state;
  return state;
}

GuildRosterExportState& GetGuildRosterExportState() {
  static GuildRosterExportState state;
  return state;
}

bool ParseGuildRosterShowOfflineValue(std::string_view value) {
  const std::string storage(value);
  return ScriptParseBoolStringOrDefault(storage.c_str(), false);
}

bool ActiveSessionHasCachedGuildRosterMembers() {
  const auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  const auto* const session = manager != nullptr ? manager->world_session() : nullptr;
  return session != nullptr && session->guild().has_roster() &&
         !session->guild().roster().members.empty();
}

bool GuildRosterShowOfflineValidationCallback(const std::string&,
                                              const std::string& old_value,
                                              const std::string& new_value) {
  if (ParseGuildRosterShowOfflineValue(old_value) ==
      ParseGuildRosterShowOfflineValue(new_value)) {
    return true;
  }

  if (ActiveSessionHasCachedGuildRosterMembers()) {
    ScriptEventDispatch::Get().FireGuildRosterUpdate();
  }
  return true;
}

void RegisterGuildRosterShowOfflineValidationCallback(CVarSystem& cvars) {
  cvars.SetValidationCallback("guildShowOffline",
                              GuildRosterShowOfflineValidationCallback);
}

void EnsureGuildRosterShowOfflineState() {
  auto& cvars = CVarSystem::Instance();
  EnsureGuildRosterShowOfflineCVarBehavior(cvars);
}

std::optional<std::string> TryLookupAreaName(
    const openwow::data::dbc::DbcLoader* dbc, std::uint32_t area_id) {
  if (area_id == 0) {
    return std::nullopt;
  }
  if (dbc == nullptr) {
    return std::nullopt;
  }
  const auto* entry = dbc->area_table().LookupEntry(area_id);
  if (entry == nullptr || entry->name.empty()) {
    return std::nullopt;
  }
  return std::string(entry->name);
}

std::optional<std::string_view> TryGetClassDisplayName(
    const openwow::data::dbc::DbcLoader* dbc, const std::uint8_t class_id) {
  if (dbc == nullptr) {
    return std::nullopt;
  }

  const auto* entry = dbc->chr_classes().LookupEntry(class_id);
  if (entry == nullptr || entry->name.empty()) {
    return std::nullopt;
  }

  return entry->name;
}

int CompareGuildRosterMembers(const openwow::data::dbc::DbcLoader* dbc,
                              const openwow::game::GuildMember& left,
                              const openwow::game::GuildMember& right,
                              bool show_offline) {
  const bool left_online = left.status != 0;
  const bool right_online = right.status != 0;
  if (!show_offline && left_online != right_online) {
    return left_online ? -1 : 1;
  }

  for (const auto& criterion : GetGuildRosterViewState().sort_order()) {
    int result = 0;
    switch (criterion.key) {
      case GuildRosterSortKey::kRank:
        if (left.rank_id != right.rank_id) {
          result = right.rank_id < left.rank_id ? -1 : 1;
        }
        break;
      case GuildRosterSortKey::kLevel:
        if (left.level != right.level) {
          result = right.level < left.level ? -1 : 1;
        }
        break;
      case GuildRosterSortKey::kName:
        result = openwow::core::SStrCmpUTF8NoCase(left.name.c_str(),
                                                  right.name.c_str(),
                                                  0x7FFFFFFF);
        break;
      case GuildRosterSortKey::kZone: {
        const auto left_zone =
            TryLookupAreaName(dbc, static_cast<std::uint32_t>(left.area_id));
        const auto right_zone =
            TryLookupAreaName(dbc, static_cast<std::uint32_t>(right.area_id));
        if (left_zone && right_zone) {
          result = openwow::core::SStrCmpNoCaseCollate(left_zone->c_str(),
                                                       right_zone->c_str(),
                                                       0x7FFFFFFF);
        }
        break;
      }
      case GuildRosterSortKey::kClass: {
        const auto left_class = TryGetClassDisplayName(dbc, left.class_id);
        const auto right_class = TryGetClassDisplayName(dbc, right.class_id);
        if (left_class && right_class) {
          result = openwow::core::SStrCmpNoCaseCollate(left_class->data(),
                                                       right_class->data(),
                                                       0x7FFFFFFF);
        }
        break;
      }
      case GuildRosterSortKey::kGroup:
        break;
      case GuildRosterSortKey::kOnline:
        if (left_online && right_online) {
          break;
        }
        if (left_online != right_online) {
          result = left_online ? -1 : 1;
          break;
        }
        if (left.last_save < right.last_save) {
          result = -1;
        } else if (left.last_save > right.last_save) {
          result = 1;
        }
        break;
      case GuildRosterSortKey::kNote:
        result = openwow::core::SStrCmpNoCaseCollate(left.note.c_str(),
                                                     right.note.c_str(),
                                                     0x7FFFFFFF);
        break;
    }
    if (result != 0) {
      return criterion.descending ? -result : result;
    }
  }

  return 0;
}

std::vector<const openwow::game::GuildMember*> BuildSortedGuildRoster(
    const openwow::game::WorldSession& session,
    const bool include_hidden_offline) {
  std::vector<const openwow::game::GuildMember*> result;
  if (!session.guild().has_roster()) {
    return result;
  }

  const auto& members = session.guild().roster().members;
  const bool show_offline = GetGuildRosterShowOfflineState();
  result.reserve(members.size());
  for (const auto& member : members) {
    if (!include_hidden_offline && !show_offline && member.status == 0) {
      continue;
    }
    result.push_back(&member);
  }

  const auto* dbc = session.GetDbcLoader();
  std::sort(result.begin(), result.end(),
            [dbc, show_offline](const openwow::game::GuildMember* left,
                                const openwow::game::GuildMember* right) {
              return CompareGuildRosterMembers(dbc, *left, *right,
                                               show_offline)
                  < 0;
            });
  return result;
}

std::vector<const openwow::game::GuildMember*> BuildDisplayedGuildRoster(
    const openwow::game::WorldSession& session) {
  return BuildSortedGuildRoster(session, false);
}

std::vector<const openwow::game::GuildMember*> BuildFullGuildRoster(
    const openwow::game::WorldSession& session) {
  return BuildSortedGuildRoster(session, true);
}

std::string LookupGuildRosterRankName(const openwow::game::WorldSession& session,
                                      const openwow::game::GuildMember& member) {
  if (!session.guild().has_guild_info()) {
    return {};
  }

  const auto& guild_info = session.guild().guild_info();
  if (member.rank_id < 0) {
    return {};
  }

  const auto rank_index = static_cast<std::uint32_t>(member.rank_id);
  if (rank_index >= guild_info.rank_count ||
      rank_index >= openwow::game::kGuildRanksMaxCount) {
    return {};
  }

  return guild_info.rank_names[rank_index];
}

std::string LookupGuildRosterClassName(
    const openwow::game::WorldSession& session,
    const openwow::game::GuildMember& member) {
  return std::string(TryGetClassDisplayName(session.GetDbcLoader(),
                                            member.class_id)
                         .value_or(std::string_view{}));
}

std::string LookupGuildRosterAreaName(const openwow::game::WorldSession& session,
                                      const openwow::game::GuildMember& member) {
  return TryLookupAreaName(session.GetDbcLoader(),
                           static_cast<std::uint32_t>(member.area_id))
      .value_or(std::string{});
}

std::int32_t EncodeGuildRosterOfflineValue(float last_save) {
  const double promoted = static_cast<double>(last_save);
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(promoted));
  std::memcpy(&bits, &promoted, sizeof(bits));
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
}

std::optional<std::filesystem::path> ResolveGuildRosterExportPath() {
  const std::string exe_dir = openwow::platform::OS_GetModuleDirectory();
  if (exe_dir.empty()) {
    return std::nullopt;
  }

  char resolved_path[1024]{};
  openwow::core::FileStackLogPathOptions options;
  options.when_utc = std::chrono::system_clock::now();
  options.default_root = exe_dir.c_str();
  options.can_resolve_path = [](const char* path) {
    return openwow::vfs::FileSystem_GetPathType(path) !=
           openwow::vfs::FileSystemPathType::kMissing;
  };
  options.create_directory = [](const char* path, const bool recursive) {
    return openwow::vfs::FileSystem_CreateDirectory(path, recursive);
  };

  if (!openwow::core::BuildFileStackLogPath("Logs\\GuildRoster.txt",
                                            resolved_path,
                                            sizeof(resolved_path), true,
                                            options)) {
    return std::nullopt;
  }

  return openwow::vfs::ToNativePath(resolved_path);
}

bool ExportGuildRosterToLog(const openwow::game::WorldSession& session) {
  if (!session.guild().has_roster() || session.objects().GetActivePlayer() == nullptr) {
    return false;
  }

  const auto path = ResolveGuildRosterExportPath();
  if (!path.has_value()) {
    return false;
  }

  std::ofstream output(*path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << BuildGuildRosterExportContents(session);
  return output.good();
}

std::optional<GuildRosterSortKey> TryParseGuildRosterSortType(
    std::string_view sort_type) {
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "rank")) {
    return GuildRosterSortKey::kRank;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "level")) {
    return GuildRosterSortKey::kLevel;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "name")) {
    return GuildRosterSortKey::kName;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "zone")) {
    return GuildRosterSortKey::kZone;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "class")) {
    return GuildRosterSortKey::kClass;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "group")) {
    return GuildRosterSortKey::kGroup;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "online")) {
    return GuildRosterSortKey::kOnline;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(sort_type, "note")) {
    return GuildRosterSortKey::kNote;
  }
  return std::nullopt;
}

}

const openwow::game::GuildMember* GetGuildRosterMemberByDisplayIndex(
    lua_State* L, int one_based_index) {
  const int display_index = one_based_index - 1;
  if (display_index < 0) {
    return nullptr;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return nullptr;
  }

  const auto sorted_roster = BuildFullGuildRoster(*session);
  if (display_index >= static_cast<int>(sorted_roster.size())) {
    return nullptr;
  }
  return sorted_roster[static_cast<std::size_t>(display_index)];
}

bool GuildRosterDisplayContainsGuid(const openwow::game::WorldSession& session,
                                    const std::uint64_t raw_guid) {
  if (raw_guid == 0) {
    return false;
  }

  const auto displayed_roster = BuildFullGuildRoster(session);
  return std::any_of(displayed_roster.begin(), displayed_roster.end(),
                     [raw_guid](const openwow::game::GuildMember* member) {
                       return member != nullptr &&
                              member->guid.GetRawValue() == raw_guid;
                     });
}

int GetGuildRosterVisibleMemberCount(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr || !session->guild().has_roster()) {
    return 0;
  }

  return static_cast<int>(BuildDisplayedGuildRoster(*session).size());
}

int GetGuildRosterTotalMemberCount(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr || !session->guild().has_roster()) {
    return 0;
  }
  return static_cast<int>(session->guild().roster().members.size());
}

bool GetGuildRosterShowOfflineState() {
  EnsureGuildRosterShowOfflineState();
  return ParseGuildRosterShowOfflineValue(
      CVarSystem::Instance().GetCVar("guildShowOffline"));
}

void EnsureGuildRosterShowOfflineCVarBehavior(CVarSystem& cvars) {
  if (!cvars.Exists("guildShowOffline")) {
    cvars.RegisterCVar("guildShowOffline", "1", CVarFlags::Account,
                       "Show offline guild members in the guild UI");
  }
  RegisterGuildRosterShowOfflineValidationCallback(cvars);
}

void SetGuildRosterShowOfflineState(bool show) {
  auto& cvars = CVarSystem::Instance();
  EnsureGuildRosterShowOfflineCVarBehavior(cvars);
  (void)cvars.SetCVar("guildShowOffline", show ? "1" : "0");
}

void SetGuildRosterShowOfflineState(lua_State* , bool show) {
  SetGuildRosterShowOfflineState(show);
}

void ApplyGuildRosterSort(lua_State* , std::string_view sort_type) {
  const auto parsed = TryParseGuildRosterSortType(sort_type);
  GetGuildRosterViewState().ApplySortKey(
      parsed.value_or(GuildRosterSortKey::kName));
  ScriptEventDispatch::Get().FireGuildRosterUpdate();
}

void SetGuildRosterSelectionByDisplayIndex(lua_State* L, int one_based_index) {
  const auto* member = GetGuildRosterMemberByDisplayIndex(L, one_based_index);
  GetGuildRosterViewState().SetSelection(
      member != nullptr ? member->guid.GetRawValue() : 0);
}

int GetGuildRosterSelectionDisplayIndex(lua_State* L) {
  const auto selected_guid = GetGuildRosterViewState().selection();
  if (selected_guid == 0) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto sorted_roster = BuildFullGuildRoster(*session);
  for (std::size_t i = 0; i < sorted_roster.size(); ++i) {
    if (sorted_roster[i] != nullptr &&
        sorted_roster[i]->guid.GetRawValue() == selected_guid) {
      return static_cast<int>(i) + 1;
    }
  }
  return 0;
}

std::string BuildGuildRosterExportContents(
    const openwow::game::WorldSession& session) {
  std::ostringstream output;
  const auto sorted_roster = BuildDisplayedGuildRoster(session);
  for (const auto* member : sorted_roster) {
    if (member == nullptr) {
      continue;
    }

    output << member->name << '\t' << static_cast<int>(member->level) << '\t'
           << LookupGuildRosterClassName(session, *member) << '\t'
           << LookupGuildRosterAreaName(session, *member) << '\t'
           << LookupGuildRosterRankName(session, *member) << '\t'
           << member->note << '\t' << member->officer_note << '\t'
           << EncodeGuildRosterOfflineValue(member->last_save) << "\r\n";
  }
  return output.str();
}

void RequestGuildRosterExport(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return;
  }

  if (session->guild().has_roster()) {
    (void)ExportGuildRosterToLog(*session);
    return;
  }

  GetGuildRosterExportState().pending = true;
  session->interaction().SendGuildRoster();
}

void TryCompletePendingGuildRosterExport(
    const openwow::game::WorldSession& session) {
  auto& export_state = GetGuildRosterExportState();
  if (!export_state.pending || !session.guild().has_roster()) {
    return;
  }

  export_state.pending = false;
  (void)ExportGuildRosterToLog(session);
}

void ResetGuildRosterViewState() {
  GetGuildRosterViewState() = GuildRosterViewState{};
  GetGuildRosterExportState() = GuildRosterExportState{};
}

}
