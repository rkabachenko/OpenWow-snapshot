#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

struct lua_State;

namespace openwow::data::dbc { class DbcLoader; }
namespace openwow::game {
class AuctionInteraction;
class AuctionPacketHandler;
enum class AuctionSelectionList : std::uint8_t;
class CGPlayer_C;
class InteractionSender;
class ItemDefinitions;
struct ItemTemplate;
struct ItemUseRequirementView;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class SessionHandler;
class WorldSession;
class ItemUseRequirementSources;
namespace actions::held_cursor { class HeldCursor; }
}

namespace openwow::ui::game {

class ScriptEventDispatch;
class SecureExecution;

class AuctionLuaAdapter final {
 public:
  struct Dependencies {
    openwow::game::AuctionInteraction& auction;
    openwow::game::AuctionPacketHandler& packets;
    const openwow::data::dbc::DbcLoader* dbc;
    openwow::game::actions::held_cursor::HeldCursor* held_cursor;
    openwow::game::InteractionSender& interaction;
    openwow::game::ItemDefinitions& items;
    openwow::game::WorldSession& world_session;
    openwow::game::PlayerInventoryReplica& inventory;
    openwow::game::QueryCache& queries;
    const openwow::game::ItemUseRequirementSources& item_requirements;
    openwow::game::SessionHandler& session_state;
    ScriptEventDispatch& events;
    SecureExecution& security;
  };

  void Bind(Dependencies);
  [[nodiscard]] bool bound() const noexcept;
  [[nodiscard]] openwow::game::AuctionInteraction& auction() const;
  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc() const noexcept;
  [[nodiscard]] openwow::game::actions::held_cursor::HeldCursor*
  held_cursor() const noexcept;
  [[nodiscard]] openwow::game::InteractionSender& interaction() const;
  [[nodiscard]] openwow::game::ItemDefinitions& item_definitions() const;

  [[nodiscard]] openwow::game::ObjectManager& objects() const;
  [[nodiscard]] openwow::game::PlayerInventoryReplica& inventory() const;
  [[nodiscard]] openwow::game::QueryCache& query_cache() const;
  void PresentListChanged(openwow::game::AuctionSelectionList list) const;
  void PresentSellSelectionChanged() const;
  void PresentMultiSellStarted(std::uint32_t stack_count) const;
  void PresentMultiSellFailed() const;
  void RequestOwnerRefresh() const;
  void RequestNameRefresh(
      std::uint64_t guid, openwow::game::AuctionSelectionList list) const;
  void CloseHouse() const;
  [[nodiscard]] bool MeetsItemRequirements(
      const openwow::game::CGPlayer_C&,
      const openwow::game::ItemUseRequirementView&) const;
  [[nodiscard]] bool CanAcquireItem(
      const openwow::game::ItemTemplate&, std::uint32_t) const;

  [[nodiscard]] bool CanPerformProtectedAction(int action_kind) const;
  void ReturnSellItemToCursor(
      std::uint64_t, std::uint64_t, std::int32_t) const;
  void SellItems(
      std::uint64_t,
      std::vector<std::pair<std::uint64_t, std::uint32_t>>,
      std::uint32_t, std::uint32_t, std::uint32_t) const;

 private:
  [[nodiscard]] const Dependencies& deps() const;
  std::optional<Dependencies> dependencies_;
};

[[nodiscard]] AuctionLuaAdapter& RequireAuctionLuaAdapter(lua_State*);

}
