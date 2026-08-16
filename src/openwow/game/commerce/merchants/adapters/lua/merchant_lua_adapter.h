#pragma once

#include "openwow/game/commerce/merchants/merchant_requirements.h"

#include <cstdint>
#include <optional>

struct lua_State;
namespace openwow::data::dbc { class DbcLoader; }
namespace openwow::game {
class CursorSurface;
class GossipManager;
class InteractionSender;
class ItemInteractionSession;
struct ItemTemplate;
class LootInteraction;
class Localization;
class MiscHandler;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class ReputationInfo;
class SessionHandler;
class ItemUseRequirementSources;
class WorldSession;
namespace actions::held_cursor { class HeldCursor; }
}

namespace openwow::ui::game {

class ScriptEventDispatch;
class TooltipSystem;

class MerchantLuaAdapter final {
 public:
  struct RepairQuote {
    std::uint64_t vendor_guid{0};
    std::uint32_t total_cost{0};
  };
  struct Dependencies {
    const openwow::data::dbc::DbcLoader* dbc;
    openwow::game::GossipManager& gossip;
    openwow::game::actions::held_cursor::HeldCursor* held_cursor;
    openwow::game::CursorSurface* cursor;
    openwow::game::InteractionSender& interaction;
    openwow::game::ItemInteractionSession& item_interactions;
    openwow::game::LootInteraction& loot;
    openwow::game::Localization& localization;
    const openwow::game::MerchantArenaTeamQuery& arena_team_query;
    openwow::game::PlayerInventoryReplica& inventory;
    openwow::game::QueryCache& queries;
    openwow::game::WorldSession& world_session;
    openwow::game::MiscHandler& play_time;
    const openwow::game::ItemUseRequirementSources& item_requirements;
    openwow::game::SessionHandler& session_state;
    openwow::game::ReputationInfo& reputation;
    ScriptEventDispatch& events;
    TooltipSystem& tooltip;
  };

  void Bind(Dependencies);
  [[nodiscard]] bool bound() const noexcept;
  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc() const noexcept;
  [[nodiscard]] openwow::game::CursorSurface* cursor() const noexcept;
  [[nodiscard]] openwow::game::GossipManager& gossip() const;
  [[nodiscard]] openwow::game::actions::held_cursor::HeldCursor*
  held_cursor() const noexcept;
  [[nodiscard]] openwow::game::InteractionSender& interaction() const;
  [[nodiscard]] openwow::game::ItemInteractionSession& item_interactions() const;
  [[nodiscard]] openwow::game::LootInteraction& loot() const;
  [[nodiscard]] openwow::game::Localization& localization() const;
  [[nodiscard]] const openwow::game::MerchantArenaTeamQuery&
  arena_team_query() const;

  [[nodiscard]] openwow::game::ObjectManager& objects() const;
  [[nodiscard]] openwow::game::PlayerInventoryReplica& inventory() const;
  [[nodiscard]] openwow::game::QueryCache& query_cache() const;
  [[nodiscard]] openwow::game::WorldSession& world_session() const;
  [[nodiscard]] std::uint32_t total_played_time() const;
  [[nodiscard]] bool CanUseItem(const openwow::game::ItemTemplate&) const;
  [[nodiscard]] std::optional<RepairQuote> repair_quote() const;
  void CloseMerchant() const;
  void PresentMerchantUpdated() const;
  void ShowItemTooltip(std::uint32_t) const;

 private:
  [[nodiscard]] const Dependencies& deps() const;
  std::optional<Dependencies> dependencies_;
};

[[nodiscard]] MerchantLuaAdapter& RequireMerchantLuaAdapter(lua_State*);

[[nodiscard]] MerchantLuaAdapter* TryMerchantLuaAdapter(lua_State*) noexcept;

}
