
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
struct FactionEntry;
}

namespace openwow::game {

class ObjectManager;

inline constexpr std::uint32_t kMaxRepSlots     = 128;
inline constexpr std::uint32_t kMaxFactionHdrs  = 64;
inline constexpr std::uint32_t kMaxFactionEntry = 193;

inline constexpr std::uint8_t kRepFlagVisible   = 0x01;
inline constexpr std::uint8_t kRepFlagAtWar     = 0x02;
inline constexpr std::uint8_t kRepFlagHidden    = 0x04;
inline constexpr std::uint8_t kRepFlagHdrVis    = 0x08;
inline constexpr std::uint8_t kRepFlagForced    = 0x10;
inline constexpr std::uint8_t kRepFlagInactive  = 0x20;
inline constexpr std::uint8_t kRepFlagBarAlways = 0x40;

inline constexpr std::array<std::int32_t, 8> kStandingMin = {
    -42000, -6000, -3000, 0, 3000, 9000, 21000, 42000,
};
inline constexpr std::array<std::int32_t, 8> kStandingSize = {
    36000, 3000, 3000, 3000, 6000, 12000, 21000, 999,
};

struct FactionHeader {
  std::int32_t faction_id{0};
  std::int32_t child_count{0};
  std::int32_t parent_faction{0};
  std::uint8_t collapsed_hidden{0};
};

struct FactionEntryInfo {
  std::int32_t  rep_list_id{0};
  std::int32_t  faction_id{0};
  std::int32_t  is_header{0};
  std::int32_t  header_idx{-1};
  std::int32_t  parent_faction{0};
};

struct ForcedReactionEntry {
  std::uint32_t faction_id{0};
  std::uint32_t standing{0};
};

class ReputationInfo {
 public:
  static ReputationInfo& Get();

  void Init();
  void Cleanup();

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);
  void SetPlayerIdentity(std::uint8_t race_id, std::uint8_t class_id,
                         std::uint8_t gender_id);
  void ClearPlayerIdentity();

  bool HandleInitializeFactions(const ObjectManager& objects,
                                const std::uint8_t* data, std::size_t len);

  bool HandleSetFactionAtWar(const std::uint8_t* data, std::size_t len);

  bool HandleSetFactionVisible(const ObjectManager& objects,
                               const std::uint8_t* data, std::size_t len);

  bool HandleSetFactionStanding(const ObjectManager& objects,
                                const std::uint8_t* data, std::size_t len);

  bool HandleSetForcedReactions(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] int FindHeaderIndex(std::int32_t faction_id) const;
  std::size_t AddHeader(std::int32_t faction_id, std::int32_t parent_faction);
  [[nodiscard]] bool IsChildOfHeader(std::size_t hdr_idx,
                                      std::int32_t ancestor_id) const;

  bool AddFactionEntry(const ObjectManager& objects, std::uint32_t rep_slot,
                       bool show_msg);
  std::size_t EnsureHeaderExists(std::int32_t faction_id);

  static int SortFactionEntries(const FactionEntryInfo* a,
                                const FactionEntryInfo* b,
                                const ReputationInfo& self);
  static int SortHeaders(const FactionHeader* a, const FactionHeader* b,
                          const ReputationInfo& self);

  void RebuildFactionList();

  void ToggleHeaderCollapse(std::size_t entry_idx, bool collapse);

  [[nodiscard]] bool IsAtWar(std::int32_t faction_id) const;
  [[nodiscard]] bool IsForced(std::int32_t faction_id) const;
  [[nodiscard]] bool IsInactive(std::size_t entry_idx) const;
  [[nodiscard]] std::optional<std::uint32_t> FindForcedReactionStanding(
      std::uint32_t faction_id) const;
  [[nodiscard]] bool HasReputationList(std::int32_t faction_id) const;

  [[nodiscard]] std::int32_t GetCurrentStanding(std::int32_t faction_id) const;
  [[nodiscard]] int GetStandingLevel(std::int32_t faction_id) const;

  [[nodiscard]] bool IsChildFaction(std::int32_t faction_id) const;
  [[nodiscard]] bool IsEmptyHeader(const FactionHeader& hdr) const;
  [[nodiscard]] bool IsPlayerFriendly(std::int32_t faction_id) const;

  void SyncWatchedFactionSlot(std::optional<std::uint32_t> watched_slot);
  [[nodiscard]] bool IsWatchedFaction(std::int32_t faction_id) const;
  [[nodiscard]] std::int32_t GetWatchedFactionId() const;
  [[nodiscard]] int GetSelectedFactionIndex() const;

  void SendToggleAtWar(std::int32_t faction_id,
                       bool set_at_war,
                       bool active_player_in_combat = false);
  void SendSetWatchedFaction(std::int32_t faction_id);
  void SendSetInactive(std::int32_t faction_id, bool inactive);

  void InitFromPlayerData(const ObjectManager& objects);
  [[nodiscard]] const std::vector<std::uint32_t> *
  FindChildSpilloverFactionIds(std::int32_t parent_faction_id) const;

  struct FactionLuaInfo {
    std::optional<std::string> name;
    std::optional<std::string> description;
    int   standing_id{0};
    int   bar_min{0};
    int   bar_max{0};
    int   bar_value{0};
    std::optional<bool> at_war;
    std::optional<bool> can_toggle_at_war;
    std::optional<bool> is_header;
    std::optional<bool> is_collapsed;
    std::optional<bool> is_player_friendly;
    std::optional<bool> is_watched;
    std::optional<bool> is_child;
  };

  [[nodiscard]] FactionLuaInfo PushFactionInfo(std::int32_t faction_id) const;

  [[nodiscard]] std::size_t num_entries() const { return num_entries_; }
  [[nodiscard]] std::size_t num_visible() const { return num_visible_; }
  [[nodiscard]] std::size_t num_headers() const { return num_headers_; }
  [[nodiscard]] const std::vector<ForcedReactionEntry>& forced_reactions() const {
    return forced_reactions_;
  }

  [[nodiscard]] const FactionEntryInfo& GetEntry(std::size_t idx) const {
    return entries_[idx];
  }
  [[nodiscard]] std::int32_t GetFactionIdByIndex(std::size_t idx) const {
    return (idx < num_entries_) ? entries_[idx].faction_id : 0;
  }
  [[nodiscard]] const FactionHeader& GetHeader(std::size_t idx) const {
    return headers_[idx];
  }

  [[nodiscard]] std::uint8_t GetSlotFlags(std::uint32_t slot) const {
    return (slot < kMaxRepSlots) ? rep_flags_[slot] : 0;
  }
  [[nodiscard]] std::int32_t GetSlotStanding(std::uint32_t slot) const {
    return (slot < kMaxRepSlots) ? rep_standing_[slot] : 0;
  }
  [[nodiscard]] std::int32_t GetSlotBaseValue(std::uint32_t slot) const {
    return (slot < kMaxRepSlots) ? rep_base_values_[slot] : 0;
  }
  [[nodiscard]] std::uint32_t GetSlotFactionId(std::uint32_t slot) const {
    return (slot < kMaxRepSlots) ? rep_faction_ids_[slot] : 0;
  }

  void SetSelectedFaction(std::int32_t id) { selected_faction_id_ = id; }

  using UpdateCallback = std::function<void()>;
  void SetOnUpdate(UpdateCallback cb) { on_update_ = std::move(cb); }

  using FactionStateRefreshCallback = std::function<void()>;
  void SetFactionStateRefreshCallback(FactionStateRefreshCallback cb) {
    on_faction_state_refresh_ = std::move(cb);
  }
  [[nodiscard]] bool did_last_standing_change_faction_state() const {
    return last_standing_faction_state_changed_;
  }

 private:
  [[nodiscard]] const openwow::data::dbc::FactionEntry* LookupFactionEntry(
      std::int32_t faction_id) const;

  [[nodiscard]] std::int32_t LookupRepListId(std::int32_t faction_id) const;

  [[nodiscard]] std::int32_t LookupParentFaction(std::int32_t faction_id) const;

  [[nodiscard]] const char* LookupFactionName(std::int32_t faction_id) const;
  [[nodiscard]] const char* LookupFactionDescription(std::int32_t faction_id) const;
  void RebuildChildSpilloverFactionIndex();
  void DisplayRepChangeMessage(const ObjectManager& objects,
                               std::int32_t faction_id,
                               std::int32_t amount,
                               bool is_generic,
                               float bonus_rep) const;
  void DisplayStandingChangeMessage(const ObjectManager& objects,
                                    std::int32_t faction_id,
                                    int standing_level) const;

  [[nodiscard]] bool IsHeaderCollapsed(std::size_t header_index) const;
  [[nodiscard]] bool IsHeaderHiddenInList(const FactionHeader& header) const;
  [[nodiscard]] bool IsEntryHiddenForSort(const FactionEntryInfo& entry) const;
  [[nodiscard]] int CompareEntryFactionNames(std::int32_t lhs_faction_id,
                                             std::int32_t rhs_faction_id) const;
  [[nodiscard]] int CompareHeaderFactionNames(std::int32_t lhs_faction_id,
                                              std::int32_t rhs_faction_id) const;

  void FireUpdate();

  std::array<std::uint8_t,  kMaxRepSlots> rep_flags_{};
  std::array<std::int32_t,  kMaxRepSlots> rep_standing_{};
  std::array<std::int32_t,  kMaxRepSlots> rep_base_values_{};
  std::array<std::uint32_t, kMaxRepSlots> rep_faction_ids_{};

  std::array<FactionHeader, kMaxFactionHdrs> headers_{};
  std::size_t num_headers_{0};

  std::array<FactionEntryInfo, kMaxFactionEntry> entries_{};
  std::size_t num_entries_{0};
  std::size_t num_visible_{0};

  std::uint64_t collapsed_mask_{~std::uint64_t{0}};

  std::int32_t selected_faction_id_{0};
  std::int32_t watched_faction_slot_{-1};
  bool has_active_player_context_{false};
  std::optional<std::uint8_t> player_gender_id_;

  std::vector<ForcedReactionEntry> forced_reactions_;
  std::unordered_map<std::int32_t, std::vector<std::uint32_t>>
      child_spillover_factions_;

  UpdateCallback on_update_;
  FactionStateRefreshCallback on_faction_state_refresh_;
  bool last_standing_faction_state_changed_{false};
  const openwow::data::dbc::DbcLoader* dbc_{nullptr};
  std::optional<std::uint8_t> player_race_id_;
  std::optional<std::uint8_t> player_class_id_;
};

}
