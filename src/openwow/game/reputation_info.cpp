
#include "openwow/game/reputation_info.h"

#include <cstdlib>
#include <cstring>

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/localization.h"
#include "openwow/game/world_session.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::game {
namespace {

openwow::net::wotlk::WorldPacket BuildSetFactionAtWarPacket(
    std::uint32_t rep_list_id,
    bool set_at_war) {
  openwow::net::wotlk::WorldPacket pkt(
      openwow::net::wotlk::Opcode::CMSG_SET_FACTION_ATWAR);
  pkt.AppendU32(rep_list_id);
  pkt.AppendU8(set_at_war ? 1u : 0u);
  return pkt;
}

openwow::net::wotlk::WorldPacket BuildSetWatchedFactionPacket(
    std::int32_t rep_list_id) {
  openwow::net::wotlk::WorldPacket pkt(
      openwow::net::wotlk::Opcode::CMSG_SET_WATCHED_FACTION);
  pkt.AppendU32(static_cast<std::uint32_t>(rep_list_id));
  return pkt;
}

openwow::net::wotlk::WorldPacket BuildSetFactionInactivePacket(
    std::uint32_t rep_list_id,
    bool inactive) {
  openwow::net::wotlk::WorldPacket pkt(
      openwow::net::wotlk::Opcode::CMSG_SET_FACTION_INACTIVE);
  pkt.AppendU32(rep_list_id);
  pkt.AppendU8(inactive ? 1u : 0u);
  return pkt;
}

constexpr std::size_t kReputationMessageBufferSize = 3000;

std::string GetLocalizedGlobalString(const char* key) {
  return Localization::Get().GetString(key != nullptr ? key : "");
}

void FireUpdateFactionEvent() {
  ui::game::ScriptEventDispatch::Get().FireGlobalEvent(
      ui::game::events::UPDATE_FACTION);
}

thread_local const ReputationInfo* g_reputation_sort_context = nullptr;

class ScopedReputationSortContext {
 public:
  explicit ScopedReputationSortContext(const ReputationInfo& info)
      : previous_(g_reputation_sort_context) {
    g_reputation_sort_context = &info;
  }

  ~ScopedReputationSortContext() {
    g_reputation_sort_context = previous_;
  }

  ScopedReputationSortContext(const ScopedReputationSortContext&) = delete;
  ScopedReputationSortContext& operator=(const ScopedReputationSortContext&) = delete;

 private:
  const ReputationInfo* previous_;
};

int CompareFactionHeadersForQSort(const void* lhs, const void* rhs) {
  const auto* context = g_reputation_sort_context;
  if (context == nullptr) {
    return 0;
  }

  return ReputationInfo::SortHeaders(
      static_cast<const FactionHeader*>(lhs),
      static_cast<const FactionHeader*>(rhs),
      *context);
}

int CompareFactionEntriesForQSort(const void* lhs, const void* rhs) {
  const auto* context = g_reputation_sort_context;
  if (context == nullptr) {
    return 0;
  }

  return ReputationInfo::SortFactionEntries(
      static_cast<const FactionEntryInfo*>(lhs),
      static_cast<const FactionEntryInfo*>(rhs),
      *context);
}

}

ReputationInfo& ReputationInfo::Get() {
  static ReputationInfo instance;
  return instance;
}

void ReputationInfo::Init() {
  rep_flags_.fill(0);
  rep_standing_.fill(0);
  rep_base_values_.fill(0);
  rep_faction_ids_.fill(0);
  num_entries_ = 0;
  num_visible_ = 0;
  num_headers_ = 0;
  selected_faction_id_ = 0;
  watched_faction_slot_ = -1;
  has_active_player_context_ = false;
  collapsed_mask_ = ~std::uint64_t{0};
  headers_ = {};
  entries_ = {};
  forced_reactions_.clear();
  child_spillover_factions_.clear();
  last_standing_faction_state_changed_ = false;
  ClearPlayerIdentity();
}

void ReputationInfo::Cleanup() {
  num_entries_ = 0;
  num_visible_ = 0;
  num_headers_ = 0;
  selected_faction_id_ = 0;
  watched_faction_slot_ = -1;
  has_active_player_context_ = false;
  collapsed_mask_ = ~std::uint64_t{0};
  headers_ = {};
  entries_ = {};
  rep_faction_ids_.fill(0);
  forced_reactions_.clear();
  child_spillover_factions_.clear();
  last_standing_faction_state_changed_ = false;
  ClearPlayerIdentity();
}

void ReputationInfo::BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
  if (dbc_ == dbc) {
    return;
  }
  dbc_ = dbc;
  child_spillover_factions_.clear();
}

void ReputationInfo::SetPlayerIdentity(std::uint8_t race_id,
                                       std::uint8_t class_id,
                                       std::uint8_t gender_id) {
  player_race_id_ = race_id;
  player_class_id_ = class_id;
  player_gender_id_ = gender_id;
}

void ReputationInfo::ClearPlayerIdentity() {
  player_race_id_.reset();
  player_class_id_.reset();
  player_gender_id_.reset();
}

int ReputationInfo::FindHeaderIndex(std::int32_t faction_id) const {
  if (num_headers_ == 0) return -1;
  for (std::size_t i = 0; i < num_headers_; ++i) {
    if (headers_[i].faction_id == faction_id) return static_cast<int>(i);
  }
  return -1;
}

std::size_t ReputationInfo::AddHeader(std::int32_t faction_id,
                                       std::int32_t parent_faction) {
  for (std::size_t i = 0; i < num_headers_; ++i) {
    if (headers_[i].faction_id == faction_id) {
      collapsed_mask_ = ~std::uint64_t{0};
      return i;
    }
  }
  if (num_headers_ >= kMaxFactionHdrs) return num_headers_ - 1;

  auto& h = headers_[num_headers_];
  h.faction_id = faction_id;
  h.collapsed_hidden = 0;
  h.parent_faction = parent_faction;
  h.child_count = 0;
  std::size_t idx = num_headers_++;
  collapsed_mask_ = ~std::uint64_t{0};
  return idx;
}

bool ReputationInfo::IsChildOfHeader(std::size_t hdr_idx,
                                      std::int32_t ancestor_id) const {
  if (ancestor_id == -1 || ancestor_id == 0) return false;

  while (true) {
    const std::int32_t parent = headers_[hdr_idx].parent_faction;
    if (parent == ancestor_id) return true;
    if (parent == 0 || parent == -1) return false;

    const int pidx = FindHeaderIndex(parent);
    if (pidx < 0) return false;
    hdr_idx = static_cast<std::size_t>(pidx);
  }
}

int ReputationInfo::GetSelectedFactionIndex() const {
  if (num_visible_ == 0) return -1;
  for (std::size_t i = 0; i < num_visible_; ++i) {
    if (entries_[i].faction_id == selected_faction_id_)
      return static_cast<int>(i);
  }
  return -1;
}

std::size_t ReputationInfo::EnsureHeaderExists(std::int32_t faction_id) {
  std::int32_t parent = 0;
  if (const auto* faction = LookupFactionEntry(faction_id); faction != nullptr) {
    parent = static_cast<std::int32_t>(faction->parent_faction_id);
    if (parent != 0) {
      EnsureHeaderExists(parent);
    }
  }

  for (std::size_t i = 0; i < num_entries_; ++i) {
    if (entries_[i].is_header != 0 && entries_[i].faction_id == faction_id) {
      return i;
    }
  }

  if (num_entries_ >= kMaxFactionEntry) return num_entries_ - 1;

  auto& entry = entries_[num_entries_];
  entry.rep_list_id = LookupRepListId(faction_id);
  entry.faction_id = faction_id;
  entry.is_header = 1;
  entry.header_idx = -1;
  entry.parent_faction = faction_id;
  ++num_entries_;

  return AddHeader(faction_id, parent);
}

bool ReputationInfo::IsAtWar(std::int32_t faction_id) const {
  auto slot = LookupRepListId(faction_id);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots)
    return false;
  return (rep_flags_[slot] & kRepFlagAtWar) != 0;
}

bool ReputationInfo::IsForced(std::int32_t faction_id) const {
  auto slot = LookupRepListId(faction_id);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots)
    return false;
  return (rep_flags_[slot] & kRepFlagForced) != 0;
}

std::optional<std::uint32_t> ReputationInfo::FindForcedReactionStanding(
    std::uint32_t faction_id) const {
  for (const auto& reaction : forced_reactions_) {
    if (reaction.faction_id == faction_id) {
      return reaction.standing;
    }
  }
  return std::nullopt;
}

bool ReputationInfo::HasReputationList(const std::int32_t faction_id) const {
  const auto* entry = LookupFactionEntry(faction_id);
  return entry != nullptr && entry->reputation_list_id >= 0;
}

bool ReputationInfo::IsInactive(std::size_t entry_idx) const {
  std::int32_t fid = 0;
  if (entry_idx < num_entries_)
    fid = entries_[entry_idx].faction_id;
  auto slot = LookupRepListId(fid);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots)
    return false;
  return (rep_flags_[slot] & kRepFlagInactive) != 0;
}

std::int32_t ReputationInfo::GetCurrentStanding(
    std::int32_t faction_id) const {
  auto slot = LookupRepListId(faction_id);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots) return 0;
  return rep_base_values_[slot] + rep_standing_[slot];
}

int ReputationInfo::GetStandingLevel(std::int32_t faction_id) const {
  auto slot = LookupRepListId(faction_id);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots) return 3;

  std::int32_t rep = rep_base_values_[slot] + rep_standing_[slot];

  if (rep >= 42000) return 7;
  if (rep >= 21000) return 6;
  if (rep >=  9000) return 5;
  if (rep >=  3000) return 4;
  if (rep >=     0) return 3;
  if (rep >= -3000) return 2;
  if (rep >= -6000) return 1;
  return 0;
}

bool ReputationInfo::IsChildFaction(std::int32_t faction_id) const {
  for (std::size_t i = 0; i < num_visible_; ++i) {
    if (entries_[i].faction_id == faction_id && entries_[i].header_idx >= 0) {
      auto hidx = static_cast<std::size_t>(entries_[i].header_idx);
      return headers_[hidx].parent_faction != 0;
    }
  }
  return false;
}

bool ReputationInfo::IsEmptyHeader(const FactionHeader& hdr) const {
  if (!IsPlayerFriendly(hdr.faction_id)) {
    return hdr.child_count == 0;
  }
  return false;
}

bool ReputationInfo::IsPlayerFriendly(std::int32_t faction_id) const {
  if (!player_race_id_.has_value() || !player_class_id_.has_value()) {
    return false;
  }

  const auto* entry = LookupFactionEntry(faction_id);
  if (entry == nullptr || entry->reputation_list_id < 0) {
    return false;
  }

  const auto race_bit = std::uint32_t{1} << (*player_race_id_ - 1);
  const auto class_bit = std::uint32_t{1} << (*player_class_id_ - 1);

  for (std::size_t index = 0; index < entry->base_rep_race_mask.size(); ++index) {
    const auto race_mask = entry->base_rep_race_mask[index];
    const auto class_mask = entry->base_rep_class_mask[index];
    if (race_mask != 0) {
      if ((race_mask & race_bit) == 0) {
        continue;
      }
      if (class_mask == 0) {
        return entry->base_rep_value[index] < 0;
      }
    } else if (class_mask == 0) {
      continue;
    }

    if ((class_mask & class_bit) != 0) {
      return entry->base_rep_value[index] < 0;
    }
  }

  return false;
}

void ReputationInfo::SyncWatchedFactionSlot(
    std::optional<std::uint32_t> watched_slot) {
  has_active_player_context_ = watched_slot.has_value();
  if (!watched_slot.has_value() || *watched_slot >= kMaxRepSlots) {
    watched_faction_slot_ = -1;
    return;
  }

  watched_faction_slot_ = static_cast<std::int32_t>(*watched_slot);
}

bool ReputationInfo::IsWatchedFaction(std::int32_t faction_id) const {
  auto slot = LookupRepListId(faction_id);
  return slot >= 0 && slot == watched_faction_slot_;
}

std::int32_t ReputationInfo::GetWatchedFactionId() const {
  if (watched_faction_slot_ < 0) {
    return 0;
  }

  const auto slot = static_cast<std::uint32_t>(watched_faction_slot_);
  return (slot < kMaxRepSlots) ? static_cast<std::int32_t>(rep_faction_ids_[slot]) : 0;
}

bool ReputationInfo::AddFactionEntry(const ObjectManager& objects,
                                     std::uint32_t rep_slot,
                                     bool show_msg) {
  if (rep_slot >= kMaxRepSlots) return false;

  for (std::size_t i = 0; i < num_entries_; ++i) {
    if (static_cast<std::uint32_t>(entries_[i].rep_list_id) == rep_slot)
      return false;
  }
  if (num_entries_ >= kMaxFactionEntry) return false;

  const auto entry_index = num_entries_++;
  auto& e = entries_[entry_index];
  e.rep_list_id = static_cast<std::int32_t>(rep_slot);
  e.faction_id = (rep_slot < kMaxRepSlots) ?
      static_cast<std::int32_t>(rep_faction_ids_[rep_slot]) : 0;
  e.is_header = (rep_flags_[rep_slot] & kRepFlagHdrVis) ? 1 : 0;
  e.header_idx = -1;

  std::int32_t parent = LookupParentFaction(e.faction_id);
  if (e.is_header) {
    AddHeader(e.faction_id, parent);
    e.parent_faction = e.faction_id;
  } else {
    e.parent_faction = parent;
  }

  if (!e.is_header || parent >= 0) {
    EnsureHeaderExists(parent);
  }

  if (show_msg) {
    DisplayStandingChangeMessage(objects, e.faction_id,
                                 GetStandingLevel(e.faction_id));
  }

  return true;
}

bool ReputationInfo::HandleInitializeFactions(const ObjectManager& objects,
                                                const std::uint8_t* data,
                                                std::size_t len) {
  if (!data || len < 4) return false;
  PacketReader r(data, len);

  std::uint32_t count;
  if (!r.ReadU32(count)) return false;
  if (count > kMaxRepSlots) count = kMaxRepSlots;
  if (r.Remaining() < count * 5) return false;

  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint8_t flags;
    std::int32_t standing;
    if (!r.ReadU8(flags)) return false;
    if (!r.ReadI32(standing)) return false;
    rep_flags_[i] = flags;
    rep_standing_[i] = standing;
  }

  InitFromPlayerData(objects);
  FireUpdateFactionEvent();
  FireUpdate();
  return true;
}

bool ReputationInfo::HandleSetFactionAtWar(const std::uint8_t* data,
                                           std::size_t len) {
  if (!data || len < 5) return false;
  PacketReader r(data, len);

  std::uint32_t slot;
  std::uint8_t  flags;
  if (!r.ReadU32(slot)) return false;
  if (!r.ReadU8(flags)) return false;
  if (slot >= kMaxRepSlots) return false;

  if ((flags & kRepFlagAtWar) != 0) {
    rep_flags_[slot] |= kRepFlagAtWar;
  } else {
    rep_flags_[slot] &= static_cast<std::uint8_t>(~kRepFlagAtWar);
  }

  FireUpdate();
  return true;
}

bool ReputationInfo::HandleSetFactionVisible(const ObjectManager& objects,
                                               const std::uint8_t* data,
                                               std::size_t len) {
  if (!data || len < 4) return false;
  PacketReader r(data, len);

  std::uint32_t slot;
  if (!r.ReadU32(slot)) return false;
  if (slot >= kMaxRepSlots) return false;

  if ((rep_flags_[slot] & kRepFlagVisible) == 0) {
    AddFactionEntry(objects, slot, true);
    rep_flags_[slot] |= kRepFlagVisible;
    RebuildFactionList();
  }
  return true;
}

bool ReputationInfo::HandleSetFactionStanding(const ObjectManager& objects,
                                                const std::uint8_t* data,
                                                std::size_t len) {
  if (!data || len < 9) return false;
  PacketReader r(data, len);

  float bonus_rep;
  if (!r.ReadFloat(bonus_rep)) return false;

  std::uint8_t increased;
  if (!r.ReadU8(increased)) return false;

  std::uint32_t count;
  if (!r.ReadU32(count)) return false;
  if (count > kMaxRepSlots) return false;
  if (r.Remaining() < count * 8) return false;

  bool flags_changed = false;

  bool standing_changed = false;
  bool faction_state_changed = false;
  last_standing_faction_state_changed_ = false;

  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t slot;
    std::int32_t  new_standing;
    if (!r.ReadU32(slot)) return false;
    if (!r.ReadI32(new_standing)) return false;
    if (slot >= kMaxRepSlots) continue;

    const std::uint8_t old_flags = rep_flags_[slot];
    const auto faction_id = static_cast<std::int32_t>(rep_faction_ids_[slot]);
    const int old_level = GetStandingLevel(faction_id);
    bool entry_added = false;
    int rep_message_mode = 0;

    if ((old_flags & kRepFlagBarAlways) != 0) {
      rep_message_mode = ((old_flags & kRepFlagHidden) != 0) ? 2 : 1;
    } else if (i == 0) {
      rep_message_mode = ((old_flags & kRepFlagHidden) == 0) ? 1 : 0;
    }

    if (new_standing != rep_standing_[slot]) {
      const auto delta = new_standing - rep_standing_[slot];
      rep_standing_[slot] = new_standing;
      if (rep_message_mode != 0) {
        DisplayRepChangeMessage(objects, faction_id, delta,
                                rep_message_mode == 2, bonus_rep);
      }
    }

    const int new_level = GetStandingLevel(faction_id);

    std::uint8_t flags = old_flags;
    if ((flags & (kRepFlagVisible | kRepFlagHidden)) == 0) {
      flags |= kRepFlagVisible;
      entry_added = AddFactionEntry(objects, slot, true);
    }

    if (new_level < 2) {
      flags |= kRepFlagAtWar;
    } else if (new_level > old_level) {
      flags &= static_cast<std::uint8_t>(~kRepFlagAtWar);
    }

    if (flags != old_flags) {
      rep_flags_[slot] = flags;
      flags_changed = true;
      if (((flags ^ old_flags) & kRepFlagAtWar) != 0) {
        faction_state_changed = true;
      }
    }

    if (new_level != old_level) {
      if ((flags & kRepFlagHidden) == 0 &&
          (((flags & kRepFlagHdrVis) == 0) || (flags & 0x80u) != 0)) {
        if (!entry_added) {
          DisplayStandingChangeMessage(objects, faction_id, new_level);
        }
      }
      standing_changed = true;
      faction_state_changed = true;
    }
  }

  if (flags_changed) {
    RebuildFactionList();
  }

  (void)standing_changed;
  FireUpdate();
  last_standing_faction_state_changed_ = faction_state_changed;
  return true;
}

bool ReputationInfo::HandleSetForcedReactions(const std::uint8_t* data,
                                              std::size_t len) {
  if (!data || len < 4) {
    return false;
  }

  PacketReader r(data, len);
  std::uint32_t count = 0;
  if (!r.ReadU32(count)) {
    return false;
  }
  if (r.Remaining() < count * sizeof(ForcedReactionEntry)) {
    return false;
  }

  forced_reactions_.resize(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    auto& reaction = forced_reactions_[i];
    if (!r.ReadU32(reaction.faction_id)) {
      return false;
    }
    if (!r.ReadU32(reaction.standing)) {
      return false;
    }
  }

  FireUpdate();
  return true;
}

void ReputationInfo::SendToggleAtWar(std::int32_t faction_id,
                                     bool set_at_war,
                                     bool active_player_in_combat) {

  if (FindHeaderIndex(faction_id) > 0) return;

  if (!set_at_war && GetCurrentStanding(faction_id) < -3000) return;

  auto slot = LookupRepListId(faction_id);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots) return;
  auto uslot = static_cast<std::uint32_t>(slot);

  if (!set_at_war && active_player_in_combat) {
    ui::game::DisplaySystemMessage(450);
    return;
  }

  auto flags = rep_flags_[uslot];
  if (set_at_war && (flags & kRepFlagForced) == 0) {
    flags |= kRepFlagAtWar;
  } else {
    flags &= static_cast<std::uint8_t>(~kRepFlagAtWar);
  }
  rep_flags_[uslot] = flags;

  (void)net::ClientServices__SendPacket(
      BuildSetFactionAtWarPacket(uslot, set_at_war));
  FireUpdate();
  if (on_faction_state_refresh_) {
    on_faction_state_refresh_();
  }
}

void ReputationInfo::SendSetWatchedFaction(std::int32_t faction_id) {
  const auto slot = LookupRepListId(faction_id);
  if (!has_active_player_context_ || slot == watched_faction_slot_) {
    return;
  }

  (void)net::ClientServices__SendPacket(BuildSetWatchedFactionPacket(slot));
}

void ReputationInfo::SendSetInactive(std::int32_t faction_id, bool inactive) {
  if (FindHeaderIndex(faction_id) > 0) return;

  auto slot = LookupRepListId(faction_id);
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMaxRepSlots) return;
  auto uslot = static_cast<std::uint32_t>(slot);

  if (inactive) {
    rep_flags_[uslot] |= kRepFlagInactive;
  } else {
    rep_flags_[uslot] &= static_cast<std::uint8_t>(~kRepFlagInactive);
  }

  (void)net::ClientServices__SendPacket(
      BuildSetFactionInactivePacket(uslot, inactive));
  RebuildFactionList();
}

void ReputationInfo::ToggleHeaderCollapse(std::size_t entry_idx, bool collapse) {
  if (entry_idx >= num_entries_ || !entries_[entry_idx].is_header) {
    collapsed_mask_ = collapse ? std::uint64_t{0} : ~std::uint64_t{0};
    RebuildFactionList();
    return;
  }

  const auto header_index = FindHeaderIndex(entries_[entry_idx].parent_faction);
  if (header_index < 0) return;

  const auto hdr_idx = static_cast<std::size_t>(header_index);
  const auto header_faction_id = headers_[hdr_idx].faction_id;
  const auto collapsed_hidden = static_cast<std::uint8_t>(collapse ? 1 : 0);

  if (collapse) {
    collapsed_mask_ &= ~(std::uint64_t{1} << hdr_idx);
  } else {
    collapsed_mask_ |= (std::uint64_t{1} << hdr_idx);
  }

  for (std::size_t i = 0; i < num_headers_; ++i) {
    if (IsChildOfHeader(i, header_faction_id)) {
      headers_[i].collapsed_hidden = collapsed_hidden;
    }
  }

  RebuildFactionList();
}

void ReputationInfo::RebuildFactionList() {

  for (std::size_t h = 0; h < num_headers_; ++h) {
    headers_[h].child_count = 0;
  }

  for (std::size_t h = 0; h < num_headers_; ++h) {
    for (std::size_t e = 0; e < num_entries_; ++e) {
      std::int32_t parent_fid = entries_[e].parent_faction;

      auto slot = LookupRepListId(entries_[e].faction_id);
      if (slot >= 0 && static_cast<std::uint32_t>(slot) < kMaxRepSlots) {
        if (rep_flags_[slot] & kRepFlagInactive) {
          parent_fid = -1;
        }
      }

      if (parent_fid == headers_[h].faction_id && !entries_[e].is_header) {
        ++headers_[h].child_count;
      }
    }
  }

  for (std::size_t i = 0; i < num_headers_; ++i) {
    for (std::size_t j = 1; j < num_headers_; ++j) {
      if (i != j && headers_[j].parent_faction != 0) {
        if (headers_[i].faction_id == headers_[j].parent_faction) {
          headers_[i].child_count += headers_[j].child_count;
          if (headers_[j].parent_faction != -1
              && IsPlayerFriendly(headers_[j].faction_id)) {
            ++headers_[i].child_count;
          }
        }
      }
    }
  }

  std::array<std::int32_t, kMaxFactionHdrs> collapsed_header_ids{};
  for (std::size_t i = 0; i < num_headers_; ++i) {
    collapsed_header_ids[i] = IsHeaderCollapsed(i) ? headers_[i].faction_id : -2;
  }

  {
    const ScopedReputationSortContext sort_context(*this);
    std::qsort(headers_.data(), num_headers_, sizeof(headers_[0]),
               CompareFactionHeadersForQSort);
  }

  collapsed_mask_ = ~std::uint64_t{0};
  for (std::size_t old = 0; old < num_headers_; ++old) {
    if (collapsed_header_ids[old] != -2) {
      for (std::size_t k = 0; k < num_headers_; ++k) {
        if (headers_[k].faction_id == collapsed_header_ids[old]) {
          collapsed_mask_ &= ~(std::uint64_t{1} << k);
        }
      }
    }
  }

  num_visible_ = num_entries_;
  for (std::size_t e = 0; e < num_entries_; ++e) {
    std::int32_t parent_fid = IsInactive(e) ? -1 : entries_[e].parent_faction;
    entries_[e].header_idx = -1;

    for (std::size_t h = 0; h < num_headers_; ++h) {
      if (headers_[h].faction_id == parent_fid) {
        entries_[e].header_idx = static_cast<std::int32_t>(h);
        break;
      }
    }

    auto hidx = entries_[e].header_idx;
    if (hidx >= 0 && !entries_[e].is_header) {
      auto uhidx = static_cast<std::size_t>(hidx);
      if (IsHeaderCollapsed(uhidx) || headers_[uhidx].collapsed_hidden != 0) {
        --num_visible_;
      }
    }

    if (entries_[e].is_header && hidx >= 0) {
      auto uhidx = static_cast<std::size_t>(hidx);
      if (IsHeaderHiddenInList(headers_[uhidx])) {
        --num_visible_;
      }
    }
  }

  {
    const ScopedReputationSortContext sort_context(*this);
    std::qsort(entries_.data(), num_entries_, sizeof(entries_[0]),
               CompareFactionEntriesForQSort);
  }

  FireUpdate();
}

int ReputationInfo::SortFactionEntries(const FactionEntryInfo* a,
                                        const FactionEntryInfo* b,
                                        const ReputationInfo& self) {
  if (a->header_idx == b->header_idx) {
    if (a->is_header != 0 || b->is_header != 0) {
      return 2 * (a->is_header == 0) - 1;
    }
    return self.CompareEntryFactionNames(a->faction_id, b->faction_id);
  }

  const bool a_hidden = self.IsEntryHiddenForSort(*a);
  const bool b_hidden = self.IsEntryHiddenForSort(*b);
  if (a_hidden) {
    return !b_hidden;
  }
  if (b_hidden) {
    return -1;
  }
  return 2 * (a->header_idx >= b->header_idx) - 1;
}

int ReputationInfo::SortHeaders(const FactionHeader* a,
                                 const FactionHeader* b,
                                 const ReputationInfo& self) {
  const bool a_empty = self.IsEmptyHeader(*a);
  const bool b_empty = self.IsEmptyHeader(*b);
  if (a_empty) {
    return !b_empty;
  }
  if (b_empty) {
    return -1;
  }

  if (a->parent_faction != 0 && b->faction_id != 0
      && a->parent_faction == b->faction_id) {
    return 1;
  }
  if (b->parent_faction != 0 && a->faction_id != 0
      && b->parent_faction == a->faction_id) {
    return -1;
  }

  if (a->parent_faction != b->parent_faction) {
    auto* left = a;
    auto* right = b;

    while (left->parent_faction != 0) {
      const auto parent = self.FindHeaderIndex(left->parent_faction);
      left = (parent >= 0) ? &self.headers_[static_cast<std::size_t>(parent)] : nullptr;
      if (left == nullptr) {
        break;
      }
    }

    while (right->parent_faction != 0) {
      const auto parent = self.FindHeaderIndex(right->parent_faction);
      right = (parent >= 0) ? &self.headers_[static_cast<std::size_t>(parent)] : nullptr;
      if (right == nullptr) {
        break;
      }
    }

    if (left != nullptr && right != nullptr) {
      return self.CompareHeaderFactionNames(left->faction_id, right->faction_id);
    }
  }

  return self.CompareHeaderFactionNames(a->faction_id, b->faction_id);
}

void ReputationInfo::InitFromPlayerData(const ObjectManager& objects) {
  collapsed_mask_ = ~std::uint64_t{0};
  num_entries_ = 0;
  num_visible_ = 0;
  num_headers_ = 0;
  rep_faction_ids_.fill(0);
  rep_base_values_.fill(0);
  child_spillover_factions_.clear();

  if (dbc_ == nullptr || !player_race_id_.has_value() || !player_class_id_.has_value()) {
    return;
  }

  RebuildChildSpilloverFactionIndex();

  const auto race_bit = std::uint32_t{1} << (*player_race_id_ - 1);
  const auto class_bit = std::uint32_t{1} << (*player_class_id_ - 1);
  const auto& factions = dbc_->faction().entries();

  for (auto it = factions.rbegin(); it != factions.rend(); ++it) {
    if (it->reputation_list_id < 0
        || static_cast<std::uint32_t>(it->reputation_list_id) >= kMaxRepSlots) {
      continue;
    }

    const auto slot = static_cast<std::uint32_t>(it->reputation_list_id);
    rep_faction_ids_[slot] = it->id;

    for (std::size_t index = 0; index < it->base_rep_race_mask.size(); ++index) {
      const auto race_mask = it->base_rep_race_mask[index];
      const auto class_mask = it->base_rep_class_mask[index];
      if (race_mask != 0) {
        if ((race_mask & race_bit) == 0) {
          continue;
        }
        if (class_mask != 0 && (class_mask & class_bit) == 0) {
          continue;
        }
      } else {
        if (class_mask == 0 || (class_mask & class_bit) == 0) {
          continue;
        }
      }

      rep_base_values_[slot] = it->base_rep_value[index];
      if ((rep_flags_[slot] & kRepFlagVisible) != 0) {
        AddFactionEntry(objects, slot, false);
      }
    }
  }

  if (num_entries_ >= kMaxFactionEntry) {
    RebuildFactionList();
    return;
  }

  AddHeader(-1, 0);
  auto& other = entries_[num_entries_];
  other.rep_list_id = -1;
  other.faction_id = -1;
  other.is_header = 1;
  other.header_idx = -1;
  other.parent_faction = -1;
  ++num_entries_;

  ToggleHeaderCollapse(num_entries_ - 1, true);
  RebuildFactionList();
}

const std::vector<std::uint32_t>* ReputationInfo::FindChildSpilloverFactionIds(
    const std::int32_t parent_faction_id) const {
  const auto it = child_spillover_factions_.find(parent_faction_id);
  return it != child_spillover_factions_.end() ? &it->second : nullptr;
}

ReputationInfo::FactionLuaInfo ReputationInfo::PushFactionInfo(
    std::int32_t faction_id) const {
  FactionLuaInfo info;
  info.standing_id = 1;

  const auto* faction = LookupFactionEntry(faction_id);
  const auto slot = LookupRepListId(faction_id);
  if (faction == nullptr || slot < 0
      || static_cast<std::uint32_t>(slot) >= kMaxRepSlots) {
    const int hdr = FindHeaderIndex(faction_id);
    if (hdr < 0 || static_cast<std::size_t>(hdr) >= kMaxFactionHdrs) {
      return info;
    }

    const auto header_index = static_cast<std::size_t>(hdr);
    if (headers_[header_index].faction_id == -1) {
      info.name = Localization::Get().GetString("FACTION_INACTIVE", "FACTION_INACTIVE");
    } else if (headers_[header_index].faction_id == 0) {
      info.name = Localization::Get().GetString("FACTION_OTHER", "FACTION_OTHER");
    } else {
      return info;
    }

    info.description = std::string();
    info.is_header = true;
    if (IsHeaderCollapsed(header_index)) {
      info.is_collapsed = true;
    }
    return info;
  }

  info.name = std::string(LookupFactionName(faction_id));
  info.description = std::string(LookupFactionDescription(faction_id));

  const int level = GetStandingLevel(faction_id);
  info.standing_id = level + 1;

  info.bar_min = static_cast<int>(kStandingMin[level]);
  info.bar_max = (level < 7) ? static_cast<int>(kStandingMin[level + 1]) : 43000;
  info.bar_value = static_cast<int>(GetCurrentStanding(faction_id));

  if (IsAtWar(faction_id)) {
    info.at_war = true;
  }
  if (info.bar_value >= -3000 && !IsForced(faction_id)) {
    info.can_toggle_at_war = true;
  }

  const int hdr = FindHeaderIndex(faction_id);
  if (hdr >= 0) {
    const auto header_index = static_cast<std::size_t>(hdr);
    info.is_header = true;
    if (IsHeaderCollapsed(header_index)) {
      info.is_collapsed = true;
    }
    if (IsPlayerFriendly(faction_id)) {
      info.is_player_friendly = true;
    }
  }

  if (IsWatchedFaction(faction_id)) {
    info.is_watched = true;
  }
  if (IsChildFaction(faction_id)) {
    info.is_child = true;
  }
  return info;
}

const openwow::data::dbc::FactionEntry* ReputationInfo::LookupFactionEntry(
    std::int32_t faction_id) const {
  if (dbc_ == nullptr || faction_id < 0) {
    return nullptr;
  }
  return dbc_->faction().LookupEntry(static_cast<std::uint32_t>(faction_id));
}

std::int32_t ReputationInfo::LookupRepListId(std::int32_t faction_id) const {
  if (faction_id <= 0) {
    return -1;
  }

  if (const auto* entry = LookupFactionEntry(faction_id); entry != nullptr) {
    return entry->reputation_list_id;
  }
  return -1;
}

std::int32_t ReputationInfo::LookupParentFaction(std::int32_t faction_id) const {
  if (const auto* entry = LookupFactionEntry(faction_id); entry != nullptr) {
    return static_cast<std::int32_t>(entry->parent_faction_id);
  }
  return 0;
}

const char* ReputationInfo::LookupFactionName(std::int32_t faction_id) const {
  if (const auto* entry = LookupFactionEntry(faction_id); entry != nullptr) {
    return entry->name.data();
  }
  return "";
}

const char* ReputationInfo::LookupFactionDescription(std::int32_t faction_id) const {
  if (const auto* entry = LookupFactionEntry(faction_id); entry != nullptr) {
    return entry->description.data();
  }
  return "";
}

void ReputationInfo::RebuildChildSpilloverFactionIndex() {
  if (dbc_ == nullptr) {
    return;
  }

  const auto& factions = dbc_->faction().entries();
  for (auto it = factions.rbegin(); it != factions.rend(); ++it) {
    const auto& faction = *it;
    if (faction.parent_faction_id == 0 || faction.reputation_list_id < 0
        || static_cast<std::uint32_t>(faction.reputation_list_id) >= kMaxRepSlots) {
      continue;
    }

    child_spillover_factions_[static_cast<std::int32_t>(faction.parent_faction_id)]
        .push_back(faction.id);
  }
}

void ReputationInfo::DisplayRepChangeMessage(const ObjectManager& objects,
                                             std::int32_t faction_id,
                                             std::int32_t amount,
                                             const bool is_generic,
                                             const float bonus_rep) const {
  if (amount == 0) {
    return;
  }

  const char* faction_name = LookupFactionName(faction_id);
  if (faction_name == nullptr || faction_name[0] == '\0') {
    return;
  }

  const char* format_key = nullptr;
  if (is_generic) {
    format_key = amount > 0 ? "FACTION_STANDING_INCREASED_GENERIC"
                            : "FACTION_STANDING_DECREASED_GENERIC";
  } else if (amount <= 0) {
    format_key = "FACTION_STANDING_DECREASED";
  } else if (bonus_rep > 0.0f) {
    format_key = "FACTION_STANDING_INCREASED_BONUS";
  } else {
    format_key = "FACTION_STANDING_INCREASED";
  }

  const std::string format = GetLocalizedGlobalString(format_key);
  if (format.empty()) {
    return;
  }

  char buffer[kReputationMessageBufferSize]{};
  if (is_generic) {
    openwow::core::SStrPrintf(buffer, sizeof(buffer), format.c_str(),
                              faction_name);
  } else {
    openwow::core::SStrPrintf(buffer, sizeof(buffer), format.c_str(),
                              faction_name, std::abs(amount), bonus_rep);
  }

  ChatFrame_DisplayMessage(objects, buffer, ChatDisplayType::kCombatFactionChange,
                           nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0,
                           0, nullptr);
}

void ReputationInfo::DisplayStandingChangeMessage(const ObjectManager& objects,
                                                  std::int32_t faction_id,
                                                  const int standing_level) const {
  const char* faction_name = LookupFactionName(faction_id);
  if (faction_name == nullptr || faction_name[0] == '\0') {
    return;
  }

  if (!player_gender_id_.has_value()) {
    return;
  }
  const bool use_female_variant = *player_gender_id_ == 1;

  char standing_key[64]{};
  openwow::core::SStrPrintf(standing_key, sizeof(standing_key),
                            "FACTION_STANDING_LABEL%d", standing_level + 1);

  std::string standing_name = GetLocalizedGlobalString(standing_key);
  if (use_female_variant) {
    const std::string female_key = std::string(standing_key) + "_FEMALE";
    if (Localization::Get().HasString(female_key)) {
      standing_name = Localization::Get().GetString(female_key);
    }
  }

  const std::string format = GetLocalizedGlobalString("FACTION_STANDING_CHANGED");
  if (standing_name.empty() || format.empty()) {
    return;
  }

  char buffer[kReputationMessageBufferSize]{};
  openwow::core::SStrPrintf(buffer, sizeof(buffer), format.c_str(),
                            standing_name.c_str(), faction_name);
  ChatFrame_DisplayMessage(objects, buffer, ChatDisplayType::kSystem, nullptr, 0,
                           nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

bool ReputationInfo::IsEntryHiddenForSort(const FactionEntryInfo& entry) const {
  if (entry.header_idx < 0) {
    return false;
  }

  const auto header_index = static_cast<std::size_t>(entry.header_idx);
  const FactionHeader* header = (header_index < kMaxFactionHdrs) ? &headers_[header_index] : nullptr;
  if (!entry.is_header && IsHeaderCollapsed(header_index)) {
    return true;
  }
  if (header == nullptr) {
    return false;
  }
  if (header->collapsed_hidden != 0) {
    return true;
  }
  return entry.is_header != 0
      && (header->faction_id == 0 || header->faction_id == -1)
      && header->child_count == 0;
}

bool ReputationInfo::IsHeaderCollapsed(std::size_t header_index) const {
  return header_index < 64
      && (collapsed_mask_ & (std::uint64_t{1} << header_index)) == 0;
}

bool ReputationInfo::IsHeaderHiddenInList(const FactionHeader& header) const {
  return header.collapsed_hidden != 0 || IsEmptyHeader(header);
}

int ReputationInfo::CompareEntryFactionNames(std::int32_t lhs_faction_id,
                                             std::int32_t rhs_faction_id) const {
  const auto* lhs = LookupFactionEntry(lhs_faction_id);
  const auto* rhs = LookupFactionEntry(rhs_faction_id);
  if (lhs != nullptr && rhs != nullptr) {
    return openwow::core::SStrCmpNoCaseCollate(lhs->name.data(), rhs->name.data(),
                                               0x7FFFFFFF);
  }
  return 0;
}

int ReputationInfo::CompareHeaderFactionNames(std::int32_t lhs_faction_id,
                                              std::int32_t rhs_faction_id) const {
  const auto* lhs = LookupFactionEntry(lhs_faction_id);
  const auto* rhs = LookupFactionEntry(rhs_faction_id);
  if (lhs != nullptr) {
    if (rhs != nullptr) {
      return openwow::core::SStrCmpNoCaseCollate(lhs->name.data(), rhs->name.data(),
                                                 0x7FFFFFFF);
    }
    return -1;
  }
  if (rhs == nullptr) {
    return 2 * (lhs_faction_id < rhs_faction_id) - 1;
  }
  return 1;
}

void ReputationInfo::FireUpdate() {
  if (on_update_) on_update_();
}

}
