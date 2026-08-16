#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::game {
namespace actions::held_cursor {
class HeldCursor;
}
class InteractionSender;
class ItemDefinitions;
class ItemInteractionSession;
class Localization;
class MailInteraction;
class MailStationeryChoices;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class SocialManager;
struct ItemUseRequirementView;
class ItemUseRequirementSources;
class SessionHandler;
class WorldSession;
class CGPlayer_C;
}

namespace openwow::ui::game {

class CVarSystem;
class ScriptEventDispatch;

enum class MailLuaEvent : std::uint8_t {
  kSendInfoChanged,
  kSendMoneyChanged,
  kSendCodChanged,
  kSendSucceeded,
  kLockSendItems,
  kUnlockSendItems,
  kInboxChanged,
  kPendingMailChanged,
  kMailClosed,
};

class MailLuaAdapter final {
 public:
  struct Dependencies {
    openwow::game::MailInteraction& mail;
    openwow::game::InteractionSender& interaction;
    openwow::game::WorldSession& world_session;
    openwow::game::PlayerInventoryReplica& inventory;
    openwow::game::QueryCache& queries;
    openwow::game::SocialManager& social;
    openwow::game::ItemDefinitions& items;
    openwow::game::ItemInteractionSession& item_interactions;
    openwow::game::Localization& localization;
    openwow::game::MailStationeryChoices& stationery;
    openwow::game::actions::held_cursor::HeldCursor* held_cursor;
    const openwow::data::dbc::DbcLoader* dbc;
    const openwow::game::ItemUseRequirementSources& item_requirements;
    openwow::game::SessionHandler& session_state;
    CVarSystem& cvars;
    ScriptEventDispatch& events;
  };

  void Bind(Dependencies);
  [[nodiscard]] bool bound() const noexcept;
  [[nodiscard]] openwow::game::MailInteraction& mail() const;
  [[nodiscard]] openwow::game::InteractionSender& interaction() const;

  [[nodiscard]] openwow::game::ObjectManager& objects() const;
  [[nodiscard]] openwow::game::PlayerInventoryReplica& inventory() const;
  [[nodiscard]] openwow::game::QueryCache& queries() const;
  [[nodiscard]] openwow::game::SocialManager& social() const;
  [[nodiscard]] openwow::game::ItemDefinitions& items() const;
  [[nodiscard]] openwow::game::ItemInteractionSession& item_interactions() const;
  [[nodiscard]] openwow::game::Localization& localization() const;
  [[nodiscard]] openwow::game::MailStationeryChoices& stationery() const;
  [[nodiscard]] openwow::game::actions::held_cursor::HeldCursor*
  held_cursor() const noexcept;
  [[nodiscard]] const openwow::data::dbc::DbcLoader* dbc() const noexcept;
  [[nodiscard]] bool MeetsItemRequirements(
      const openwow::game::CGPlayer_C& player,
      const openwow::game::ItemUseRequirementView& requirements) const;
  [[nodiscard]] std::string Localize(
      std::string_view key, std::string_view fallback = {}) const;
  [[nodiscard]] std::string Format(
      std::string_view format, std::vector<std::string> arguments) const;
  [[nodiscard]] bool HasLocalization(std::string_view key) const;
  [[nodiscard]] std::string ExpandBody(std::string_view text) const;
  void FilterMatureLanguage(std::string& text) const;
  [[nodiscard]] bool UseLongTimeFormat() const;
  void ShowSystemMessage(int message) const;
  void ShowSystemText(std::string_view message) const;
  void Present(MailLuaEvent event, int value = 0,
               std::string_view text = {}) const;
  void EnterItemMouseover(std::uint64_t guid) const;
  void LeaveItemMouseover(std::uint64_t guid) const;
  void PickupAttachment(std::uint64_t guid, int slot) const;

 private:
  [[nodiscard]] const Dependencies& deps() const;
  std::optional<Dependencies> dependencies_;
};

[[nodiscard]] MailLuaAdapter& RequireMailLuaAdapter(lua_State* state);

[[nodiscard]] MailLuaAdapter* TryGetMailLuaAdapter(lua_State* state);
}
