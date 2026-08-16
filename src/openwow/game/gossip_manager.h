
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openwow/game/commerce/merchants/merchant_interaction.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/trainer_system.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::game {

inline constexpr std::size_t kMaxGossipMenuItems = 32u;
inline constexpr std::size_t kMaxGossipQuestItems = 32u;
inline constexpr std::size_t kGossipOptionTextMaxBytesIncludingNul = 0x800u;
inline constexpr std::size_t kGossipQuestTitleMaxBytesIncludingNul = 0x200u;

struct GossipMenuItem {
  std::uint32_t menu_item_id = 0;
  std::uint8_t icon = 0;
  bool is_coded = false;
  std::uint32_t box_money = 0;
  std::string message;
  std::string box_message;
};

struct GossipQuestItem {
  std::uint32_t quest_id = 0;
  std::uint32_t quest_icon = 0;
  std::int32_t quest_level = 0;
  std::uint32_t quest_flags = 0;
  bool is_repeatable = false;
  std::string title;
};

enum class GossipQuestSelectionAction : std::uint8_t {
  kCompleteQuest = 0,
  kQueryQuestResync = 1,
};

struct GossipQuestSelection {
  const GossipQuestItem* quest = nullptr;
  GossipQuestSelectionAction action = GossipQuestSelectionAction::kCompleteQuest;
};

struct GossipDialogData {
  ObjectGuid npc_guid;
  std::uint32_t menu_id = 0;
  std::uint32_t title_text_id = 0;
  std::vector<GossipMenuItem> items;
  std::vector<GossipQuestItem> quests;
};

struct TrainerSpell {
  std::int32_t spell_id = 0;
  TrainerSpellState state = TrainerSpellState::Unavailable;
  std::int32_t money_cost = 0;
  std::int32_t point_cost_0 = 0;
  std::int32_t point_cost_1 = 0;
  std::uint8_t req_level = 0;
  std::int32_t req_skill_line = 0;
  std::int32_t req_skill_rank = 0;
  std::int32_t req_abilities[3] = {};
};

struct TrainerList {
  ObjectGuid trainer_guid;
  std::int32_t trainer_type = 0;
  std::vector<TrainerSpell> spells;
  std::string greeting;
};

class GossipManager {
 public:
  bool HandleGossipMessage(const std::uint8_t* data, std::size_t len);
  bool HandleTrainerList(const std::uint8_t* data, std::size_t len);
  bool HandleListInventory(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] static net::wotlk::WorldPacket BuildGossipHello(
      const ObjectGuid& npc);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGossipSelectOption(
      const ObjectGuid& npc, std::uint32_t menu_id,
      std::uint32_t gossip_list_id, const std::string& code = "");
  [[nodiscard]] static net::wotlk::WorldPacket BuildNpcTextQuery(
      std::uint32_t text_id, const ObjectGuid& npc);
  [[nodiscard]] static net::wotlk::WorldPacket BuildTrainerBuySpell(
      const ObjectGuid& trainer, std::int32_t spell_id);
  [[nodiscard]] static net::wotlk::WorldPacket BuildBuyItem(
      const ObjectGuid& vendor, std::uint32_t item_id, std::uint32_t slot,
      std::uint32_t count);
  [[nodiscard]] static net::wotlk::WorldPacket BuildSellItem(
      const ObjectGuid& vendor, const ObjectGuid& item, std::uint32_t count);

  [[nodiscard]] bool has_gossip() const { return gossip_.has_value(); }
  [[nodiscard]] const GossipDialogData& gossip() const { return gossip_.value(); }
  [[nodiscard]] const ObjectGuid& interaction_guid() const { return interaction_guid_; }
  void BeginTrainerRequest(ObjectGuid trainer_guid) {
    interaction_guid_ = trainer_guid;
  }
  [[nodiscard]] const std::string& display_text() const { return display_text_; }
  void SetDisplayText(std::string text) { display_text_ = std::move(text); }
  [[nodiscard]] bool has_trainer() const { return trainer_.has_value(); }
  [[nodiscard]] const TrainerList& trainer() const { return trainer_.value(); }
  [[nodiscard]] std::int32_t trainer_type() const { return trainer_type_; }

  bool MarkTrainerSpellKnown(std::int32_t spell_id) {
    if (!trainer_.has_value()) {
      return false;
    }
    bool changed = false;
    for (TrainerSpell& spell : trainer_->spells) {
      if (spell.spell_id == spell_id && spell.state != TrainerSpellState::Known) {
        spell.state = TrainerSpellState::Known;
        changed = true;
      }
    }
    return changed;
  }
  [[nodiscard]] MerchantInteraction& merchant() noexcept { return merchant_; }
  [[nodiscard]] const MerchantInteraction& merchant() const noexcept {
    return merchant_;
  }

  void ClearGossipDialog() {
    gossip_.reset();
    display_text_.clear();
  }

  void DismissAll() {
    gossip_.reset();
    display_text_.clear();
    interaction_guid_ = {};
    trainer_.reset();
    merchant_.Close();
  }
  void Clear() { DismissAll(); }

  [[nodiscard]] const GossipQuestItem* GetGossipAvailableQuest(
      std::uint32_t index) const;

  [[nodiscard]] const GossipQuestItem* GetGossipActiveQuest(
      std::uint32_t index) const;

  [[nodiscard]] std::optional<GossipQuestSelection>
  GetGossipAvailableQuestSelection(std::uint32_t index) const;

 private:
  std::optional<GossipDialogData> gossip_;
  std::string display_text_;
  ObjectGuid interaction_guid_{};
  std::optional<TrainerList> trainer_;
  std::int32_t trainer_type_ = -1;
  MerchantInteraction merchant_;
};

}
