#pragma once

#include "openwow/game/world_session_fwd.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::game {

class GameUIManager;

class SessionEventBridge {
 public:
  SessionEventBridge();
  ~SessionEventBridge();

  SessionEventBridge(const SessionEventBridge&) = delete;
  SessionEventBridge& operator=(const SessionEventBridge&) = delete;

  void Initialize(openwow::game::WorldSession* session, GameUIManager* ui);

  void Poll(float elapsed_seconds);

  void Reset();

  [[nodiscard]] bool SynchronizePlayerUnitToken();

  void Shutdown();

 private:

  struct UnitSnapshot {
    std::uint32_t health{0};
    std::uint32_t max_health{0};
    std::uint32_t power{0};
    std::uint32_t max_power{0};
    std::uint32_t level{0};
    std::uint64_t target_guid{0};
    std::uint32_t flags{0};
    std::uint32_t stat0{0};
    std::uint32_t attack_power{0};
    std::uint32_t ranged_attack_power{0};
    std::uint32_t resistances{0};
    std::uint32_t combo_points{0};
    std::uint64_t pet_guid{0};
  };

  struct QuestSnapshot {
    std::size_t quest_count{0};
    std::uint64_t watch_update_serial{0};
    bool has_active_details{false};
    bool has_active_reward{false};
    bool has_active_request{false};
  };

  struct InventorySnapshot {
    std::uint32_t money{0};

  };

  struct SocialSnapshot {
    std::size_t party_count{0};
    std::size_t guild_count{0};
    std::size_t friend_count{0};
    bool is_raid{false};
  };

  struct LootSnapshot {
    bool loot_open{false};
  };

  struct GossipSnapshot {
    bool gossip_open{false};
  };

  struct SpellSnapshot {
    std::size_t spell_count{0};
    std::size_t cooldown_count{0};
  };

  struct TradeSnapshot {
    bool trade_open{false};
  };

  struct MailSnapshot {
    std::size_t inbox_count{0};
  };

  struct CombatSnapshot {
    bool in_combat{false};
    std::uint64_t combat_log_serial{0};
  };

  struct TalentSnapshot {
    std::uint8_t active_spec{0};
  };

  struct AuraSnapshot {

    std::size_t aura_count{0};
  };

  struct PlayerStateSnapshot {
    bool is_ghost{false};
    bool has_control{true};

    std::uint32_t player_flags{0};
    bool player_flags_seen{false};
    std::uint64_t pet_guid{0};
    std::uint64_t critter_guid{0};
  };

  struct SpellcastSnapshot {
    bool is_casting{false};
    bool is_channeling{false};
  };

  struct WorldStateUiSnapshot {
    std::uint32_t map_id{0};
  };

  UnitSnapshot SnapshotUnit(const std::string& unit_id) const;

  void PollPlayerState();
  void PollTargetState();
  void PollQuestState();
  void PollInventoryState();
  void PollSocialState();
  void PollLootState();
  void PollGossipState();
  void PollSpellState();
  void PollTradeState();
  void PollMailState(float elapsed_seconds);
  void PollCombatState();
  void PollTalentState();
  void PollPlayerExtendedState();
  void FirePlayerFlagTransitionEvents(std::uint32_t player_flags);
  void PollPetState();
  void PollCompanionState();
  void PollWorldStateUi();

  openwow::game::WorldSession* session_{nullptr};
  GameUIManager* ui_{nullptr};

  UnitSnapshot prev_player_;
  UnitSnapshot prev_target_;
  std::uint64_t prev_target_guid_{0};
  QuestSnapshot prev_quests_;
  InventorySnapshot prev_inventory_;
  SocialSnapshot prev_social_;
  LootSnapshot prev_loot_;
  GossipSnapshot prev_gossip_;
  SpellSnapshot prev_spells_;
  TradeSnapshot prev_trade_;
  MailSnapshot prev_mail_;
  CombatSnapshot prev_combat_;
  TalentSnapshot prev_talents_;
  AuraSnapshot prev_auras_;
  PlayerStateSnapshot prev_player_state_;
  SpellcastSnapshot prev_spellcast_;
  WorldStateUiSnapshot prev_world_state_ui_;

  bool initialized_{false};
};

}
