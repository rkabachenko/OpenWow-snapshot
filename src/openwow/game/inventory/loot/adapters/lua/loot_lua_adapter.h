#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;
namespace openwow::data::dbc { class DbcLoader; }
namespace openwow::game {
class GroupSystem;
class InteractionSender;
class ItemDefinitions;
struct ItemTemplate;
class LootInteraction;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class Localization;
class TargetingSystem;
class WorldSession;
}

namespace openwow::ui::game {

class ScriptEventDispatch;

enum class LootLuaEvent : std::uint8_t {
  kSlotCleared, kClosed, kOpenMasterList, kBindConfirm,
  kConfirmRoll, kConfirmDisenchant, kCancelRoll, kStartRoll,
};

class LootLuaAdapter final {
 public:
  struct Dependencies {
    const openwow::data::dbc::DbcLoader* dbc;
    openwow::game::GroupSystem& group;
    openwow::game::InteractionSender& interaction;
    openwow::game::ItemDefinitions& items;
    openwow::game::LootInteraction& loot;
    openwow::game::QueryCache& queries;
    openwow::game::PlayerInventoryReplica& inventory;
    openwow::game::Localization& localization;
    ScriptEventDispatch& events;
    openwow::game::TargetingSystem* targeting;
    openwow::game::WorldSession& world_session;
  };

  void Bind(Dependencies);
  [[nodiscard]] bool bound() const noexcept;
  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc() const noexcept;
  [[nodiscard]] openwow::game::GroupSystem& group() const;
  [[nodiscard]] openwow::game::InteractionSender& interaction() const;
  [[nodiscard]] openwow::game::ItemDefinitions& item_definitions() const;
  [[nodiscard]] openwow::game::LootInteraction& loot() const;

  [[nodiscard]] openwow::game::ObjectManager& objects() const;
  [[nodiscard]] openwow::game::QueryCache& query_cache() const;
  [[nodiscard]] std::string Localize(
      std::string_view, std::string_view) const;
  [[nodiscard]] std::string Format(
      std::string_view, std::vector<std::string>) const;
  void Present(LootLuaEvent, int = 0, int = 0) const;
  void CloseActiveLoot(bool) const;
  [[nodiscard]] bool CanPromptBind(
      const openwow::game::ItemTemplate&, std::uint32_t) const;
  [[nodiscard]] std::optional<std::uint64_t> ResolveMasterLooter(
      lua_State*, std::string_view, bool) const;
  void ApplyOptOut(bool) const;
  void RequestCandidateName(std::uint64_t) const;
  void ShowSystemMessage(int, std::string_view = {}) const;

 private:
  [[nodiscard]] const Dependencies& deps() const;
  std::optional<Dependencies> dependencies_;
};

[[nodiscard]] LootLuaAdapter& RequireLootLuaAdapter(lua_State*);

}
