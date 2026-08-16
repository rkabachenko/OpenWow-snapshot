#pragma once

#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace openwow::data {
class WDBPersistence;
}

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

class EquipmentSets;
class CombatLog;
class CooldownTracker;
class InventoryMessageState;
class ItemDefinitions;
class ItemInteractionSession;
class ObjectManager;
class PlayerInventoryReplica;
class PlayerInventoryReplicaSync;
class QueryCache;
class SpellBook;
class Localization;
void HandleItemRefundInfoPacket(
    ItemInteractionSession& interactions,
    const net::wotlk::WorldPacket& packet);
void HandleItemEnchantTimePacket(
    ObjectManager& objects, const net::wotlk::WorldPacket& packet);
void HandleSocketGemsResultPacket(
    ItemInteractionSession& interactions,
    const net::wotlk::WorldPacket& packet);
void HandleEquipmentSetUseResultPacket(
    EquipmentSets& equipment, Localization& localization,
    const net::wotlk::WorldPacket& packet);
void HandleItemTimeUpdatePacket(
    ObjectManager& objects, const net::wotlk::WorldPacket& packet);
void HandleBuyBankSlotResultPacket(const net::wotlk::WorldPacket& packet);
void HandleEquipmentSetSavedPacket(
    EquipmentSets& equipment, const net::wotlk::WorldPacket& packet);
[[nodiscard]] std::optional<ItemPushResult> HandleItemPushResultPacket(
    InventoryMessageState& state,
    const net::wotlk::WorldPacket& packet);
[[nodiscard]] std::optional<InventoryChangeFailure>
HandleInventoryChangeFailurePacket(
    InventoryMessageState& state,
    const net::wotlk::WorldPacket& packet);
void HandleEnchantmentLogPacket(
    CombatLog& combat_log, ItemDefinitions& items, QueryCache& queries,
    const net::wotlk::WorldPacket& packet);
[[nodiscard]] bool HandleItemChargesPacket(
    ObjectManager& objects, PlayerInventoryReplicaSync& inventory_sync,
    const net::wotlk::WorldPacket& packet);
void HandleItemRefundResultPacket(
    ItemInteractionSession& interactions, ObjectManager& objects,
    QueryCache& queries, Localization& localization,
    const net::wotlk::WorldPacket& packet);
void HandleItemCooldownPacket(
    PlayerInventoryReplica& inventory, ObjectManager& objects,
    QueryCache& queries, const SpellBook& spell_book,
    CooldownTracker& cooldowns, std::uint64_t elapsed_milliseconds,
    bool has_shapeshift_forms,
    const net::wotlk::WorldPacket& packet);
[[nodiscard]] std::optional<std::uint64_t> HandleItemTextPacket(
    ItemInteractionSession& interactions,
    openwow::data::WDBPersistence& persistence,
    const net::wotlk::WorldPacket& packet);

}
