
#include "openwow/game/gossip_manager.h"

#include <algorithm>

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"

namespace openwow::game {

using net::wotlk::Opcode;
using net::wotlk::WorldPacket;

namespace {

constexpr std::uint8_t kMaxVendorItems = 150;

}

bool GossipManager::HandleGossipMessage(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  GossipDialogData d;
  if (!r.ReadGuid(d.npc_guid)) return false;
  if (!r.ReadU32(d.menu_id) || !r.ReadU32(d.title_text_id)) return false;

  std::uint32_t gossip_count;
  if (!r.ReadU32(gossip_count)) return false;
  const auto clamped_gossip_count =
      std::min<std::uint32_t>(gossip_count, static_cast<std::uint32_t>(kMaxGossipMenuItems));
  d.items.resize(clamped_gossip_count);

  for (std::uint32_t i = 0; i < clamped_gossip_count; ++i) {
    auto& item = d.items[i];
    std::uint8_t is_coded;
    if (!r.ReadU32(item.menu_item_id) || !r.ReadU8(item.icon) ||
        !r.ReadU8(is_coded) || !r.ReadU32(item.box_money) ||
        !r.ReadCString(item.message, kGossipOptionTextMaxBytesIncludingNul) ||
        !r.ReadCString(item.box_message, kGossipOptionTextMaxBytesIncludingNul))
      return false;
    item.is_coded = is_coded != 0;
  }

  std::uint32_t quest_count;
  if (!r.ReadU32(quest_count)) return false;
  if (quest_count > kMaxGossipQuestItems) return false;
  d.quests.resize(quest_count);

  for (std::uint32_t i = 0; i < quest_count; ++i) {
    auto& q = d.quests[i];
    std::uint8_t repeatable;
    if (!r.ReadU32(q.quest_id) || !r.ReadU32(q.quest_icon) ||
        !r.ReadI32(q.quest_level) || !r.ReadU32(q.quest_flags) ||
        !r.ReadU8(repeatable) ||
        !r.ReadCString(q.title, kGossipQuestTitleMaxBytesIncludingNul))
      return false;
    q.is_repeatable = repeatable != 0;
  }

  gossip_ = std::move(d);
  interaction_guid_ = gossip_->npc_guid;
  display_text_.clear();
  return true;
}

bool GossipManager::HandleTrainerList(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  TrainerList t;
  if (!r.ReadGuid(t.trainer_guid)) return false;
  if (!r.ReadI32(t.trainer_type)) return false;

  std::int32_t spell_count;
  if (!r.ReadI32(spell_count)) return false;
  constexpr std::size_t kTrainerSpellWireBytes = 38u;
  if (spell_count < 0 || r.Remaining() == 0u ||
      static_cast<std::size_t>(spell_count) >
          (r.Remaining() - 1u) / kTrainerSpellWireBytes) {
    return false;
  }
  t.spells.resize(static_cast<size_t>(spell_count));

  for (std::int32_t i = 0; i < spell_count; ++i) {
    auto& s = t.spells[static_cast<size_t>(i)];
    std::uint8_t usable, req_level;
    if (!r.ReadI32(s.spell_id) || !r.ReadU8(usable) ||
        !r.ReadI32(s.money_cost) || !r.ReadI32(s.point_cost_0) ||
        !r.ReadI32(s.point_cost_1) || !r.ReadU8(req_level) ||
        !r.ReadI32(s.req_skill_line) || !r.ReadI32(s.req_skill_rank) ||
        !r.ReadI32(s.req_abilities[0]) || !r.ReadI32(s.req_abilities[1]) ||
        !r.ReadI32(s.req_abilities[2]))
      return false;
    s.state = static_cast<TrainerSpellState>(usable);
    s.req_level = req_level;
  }

  if (!r.ReadCString(t.greeting)) return false;

  trainer_ = std::move(t);
  interaction_guid_ = trainer_->trainer_guid;
  trainer_type_ = trainer_->trainer_type;
  return true;
}

bool GossipManager::HandleListInventory(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  VendorList v;
  if (!r.ReadGuid(v.vendor_guid)) return false;

  std::uint8_t item_count;
  if (!r.ReadU8(item_count)) return false;
  if (item_count > kMaxVendorItems) return false;

  if (item_count == 0) {
    std::uint8_t reason = 0;
    if (!r.ReadU8(reason)) return false;

    switch (reason) {
      case 0:
        merchant_.ObserveSnapshot(std::move(v),
                                  VendorListResult::kNoInventory);
        return true;
      case 1:
        merchant_.ObserveFailure(v.vendor_guid, VendorListResult::kDisliked);
        return true;
      case 2:
        merchant_.ObserveFailure(v.vendor_guid,
                                 VendorListResult::kTooFarAway);
        return true;
      case 3:
        merchant_.ObserveFailure(v.vendor_guid, VendorListResult::kVendorDead);
        return true;
      case 4:
        merchant_.ObserveFailure(v.vendor_guid, VendorListResult::kPlayerDead);
        return true;
      default:
        merchant_.ObserveFailure(v.vendor_guid,
                                 VendorListResult::kUnknownFailure);
        return true;
    }
  }

  v.items.resize(item_count);
  for (std::uint8_t i = 0; i < item_count; ++i) {
    auto& vi = v.items[i];
    std::int32_t max_count;
    if (!r.ReadU32(vi.slot) || !r.ReadU32(vi.item_id) ||
        !r.ReadU32(vi.display_info_id) || !r.ReadI32(max_count) ||
        !r.ReadU32(vi.price) || !r.ReadU32(vi.max_durability) ||
        !r.ReadU32(vi.buy_count) || !r.ReadU32(vi.extended_cost)) {
      return false;
    }
    vi.max_count = max_count;
  }

  merchant_.ObserveSnapshot(std::move(v), VendorListResult::kItems);
  return true;
}

WorldPacket GossipManager::BuildGossipHello(const ObjectGuid& npc) {
  WorldPacket pkt(Opcode::CMSG_GOSSIP_HELLO);
  pkt.AppendU64(npc.GetRawValue());
  return pkt;
}

WorldPacket GossipManager::BuildGossipSelectOption(
    const ObjectGuid& npc, std::uint32_t menu_id,
    std::uint32_t gossip_list_id, const std::string& code) {
  WorldPacket pkt(Opcode::CMSG_GOSSIP_SELECT_OPTION);
  pkt.AppendU64(npc.GetRawValue());
  pkt.AppendU32(menu_id);
  pkt.AppendU32(gossip_list_id);
  if (!code.empty()) pkt.AppendString(code.c_str());
  return pkt;
}

WorldPacket GossipManager::BuildNpcTextQuery(std::uint32_t text_id,
                                             const ObjectGuid& npc) {
  WorldPacket pkt(Opcode::CMSG_NPC_TEXT_QUERY);
  pkt.AppendU32(text_id);
  pkt.AppendU64(npc.GetRawValue());
  return pkt;
}

WorldPacket GossipManager::BuildTrainerBuySpell(const ObjectGuid& trainer,
                                                std::int32_t spell_id) {
  WorldPacket pkt(Opcode::CMSG_TRAINER_BUY_SPELL);
  pkt.AppendU64(trainer.GetRawValue());
  std::uint32_t raw;
  std::memcpy(&raw, &spell_id, 4);
  pkt.AppendU32(raw);
  return pkt;
}

WorldPacket GossipManager::BuildBuyItem(const ObjectGuid& vendor,
                                        std::uint32_t item_id,
                                        std::uint32_t slot,
                                        std::uint32_t count) {
  WorldPacket pkt(Opcode::CMSG_BUY_ITEM);
  pkt.AppendU64(vendor.GetRawValue());
  pkt.AppendU32(item_id);
  pkt.AppendU32(slot);
  pkt.AppendU32(count);
  pkt.AppendU8(0);

  return pkt;
}

WorldPacket GossipManager::BuildSellItem(const ObjectGuid& vendor,
                                         const ObjectGuid& item,
                                         std::uint32_t count) {
  WorldPacket pkt(Opcode::CMSG_SELL_ITEM);
  pkt.AppendU64(vendor.GetRawValue());
  pkt.AppendU64(item.GetRawValue());
  pkt.AppendU32(count);
  return pkt;
}

const GossipQuestItem* GossipManager::GetGossipAvailableQuest(
    std::uint32_t index) const {
  if (!gossip_.has_value())
    return nullptr;

  const auto& quests = gossip_->quests;
  std::uint32_t match_count = 0;

  for (const auto& quest : quests) {
    if (quest.quest_icon == 3 || quest.quest_icon == 4)
      continue;

    if (match_count == index)
      return &quest;
    ++match_count;
  }

  return nullptr;
}

const GossipQuestItem* GossipManager::GetGossipActiveQuest(
    std::uint32_t index) const {
  if (!gossip_.has_value())
    return nullptr;

  const auto& quests = gossip_->quests;
  std::uint32_t match_count = 0;

  for (const auto& quest : quests) {
    if (quest.quest_icon != 3 && quest.quest_icon != 4)
      continue;

    if (match_count == index)
      return &quest;
    ++match_count;
  }

  return nullptr;
}

std::optional<GossipQuestSelection>
GossipManager::GetGossipAvailableQuestSelection(std::uint32_t index) const {
  const auto* quest = GetGossipAvailableQuest(index);
  if (quest == nullptr)
    return std::nullopt;

  return GossipQuestSelection{
      .quest = quest,
      .action = quest->quest_icon == 0 ? GossipQuestSelectionAction::kCompleteQuest
                                       : GossipQuestSelectionAction::kQueryQuestResync,
  };
}

}
