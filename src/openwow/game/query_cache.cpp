
#include "openwow/game/query_cache.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_manager.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

namespace {

constexpr std::size_t kNameCacheMaxNameBytes = 0x30;
constexpr std::size_t kNameCacheMaxDeclinedBytes = 0x40;
constexpr std::size_t kNameCacheMaxRealmBytes = 0x100;
constexpr std::size_t kCreatureStringMaxBytes = 0x400;
constexpr std::size_t kGameObjectStringMaxBytes = 0x400;
constexpr std::size_t kItemNameMaxBytes = 400;
constexpr std::size_t kItemDescriptionMaxBytes = 0x400;
constexpr std::size_t kItemMaxStats = 10;
constexpr std::size_t kNpcTextStringMaxBytes = 3000;

bool StoreRetailWdbRecord(openwow::data::DBCacheRuntime& runtime,
                          const openwow::data::WDBCacheType type,
                          const std::uint32_t entry,
                          const std::uint32_t format_version,
                          const std::uint8_t *packet_data,
                          const std::size_t consumed_bytes) {

  constexpr std::size_t kKeyBytes = sizeof(std::uint32_t);
  if (entry == 0 || packet_data == nullptr || consumed_bytes < kKeyBytes) {
    return false;
  }

  std::vector<std::uint8_t> payload(packet_data + kKeyBytes,
                                    packet_data + consumed_bytes);
  auto &cache = runtime.cache();
  cache.UpdateEntry(type, entry, std::move(payload), format_version);
  runtime.persistence().SetDirty(type);
  return cache.Has(type, entry);
}

bool InvalidateRetailWdbRecord(openwow::data::DBCacheRuntime& runtime,
                               const openwow::data::WDBCacheType type,
                               const std::uint32_t entry) {
  if (!runtime.cache().InvalidateEntry(type, entry)) {
    return false;
  }
  runtime.persistence().SetDirty(type);
  return true;
}

void BuildWdbHydrationPacket(const std::uint32_t entry,
                             const std::vector<std::uint8_t> &payload,
                             std::vector<std::uint8_t> &packet) {
  packet.clear();
  packet.reserve(sizeof(entry) + payload.size());
  packet.push_back(static_cast<std::uint8_t>(entry & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((entry >> 8u) & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((entry >> 16u) & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((entry >> 24u) & 0xFFu));
  packet.insert(packet.end(), payload.begin(), payload.end());
}

using PlayerNameHashIndex =
    openwow::foundation::FlatHashMap<std::uint32_t, std::vector<std::uint64_t>>;

void IndexPlayerNameEntry(PlayerNameHashIndex& index, const PlayerNameInfo& info) {
  if (info.name.empty()) {
    return;
  }

  index[openwow::core::SStrHashCI(info.name.c_str())].push_back(
      info.guid.GetRawValue());
}

void UnindexPlayerNameEntry(PlayerNameHashIndex& index, const PlayerNameInfo& info) {
  if (info.name.empty()) {
    return;
  }

  const auto hash = openwow::core::SStrHashCI(info.name.c_str());
  auto* const guids = index.FindValue(hash);
  if (guids == nullptr) {
    return;
  }

  const auto raw_guid = info.guid.GetRawValue();
  guids->erase(std::remove(guids->begin(), guids->end(), raw_guid), guids->end());
  if (guids->empty()) {
    index.erase(hash);
  }
}

}

QueryCache::QueryCache(openwow::data::DBCacheRuntime& db_cache_runtime,
                       ItemDefinitions& item_definitions)
    : db_cache_runtime_(db_cache_runtime),
      item_definitions_(item_definitions),
      tick_count_provider_([]() { return openwow::core::GameClock::GetTickCount32(); }) {}

void QueryCache::UnindexPlayerNameLocked(const std::uint64_t raw_guid) {
  if (const PlayerNameInfo* const info = player_names_.FindValue(raw_guid)) {
    UnindexPlayerNameEntry(player_name_hash_index_, *info);
  }
}

void QueryCache::ErasePlayerNameLocked(const std::uint64_t raw_guid) {
  UnindexPlayerNameLocked(raw_guid);
  player_name_persistence_serials_.erase(raw_guid);
  player_names_.erase(raw_guid);
}

void QueryCache::ClearPlayerNameCacheLocked() {
  player_names_.clear();
  player_name_hash_index_.clear();
  player_name_persistence_serials_.clear();
  next_player_name_persistence_serial_ = 1;
  name_queries_.Clear();
  active_name_query_callback_drains_.clear();
}

std::vector<QueryCache::QueryCallback>
QueryCache::ResolveNameQueryCallbacksLocked(const std::uint64_t raw_guid,
                                            const bool erase_on_finish) {
  active_name_query_callback_drains_.push_back(ActiveNameQueryCallbackDrain{
      .guid = raw_guid,
      .erase_on_finish = erase_on_finish,
      .deferred_invalidation = false,
  });
  return name_queries_.Resolve(raw_guid);
}

void QueryCache::FinishNameQueryCallbackDrainLocked(const std::uint64_t raw_guid) {
  for (auto it = active_name_query_callback_drains_.rbegin();
       it != active_name_query_callback_drains_.rend(); ++it) {
    if (it->guid != raw_guid) {
      continue;
    }

    const bool erase_on_finish = it->erase_on_finish || it->deferred_invalidation;
    active_name_query_callback_drains_.erase(std::next(it).base());
    if (erase_on_finish) {
      ErasePlayerNameLocked(raw_guid);
    }
    break;
  }
}

template <typename EntryMap>
bool QueryCache::BeginUint32InvalidationLocked(
    EntryMap &entries, AsyncQueryChannel &queries,
    std::vector<ActiveQueryCallbackDrain> &active_drains,
    const std::uint32_t entry, std::vector<QueryCallback> &callbacks,
    bool &started_callback_drain, bool &deferred_to_active_drain) {
  started_callback_drain = false;
  deferred_to_active_drain = false;

  for (auto it = active_drains.rbegin(); it != active_drains.rend(); ++it) {
    if (it->entry != entry) {
      continue;
    }
    it->deferred_invalidation = true;
    deferred_to_active_drain = true;
    return true;
  }

  const bool erased = entries.erase(entry) != 0;
  if (!queries.IsPending(entry)) {
    return erased;
  }

  callbacks = queries.Resolve(entry);
  active_drains.push_back(
      ActiveQueryCallbackDrain{.entry = entry, .deferred_invalidation = false});
  started_callback_drain = true;
  return true;
}

template <typename EntryMap>
bool QueryCache::FinishUint32CallbackDrainLocked(
    EntryMap &entries,
    std::vector<ActiveQueryCallbackDrain> &active_drains,
    const std::uint32_t entry) {
  for (auto it = active_drains.rbegin(); it != active_drains.rend(); ++it) {
    if (it->entry != entry) {
      continue;
    }

    const bool deferred_invalidation = it->deferred_invalidation;
    active_drains.erase(std::next(it).base());
    if (deferred_invalidation) {
      entries.erase(entry);
    }
    return deferred_invalidation;
  }
  return false;
}

bool QueryCache::HandleNameQueryResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  ObjectGuid guid;
  if (!r.ReadPackedGuid(guid))
    return false;

  std::uint8_t response_type;
  if (!r.ReadU8(response_type))
    return false;

  const auto finish_name_update = [&](PlayerNameInfo info) {
    std::vector<QueryCallback> callbacks;
    {
      std::lock_guard lock(mutex_);
      UnindexPlayerNameLocked(guid.GetRawValue());
      const auto [it, inserted] =
          player_names_.insert_or_assign(guid.GetRawValue(), std::move(info));
      IndexPlayerNameEntry(player_name_hash_index_, it->second);
      if (inserted) {
        player_name_persistence_serials_[guid.GetRawValue()] =
            next_player_name_persistence_serial_++;
      }
      callbacks = ResolveNameQueryCallbacksLocked(guid.GetRawValue(), false);
    }

    for (auto &callback : callbacks) {
      callback(true);
    }

    {
      std::lock_guard lock(mutex_);
      FinishNameQueryCallbackDrainLocked(guid.GetRawValue());
    }
    return true;
  };

  const auto fail_name_query = [&]() {
    std::vector<QueryCallback> callbacks;
    {
      std::lock_guard lock(mutex_);
      callbacks = ResolveNameQueryCallbacksLocked(guid.GetRawValue(), true);
    }

    for (auto &callback : callbacks) {
      callback(false);
    }

    {
      std::lock_guard lock(mutex_);
      FinishNameQueryCallbackDrainLocked(guid.GetRawValue());
    }
    return true;
  };

  if (response_type == 3) {
    PlayerNameInfo info;
    info.guid = guid;
    info.name = "?";
    return finish_name_update(std::move(info));
  }

  if (response_type == 2) {
    std::lock_guard lock(mutex_);
    name_queries_.Requeue(guid.GetRawValue());
    return true;
  }

  if (response_type != 0) {
    return fail_name_query();
  }

  PlayerNameInfo info;
  info.guid = guid;
  if (!r.ReadCString(info.name, kNameCacheMaxNameBytes))
    return false;
  if (!r.ReadCString(info.realm_name, kNameCacheMaxRealmBytes))
    return false;
  if (!r.ReadU8(info.race))
    return false;
  if (!r.ReadU8(info.sex))
    return false;
  if (!r.ReadU8(info.class_id))
    return false;

  std::uint8_t declined;
  if (!r.ReadU8(declined))
    return false;
  info.declined = (declined != 0);

  if (info.declined) {
    for (auto &dn : info.declined_names) {
      if (!r.ReadCString(dn, kNameCacheMaxDeclinedBytes))
        return false;
    }
  }

  return finish_name_update(std::move(info));
}

bool QueryCache::HandleCreatureQueryResponse(const std::uint8_t *data,
                                             const std::size_t len) {
  return HandleCreatureQueryResponse(data, len, CacheUpdateOrigin::Network);
}

bool QueryCache::HandleCreatureQueryResponse(const std::uint8_t *data,
                                             const std::size_t len,
                                             const CacheUpdateOrigin origin,
                                             std::size_t *const consumed_bytes) {
  PacketReader r(data, len);

  std::uint32_t entry;
  if (!r.ReadU32(entry))
    return false;

  if ((entry & 0x80000000u) != 0 &&
      origin == CacheUpdateOrigin::Network) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    (void)InvalidateCreatureTemplate(entry & 0x7FFFFFFFu, true);
    return true;
  }
  if (entry == 0) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    return true;
  }

  CreatureTemplateInfo info;
  info.entry = entry;

  if (!r.ReadCString(info.name, kCreatureStringMaxBytes))
    return false;
  for (auto &alternate_name : info.alternate_names) {
    if (!r.ReadCString(alternate_name, kCreatureStringMaxBytes))
      return false;
  }
  if (!r.ReadCString(info.sub_name, kCreatureStringMaxBytes))
    return false;
  if (!r.ReadCString(info.icon_name, kCreatureStringMaxBytes))
    return false;
  if (!r.ReadU32(info.type_flags))
    return false;
  if (!r.ReadU32(info.creature_type))
    return false;
  if (!r.ReadU32(info.creature_family))
    return false;
  if (!r.ReadU32(info.rank))
    return false;
  for (auto &kc : info.kill_credit) {
    if (!r.ReadU32(kc))
      return false;
  }
  for (auto &did : info.display_ids) {
    if (!r.ReadU32(did))
      return false;
  }
  if (!r.ReadFloat(info.mod_health))
    return false;
  if (!r.ReadFloat(info.mod_mana))
    return false;
  if (!r.ReadU8(info.racial_leader))
    return false;
  for (auto &qi : info.quest_items) {
    if (!r.ReadU32(qi))
      return false;
  }
  if (!r.ReadU32(info.movement_id))
    return false;
  if (consumed_bytes != nullptr) {
    *consumed_bytes = r.Position();
  }

  if (origin == CacheUpdateOrigin::PersistentStorage) {
    std::lock_guard lock(mutex_);
    creature_templates_[entry] = std::move(info);
    return true;
  }

  std::vector<AsyncQueryChannel::Callback> callbacks;
  {
    std::lock_guard lock(mutex_);
    creature_templates_[entry] = std::move(info);
    callbacks = creature_queries_.Resolve(entry);
    active_creature_template_callback_drains_.push_back(
        ActiveQueryCallbackDrain{.entry = entry, .deferred_invalidation = false});
  }
  if (!StoreRetailWdbRecord(db_cache_runtime_,
                            openwow::data::WDBCacheType::Creature, entry,
                            openwow::data::wdb_format::kVersion_Creature,
                            data, r.Position())) {
    (void)InvalidateCreatureTemplate(entry, false);
  }
  for (auto &callback : callbacks) {
    callback(true);
  }
  {
    std::lock_guard lock(mutex_);
    (void)FinishUint32CallbackDrainLocked(
        creature_templates_, active_creature_template_callback_drains_, entry);
  }
  return true;
}

bool QueryCache::HandleGameObjectQueryResponse(const std::uint8_t *data,
                                               const std::size_t len) {
  return HandleGameObjectQueryResponse(data, len, CacheUpdateOrigin::Network);
}

bool QueryCache::HandleGameObjectQueryResponse(const std::uint8_t *data,
                                               const std::size_t len,
                                               const CacheUpdateOrigin origin,
                                               std::size_t *const consumed_bytes) {
  PacketReader r(data, len);

  std::uint32_t entry;
  if (!r.ReadU32(entry))
    return false;

  if ((entry & 0x80000000u) != 0 &&
      origin == CacheUpdateOrigin::Network) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    (void)InvalidateGameObjectTemplate(entry & 0x7FFFFFFFu, true);
    return true;
  }
  if (entry == 0) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    return true;
  }

  GameObjectTemplateInfo info;
  info.entry = entry;

  if (!r.ReadU32(info.type))
    return false;
  if (!r.ReadU32(info.display_id))
    return false;
  if (!r.ReadCString(info.name, kGameObjectStringMaxBytes))
    return false;

  for (auto &alternate_name : info.alternate_names) {
    if (!r.ReadCString(alternate_name, kGameObjectStringMaxBytes))
      return false;
  }
  if (!r.ReadCString(info.icon_name, kGameObjectStringMaxBytes))
    return false;
  if (!r.ReadCString(info.cast_bar_caption, kGameObjectStringMaxBytes))
    return false;
  if (!r.ReadCString(info.unk1, kGameObjectStringMaxBytes))
    return false;

  for (auto &rd : info.raw_data) {
    if (!r.ReadU32(rd))
      return false;
  }
  if (!r.ReadFloat(info.size))
    return false;
  for (auto &qi : info.quest_items) {
    if (!r.ReadU32(qi))
      return false;
  }
  if (consumed_bytes != nullptr) {
    *consumed_bytes = r.Position();
  }

  if (origin == CacheUpdateOrigin::PersistentStorage) {
    std::lock_guard lock(mutex_);
    go_templates_[entry] = std::move(info);
    return true;
  }

  std::vector<AsyncQueryChannel::Callback> callbacks;
  {
    std::lock_guard lock(mutex_);
    go_templates_[entry] = std::move(info);
    callbacks = gameobject_queries_.Resolve(entry);
    active_go_template_callback_drains_.push_back(
        ActiveQueryCallbackDrain{.entry = entry, .deferred_invalidation = false});
  }
  if (!StoreRetailWdbRecord(db_cache_runtime_,
                            openwow::data::WDBCacheType::GameObject, entry,
                            openwow::data::wdb_format::kVersion_GameObject,
                            data, r.Position())) {
    (void)InvalidateGameObjectTemplate(entry, false);
  }
  for (auto &callback : callbacks) {
    callback(true);
  }
  {
    std::lock_guard lock(mutex_);
    (void)FinishUint32CallbackDrainLocked(
        go_templates_, active_go_template_callback_drains_, entry);
  }
  return true;
}

bool QueryCache::HandleItemQuerySingleResponse(const std::uint8_t *data,
                                               const std::size_t len) {
  return HandleItemQuerySingleResponse(data, len, CacheUpdateOrigin::Network);
}

bool QueryCache::HandleItemQuerySingleResponse(const std::uint8_t *data,
                                               const std::size_t len,
                                               const CacheUpdateOrigin origin,
                                               std::size_t *const consumed_bytes) {
  PacketReader r(data, len);

  std::uint32_t entry;
  if (!r.ReadU32(entry))
    return false;

  if ((entry & 0x80000000u) != 0 &&
      origin == CacheUpdateOrigin::Network) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    (void)InvalidateItemTemplate(entry & 0x7FFFFFFFu, true);
    return true;
  }
  if (entry == 0) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    return true;
  }

  ItemTemplate info;
  info.entry = entry;

  {
    std::uint32_t value = 0;
    if (!r.ReadU32(value))
      return false;
    info.item_class = static_cast<ItemClass>(value);
  }
  if (!r.ReadU32(info.subclass))
    return false;
  {
    std::int32_t v;
    if (!r.ReadI32(v))
      return false;
    info.sound_override = v;
  }
  if (!r.ReadCString(info.name, kItemNameMaxBytes))
    return false;
  std::string skip;
  for (int i = 0; i < 3; ++i) {
    if (!r.ReadCString(skip, kItemNameMaxBytes))
      return false;
  }
  if (!r.ReadU32(info.display_id))
    return false;
  {
    std::uint32_t value = 0;
    if (!r.ReadU32(value))
      return false;
    info.quality = static_cast<ItemQuality>(value);
  }
  if (!r.ReadU32(info.flags))
    return false;
  if (!r.ReadU32(info.flags2))
    return false;
  if (!r.ReadU32(info.buy_price))
    return false;
  if (!r.ReadU32(info.sell_price))
    return false;
  {
    std::uint32_t value = 0;
    if (!r.ReadU32(value))
      return false;
    info.inventory_type = static_cast<InventoryType>(value);
  }
  {
    std::int32_t ac, ar;
    if (!r.ReadI32(ac))
      return false;
    if (!r.ReadI32(ar))
      return false;
    info.allowable_class = ac;
    info.allowable_race = ar;
  }
  if (!r.ReadU32(info.item_level))
    return false;
  if (!r.ReadU32(info.required_level))
    return false;
  if (!r.ReadU32(info.required_skill))
    return false;
  if (!r.ReadU32(info.required_skill_rank))
    return false;
  if (!r.ReadU32(info.required_spell))
    return false;

  if (!r.ReadU32(info.required_honor_rank))
    return false;
  if (!r.ReadU32(info.required_city_rank))
    return false;

  if (!r.ReadU32(info.required_reputation_faction))
    return false;
  if (!r.ReadU32(info.required_reputation_rank))
    return false;

  {
    std::int32_t value = 0;
    if (!r.ReadI32(value))
      return false;
    info.max_count = static_cast<std::uint32_t>(value);
  }
  {
    std::int32_t value = 0;
    if (!r.ReadI32(value))
      return false;
    info.stackable = static_cast<std::uint32_t>(value);
  }
  if (!r.ReadU32(info.container_slots))
    return false;

  std::uint32_t stats_count;
  if (!r.ReadU32(stats_count))
    return false;
  if (stats_count > kItemMaxStats)
    return false;
  for (std::uint32_t i = 0; i < stats_count; ++i) {
    if (!r.ReadU32(info.stats[i].type))
      return false;
    if (!r.ReadI32(info.stats[i].value))
      return false;
  }

  if (!r.ReadU32(info.scaling_stat_distribution))
    return false;
  if (!r.ReadU32(info.scaling_stat_value))
    return false;

  for (auto &dmg : info.damage) {
    if (!r.ReadFloat(dmg.min_damage))
      return false;
    if (!r.ReadFloat(dmg.max_damage))
      return false;
    if (!r.ReadU32(dmg.type))
      return false;
  }

  {
    std::uint32_t armor = 0;
    if (!r.ReadU32(armor))
      return false;
    info.armor = static_cast<std::int32_t>(armor);
  }
  std::array<std::int32_t*, 6> resistances{
      &info.holy_res, &info.fire_res,   &info.nature_res,
      &info.frost_res, &info.shadow_res, &info.arcane_res};
  for (auto* resistance : resistances) {
    std::uint32_t value = 0;
    if (!r.ReadU32(value))
      return false;
    *resistance = static_cast<std::int32_t>(value);
  }

  if (!r.ReadU32(info.delay))
    return false;
  if (!r.ReadU32(info.ammo_type))
    return false;
  if (!r.ReadFloat(info.range_mod))
    return false;

  for (auto &sp : info.spells) {
    if (!r.ReadU32(sp.spell_id))
      return false;
    if (!r.ReadU32(sp.trigger))
      return false;
    if (!r.ReadI32(sp.charges))
      return false;
    {
      std::uint32_t value = 0;
      if (!r.ReadU32(value))
        return false;
      sp.cooldown = static_cast<std::int32_t>(value);
    }
    if (!r.ReadU32(sp.category))
      return false;
    {
      std::uint32_t value = 0;
      if (!r.ReadU32(value))
        return false;
      sp.category_cooldown = static_cast<std::int32_t>(value);
    }
  }

  if (!r.ReadU32(info.bonding))
    return false;
  if (!r.ReadCString(info.description, kItemDescriptionMaxBytes))
    return false;

  if (!r.ReadU32(info.page_text))
    return false;
  if (!r.ReadU32(info.language_id))
    return false;
  if (!r.ReadU32(info.page_material))
    return false;

  if (!r.ReadU32(info.start_quest))
    return false;
  if (!r.ReadU32(info.lock_id))
    return false;
  if (!r.ReadI32(info.material))
    return false;
  if (!r.ReadU32(info.sheath))
    return false;

  if (!r.ReadU32(info.random_property))
    return false;
  if (!r.ReadU32(info.random_suffix))
    return false;
  if (!r.ReadU32(info.block))
    return false;

  if (!r.ReadU32(info.item_set))
    return false;
  if (!r.ReadU32(info.max_durability))
    return false;

  if (!r.ReadU32(info.area))
    return false;
  if (!r.ReadU32(info.map))
    return false;
  if (!r.ReadU32(info.bag_family))
    return false;
  if (!r.ReadU32(info.totem_category))
    return false;

  for (auto &s : info.sockets) {
    if (!r.ReadU32(s.color))
      return false;
    if (!r.ReadU32(s.content))
      return false;
  }
  if (!r.ReadU32(info.socket_bonus))
    return false;
  if (!r.ReadU32(info.gem_properties))
    return false;

  {
    std::uint32_t rds_u;
    if (!r.ReadU32(rds_u))
      return false;
    info.required_disenchant_skill = rds_u;
  }
  if (!r.ReadFloat(info.armor_damage_modifier))
    return false;
  if (!r.ReadU32(info.duration))
    return false;
  if (!r.ReadU32(info.item_limit_category))
    return false;
  if (!r.ReadU32(info.holiday_id))
    return false;
  if (consumed_bytes != nullptr) {
    *consumed_bytes = r.Position();
  }

  if (origin == CacheUpdateOrigin::PersistentStorage) {
    item_definitions_.CacheItem(entry, std::move(info));
    return true;
  }

  std::vector<AsyncQueryChannel::Callback> callbacks;
  {
    std::lock_guard lock(mutex_);
    callbacks = item_queries_.Resolve(entry);
    active_item_template_callback_drains_.push_back(
        ActiveQueryCallbackDrain{.entry = entry, .deferred_invalidation = false});
  }
  item_definitions_.CacheItem(entry, std::move(info));
  if (!StoreRetailWdbRecord(db_cache_runtime_,
                            openwow::data::WDBCacheType::Item, entry,
                            openwow::data::wdb_format::kVersion_Item, data,
                            r.Position())) {
    (void)InvalidateItemTemplate(entry, false);
  }
  for (auto &callback : callbacks) {
    callback(true);
  }
  bool deferred_item_invalidation = false;
  {
    std::lock_guard lock(mutex_);
    for (auto it = active_item_template_callback_drains_.rbegin();
         it != active_item_template_callback_drains_.rend(); ++it) {
      if (it->entry != entry) {
        continue;
      }
      deferred_item_invalidation = it->deferred_invalidation;
      active_item_template_callback_drains_.erase(std::next(it).base());
      break;
    }
  }
  if (deferred_item_invalidation) {
    (void)item_definitions_.InvalidateItem(entry);
  }
  return true;
}

bool QueryCache::HandleItemQueryMultipleResponse(
    const std::uint8_t *data, const std::size_t len,
    std::function<void(std::uint32_t response_entry)> on_processed) {
  PacketReader packet(data, len);
  std::uint8_t response_count = 0;
  if (!packet.ReadU8(response_count)) {
    return false;
  }

  for (std::uint16_t index = 0; index < response_count; ++index) {
    const auto record_offset = packet.Position();
    const auto record_length = packet.Remaining();

    PacketReader key_reader(data + record_offset, record_length);
    std::uint32_t response_entry = 0;
    if (!key_reader.ReadU32(response_entry)) {
      return false;
    }

    std::size_t consumed_bytes = 0;
    if (!HandleItemQuerySingleResponse(
            data + record_offset, record_length, CacheUpdateOrigin::Network,
            &consumed_bytes) ||
        consumed_bytes < sizeof(std::uint32_t) ||
        consumed_bytes > record_length) {
      return false;
    }

    packet.Skip(consumed_bytes);
    if (on_processed) {
      on_processed(response_entry);
    }
  }
  return true;
}

const PlayerNameInfo *QueryCache::GetPlayerName(std::uint64_t raw_guid) const {
  std::lock_guard lock(mutex_);
  return player_names_.FindValue(raw_guid);
}

std::optional<ObjectGuid> QueryCache::FindPlayerGuidByName(
    const std::string_view name) const {
  if (name.empty()) {
    return std::nullopt;
  }

  const std::string query(name);
  const auto hash = openwow::core::SStrHashCI(query.c_str());

  std::lock_guard lock(mutex_);
  const auto *const candidates = player_name_hash_index_.FindValue(hash);
  if (candidates == nullptr) {
    return std::nullopt;
  }

  std::optional<ObjectGuid> best_guid;
  std::uint64_t best_serial = std::numeric_limits<std::uint64_t>::max();
  for (const std::uint64_t candidate_guid : *candidates) {
    const PlayerNameInfo *const entry = player_names_.FindValue(candidate_guid);
    if (entry == nullptr) {
      continue;
    }
    if (openwow::core::SStrCmpUTF8NoCase(entry->name.c_str(), query.c_str(),
                                         0x7FFFFFFFu) != 0) {
      continue;
    }

    const std::uint64_t *const stored_serial =
        player_name_persistence_serials_.FindValue(candidate_guid);
    const auto serial = stored_serial != nullptr
                            ? *stored_serial
                            : std::numeric_limits<std::uint64_t>::max();
    if (!best_guid.has_value() || serial < best_serial) {
      best_guid = ObjectGuid(candidate_guid);
      best_serial = serial;
    }
  }

  return best_guid;
}

const PlayerNameInfo *QueryCache::GetOrRequestPlayerName(std::uint64_t raw_guid) {
  return GetOrRequestPlayerName(raw_guid, QueryRequestOptions{});
}

const PlayerNameInfo *QueryCache::GetOrRequestPlayerName(std::uint64_t raw_guid,
                                                         QueryRequestOptions options) {
  if (raw_guid == 0) {
    return nullptr;
  }

  std::lock_guard lock(mutex_);
  if (const PlayerNameInfo *const known = player_names_.FindValue(raw_guid)) {
    return known;
  }

  name_queries_.Request(raw_guid, tick_count_provider_(),
      AsyncGuidQueryChannel::RequestOptions{
          .context = options.context,
          .callback_key = AsyncGuidQueryChannel::CallbackKey(
              options.callback_key.function_id, options.callback_key.cookie),
          .dedupe_callbacks = options.dedupe_callbacks,
          .callback = std::move(options.callback)});
  return nullptr;
}

const CreatureTemplateInfo *QueryCache::GetCreatureTemplate(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return creature_templates_.FindValue(entry);
}

const CreatureTemplateInfo *QueryCache::GetOrRequestCreatureTemplate(std::uint32_t entry,
                                                                     std::uint64_t context_guid) {
  return GetOrRequestCreatureTemplate(entry, QueryRequestOptions{.context = context_guid});
}

const CreatureTemplateInfo *QueryCache::GetOrRequestCreatureTemplate(std::uint32_t entry,
                                                                     QueryRequestOptions options) {
  if (entry == 0) {
    return nullptr;
  }

  std::lock_guard lock(mutex_);
  if (const CreatureTemplateInfo *const known = creature_templates_.FindValue(entry)) {
    return known;
  }

  creature_queries_.Request(entry, tick_count_provider_(), std::move(options));
  return nullptr;
}

const GameObjectTemplateInfo *QueryCache::GetGameObjectTemplate(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return go_templates_.FindValue(entry);
}

const GameObjectTemplateInfo *
QueryCache::GetOrRequestGameObjectTemplate(std::uint32_t entry, std::uint64_t context_guid) {
  return GetOrRequestGameObjectTemplate(entry, QueryRequestOptions{.context = context_guid});
}

const GameObjectTemplateInfo *
QueryCache::GetOrRequestGameObjectTemplate(std::uint32_t entry, QueryRequestOptions options) {
  if (entry == 0) {
    return nullptr;
  }

  std::lock_guard lock(mutex_);
  if (const GameObjectTemplateInfo *const known = go_templates_.FindValue(entry)) {
    return known;
  }

  gameobject_queries_.Request(entry, tick_count_provider_(), std::move(options));
  return nullptr;
}

const ItemTemplate *QueryCache::GetItemTemplate(std::uint32_t entry) const {
  return item_definitions_.GetItem(entry);
}

const ItemTemplate *QueryCache::GetOrRequestItemTemplate(std::uint32_t entry) {
  return GetOrRequestItemTemplate(entry, QueryRequestOptions{});
}

const ItemTemplate *QueryCache::GetOrRequestItemTemplate(
    std::uint32_t entry, QueryRequestOptions options) {
  if (entry == 0) {
    return nullptr;
  }

  if (const auto* item = item_definitions_.GetItem(entry)) {
    return item;
  }

  std::lock_guard lock(mutex_);
  item_queries_.Request(entry, tick_count_provider_(), std::move(options));
  return nullptr;
}

const ItemTemplate *QueryCache::GetItemTemplateByName(
    const std::string &name) const {
  return item_definitions_.FindByName(name);
}

const NpcTextInfo *QueryCache::GetNpcText(std::uint32_t text_id) const {
  std::lock_guard lock(mutex_);
  return npc_texts_.FindValue(text_id);
}

bool QueryCache::HasPlayerName(std::uint64_t raw_guid) const {
  std::lock_guard lock(mutex_);
  return player_names_.contains(raw_guid);
}

bool QueryCache::IsNameQueryPending(std::uint64_t raw_guid) const {
  std::lock_guard lock(mutex_);
  return name_queries_.IsPending(raw_guid);
}

bool QueryCache::InvalidatePlayerName(std::uint64_t raw_guid) {
  std::vector<QueryCallback> callbacks;
  {
    std::lock_guard lock(mutex_);

    for (auto it = active_name_query_callback_drains_.rbegin();
         it != active_name_query_callback_drains_.rend(); ++it) {
      if (it->guid != raw_guid) {
        continue;
      }

      it->deferred_invalidation = true;
      return true;
    }

    if (player_names_.contains(raw_guid)) {
      ErasePlayerNameLocked(raw_guid);
      return true;
    }

    if (!name_queries_.IsPending(raw_guid)) {
      return false;
    }

    callbacks = ResolveNameQueryCallbacksLocked(raw_guid, true);
  }

  for (auto &callback : callbacks) {
    callback(false);
  }

  {
    std::lock_guard lock(mutex_);
    FinishNameQueryCallbackDrainLocked(raw_guid);
  }
  return true;
}

bool QueryCache::HasCreatureTemplate(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return creature_templates_.contains(entry);
}

bool QueryCache::HasGameObjectTemplate(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return go_templates_.contains(entry);
}

bool QueryCache::HasItemTemplate(std::uint32_t entry) const {
  return item_definitions_.HasItem(entry);
}

bool QueryCache::HasNpcText(std::uint32_t text_id) const {
  std::lock_guard lock(mutex_);
  return npc_texts_.contains(text_id);
}

bool QueryCache::InvalidateCreatureTemplate(const std::uint32_t entry) {
  return InvalidateCreatureTemplate(entry, true);
}

bool QueryCache::InvalidateCreatureTemplate(const std::uint32_t entry,
                                            const bool update_wdb_owner) {
  if (entry == 0) {
    return false;
  }

  std::vector<QueryCallback> callbacks;
  bool started_callback_drain = false;
  bool deferred_to_active_drain = false;
  bool decoded_invalidated = false;
  {
    std::lock_guard lock(mutex_);
    decoded_invalidated = BeginUint32InvalidationLocked(
        creature_templates_, creature_queries_,
        active_creature_template_callback_drains_, entry, callbacks,
        started_callback_drain, deferred_to_active_drain);
  }
  (void)deferred_to_active_drain;

  const bool wdb_invalidated =
      update_wdb_owner &&
      InvalidateRetailWdbRecord(
          db_cache_runtime_, openwow::data::WDBCacheType::Creature, entry);
  for (auto &callback : callbacks) {
    callback(false);
  }
  if (started_callback_drain) {
    std::lock_guard lock(mutex_);
    (void)FinishUint32CallbackDrainLocked(
        creature_templates_, active_creature_template_callback_drains_, entry);
  }
  return decoded_invalidated || wdb_invalidated;
}

bool QueryCache::InvalidateItemTemplate(const std::uint32_t entry) {
  return InvalidateItemTemplate(entry, true);
}

bool QueryCache::InvalidateItemTemplate(const std::uint32_t entry,
                                        const bool update_wdb_owner) {
  if (entry == 0) {
    return false;
  }

  std::vector<QueryCallback> callbacks;
  bool started_callback_drain = false;
  bool deferred_to_active_drain = false;
  {
    std::lock_guard lock(mutex_);
    for (auto it = active_item_template_callback_drains_.rbegin();
         it != active_item_template_callback_drains_.rend(); ++it) {
      if (it->entry != entry) {
        continue;
      }
      it->deferred_invalidation = true;
      deferred_to_active_drain = true;
      break;
    }
    if (!deferred_to_active_drain && item_queries_.IsPending(entry)) {
      callbacks = item_queries_.Resolve(entry);
      active_item_template_callback_drains_.push_back(
          ActiveQueryCallbackDrain{.entry = entry,
                                   .deferred_invalidation = false});
      started_callback_drain = true;
    }
  }

  bool item_definitions_invalidated = false;
  if (!deferred_to_active_drain) {
    item_definitions_invalidated = item_definitions_.InvalidateItem(entry);
  }
  const bool wdb_invalidated =
      update_wdb_owner &&
      InvalidateRetailWdbRecord(
          db_cache_runtime_, openwow::data::WDBCacheType::Item, entry);
  for (auto &callback : callbacks) {
    callback(false);
  }
  if (started_callback_drain) {
    bool deferred_during_callbacks = false;
    {
      std::lock_guard lock(mutex_);
      for (auto it = active_item_template_callback_drains_.rbegin();
           it != active_item_template_callback_drains_.rend(); ++it) {
        if (it->entry != entry) {
          continue;
        }
        deferred_during_callbacks = it->deferred_invalidation;
        active_item_template_callback_drains_.erase(std::next(it).base());
        break;
      }
    }
    if (deferred_during_callbacks) {
      item_definitions_invalidated = item_definitions_.InvalidateItem(entry) ||
                               item_definitions_invalidated;
    }
  }
  return deferred_to_active_drain || started_callback_drain ||
         item_definitions_invalidated || wdb_invalidated;
}

bool QueryCache::InvalidateGameObjectTemplate(const std::uint32_t entry) {
  return InvalidateGameObjectTemplate(entry, true);
}

bool QueryCache::InvalidateGameObjectTemplate(const std::uint32_t entry,
                                              const bool update_wdb_owner) {
  if (entry == 0) {
    return false;
  }

  std::vector<QueryCallback> callbacks;
  bool started_callback_drain = false;
  bool deferred_to_active_drain = false;
  bool decoded_invalidated = false;
  {
    std::lock_guard lock(mutex_);
    decoded_invalidated = BeginUint32InvalidationLocked(
        go_templates_, gameobject_queries_, active_go_template_callback_drains_,
        entry, callbacks, started_callback_drain,
        deferred_to_active_drain);
  }
  (void)deferred_to_active_drain;

  const bool wdb_invalidated =
      update_wdb_owner && InvalidateRetailWdbRecord(
                              db_cache_runtime_,
                              openwow::data::WDBCacheType::GameObject, entry);
  for (auto &callback : callbacks) {
    callback(false);
  }
  if (started_callback_drain) {
    std::lock_guard lock(mutex_);
    (void)FinishUint32CallbackDrainLocked(
        go_templates_, active_go_template_callback_drains_, entry);
  }
  return decoded_invalidated || wdb_invalidated;
}

bool QueryCache::InvalidateNpcText(const std::uint32_t text_id,
                                   const bool update_wdb_owner) {
  if (text_id == 0) {
    return false;
  }

  std::vector<QueryCallback> callbacks;
  bool started_callback_drain = false;
  bool deferred_to_active_drain = false;
  bool decoded_invalidated = false;
  {
    std::lock_guard lock(mutex_);
    decoded_invalidated = BeginUint32InvalidationLocked(
        npc_texts_, npc_text_queries_, active_npc_text_callback_drains_,
        text_id, callbacks, started_callback_drain,
        deferred_to_active_drain);
  }
  (void)deferred_to_active_drain;

  const bool wdb_invalidated =
      update_wdb_owner &&
      InvalidateRetailWdbRecord(
          db_cache_runtime_, openwow::data::WDBCacheType::NpcText, text_id);
  for (auto &callback : callbacks) {
    callback(false);
  }
  if (started_callback_drain) {
    std::lock_guard lock(mutex_);
    (void)FinishUint32CallbackDrainLocked(
        npc_texts_, active_npc_text_callback_drains_, text_id);
  }
  return decoded_invalidated || wdb_invalidated;
}

void QueryCache::CancelCreatureTemplateCallback(
    const std::uint32_t entry, const CallbackKey key) {
  std::lock_guard lock(mutex_);
  creature_queries_.CancelCallback(entry, key);
}

void QueryCache::CancelGameObjectTemplateCallback(
    const std::uint32_t entry, const CallbackKey key) {
  std::lock_guard lock(mutex_);
  gameobject_queries_.CancelCallback(entry, key);
}

void QueryCache::CancelItemTemplateCallback(
    const std::uint32_t entry, const CallbackKey key) {
  std::lock_guard lock(mutex_);
  item_queries_.CancelCallback(entry, key);
}

void QueryCache::CancelItemTemplateCallbacks(const CallbackKey key) {
  std::lock_guard lock(mutex_);
  item_queries_.CancelCallbacks(key);
}

bool QueryCache::HasNameQueryDispatcher() const {
  std::lock_guard lock(mutex_);
  return name_queries_.HasDispatcher();
}

std::size_t QueryCache::GetPlayerNameCount() const {
  std::lock_guard lock(mutex_);
  return player_names_.size();
}

std::size_t QueryCache::GetCreatureCount() const {
  std::lock_guard lock(mutex_);
  return creature_templates_.size();
}

std::size_t QueryCache::GetGameObjectCount() const {
  std::lock_guard lock(mutex_);
  return go_templates_.size();
}

std::size_t QueryCache::GetItemCount() const {
  return item_definitions_.GetCacheSize();
}

std::size_t QueryCache::GetNpcTextCount() const {
  std::lock_guard lock(mutex_);
  return npc_texts_.size();
}

bool QueryCache::RequestNameQuery(std::uint64_t raw_guid) {
  if (raw_guid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  if (player_names_.contains(raw_guid) || name_queries_.IsPending(raw_guid)) {
    return false;
  }

  if (name_queries_.HasDispatcher()) {
    name_queries_.Request(raw_guid, tick_count_provider_());
  } else {
    name_queries_.MarkPending(raw_guid, tick_count_provider_());
  }
  return true;
}

void QueryCache::MarkItemQueryPending(std::uint32_t entry) {
  std::lock_guard lock(mutex_);
  item_queries_.MarkPending(entry, tick_count_provider_());
}

bool QueryCache::MarkNameQueryPending(std::uint64_t raw_guid) {
  if (raw_guid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  if (player_names_.contains(raw_guid) || name_queries_.IsPending(raw_guid)) {
    return false;
  }

  name_queries_.MarkPending(raw_guid, tick_count_provider_());
  return true;
}

void QueryCache::MarkCreatureQueryPending(std::uint32_t entry) {
  std::lock_guard lock(mutex_);
  creature_queries_.MarkPending(entry, tick_count_provider_());
}

void QueryCache::MarkGameObjectQueryPending(std::uint32_t entry) {
  std::lock_guard lock(mutex_);
  gameobject_queries_.MarkPending(entry, tick_count_provider_());
}

void QueryCache::MarkNpcTextQueryPending(std::uint32_t text_id) {
  std::lock_guard lock(mutex_);
  npc_text_queries_.MarkPending(text_id, tick_count_provider_());
}

bool QueryCache::IsItemQueryPending(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return item_queries_.IsPending(entry);
}

bool QueryCache::IsCreatureQueryPending(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return creature_queries_.IsPending(entry);
}

bool QueryCache::IsGameObjectQueryPending(std::uint32_t entry) const {
  std::lock_guard lock(mutex_);
  return gameobject_queries_.IsPending(entry);
}

bool QueryCache::IsNpcTextQueryPending(std::uint32_t text_id) const {
  std::lock_guard lock(mutex_);
  return npc_text_queries_.IsPending(text_id);
}

bool QueryCache::HandleNpcTextUpdate(const std::uint8_t *data,
                                     const std::size_t len) {
  return HandleNpcTextUpdate(data, len, CacheUpdateOrigin::Network);
}

bool QueryCache::HandleNpcTextUpdate(const std::uint8_t *data,
                                     const std::size_t len,
                                     const CacheUpdateOrigin origin,
                                     std::size_t *const consumed_bytes) {
  PacketReader r(data, len);
  std::uint32_t text_id;
  if (!r.ReadU32(text_id))
    return false;

  if ((text_id & 0x80000000u) != 0 &&
      origin == CacheUpdateOrigin::Network) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    (void)InvalidateNpcText(text_id & 0x7FFFFFFFu, true);
    return true;
  }
  if (text_id == 0) {
    if (consumed_bytes != nullptr) {
      *consumed_bytes = r.Position();
    }
    return true;
  }

  NpcTextInfo info;
  info.text_id = text_id;

  for (int i = 0; i < 8; ++i) {
    auto &blk = info.blocks[static_cast<std::size_t>(i)];
    if (!r.ReadFloat(blk.probability))
      return false;
    if (!r.ReadCString(blk.text_male, kNpcTextStringMaxBytes))
      return false;
    if (!r.ReadCString(blk.text_female, kNpcTextStringMaxBytes))
      return false;
    if (!r.ReadU32(blk.language))
      return false;

    for (int e = 0; e < 6; ++e) {
      if (!r.ReadU32(blk.emotes[static_cast<std::size_t>(e)]))
        return false;
    }
  }
  if (consumed_bytes != nullptr) {
    *consumed_bytes = r.Position();
  }

  if (origin == CacheUpdateOrigin::PersistentStorage) {
    std::lock_guard lock(mutex_);
    npc_texts_[text_id] = std::move(info);
    return true;
  }

  std::vector<AsyncQueryChannel::Callback> callbacks;
  {
    std::lock_guard lock(mutex_);
    npc_texts_[text_id] = std::move(info);
    callbacks = npc_text_queries_.Resolve(text_id);
    active_npc_text_callback_drains_.push_back(
        ActiveQueryCallbackDrain{.entry = text_id, .deferred_invalidation = false});
  }
  if (!StoreRetailWdbRecord(db_cache_runtime_,
                            openwow::data::WDBCacheType::NpcText, text_id,
                            openwow::data::wdb_format::kVersion_NpcText, data,
                            r.Position())) {
    (void)InvalidateNpcText(text_id, false);
  }
  for (auto &callback : callbacks) {
    callback(true);
  }
  {
    std::lock_guard lock(mutex_);
    (void)FinishUint32CallbackDrainLocked(
        npc_texts_, active_npc_text_callback_drains_, text_id);
  }
  return true;
}

bool QueryCache::HydrateRetailWdbCaches(openwow::data::WDBCache &cache) {
  const auto hydrate_type =
      [&cache](const openwow::data::WDBCacheType type,
               auto &&decode_record, auto &&clear_decoded_cache) {
        std::vector<std::uint8_t> packet;
        for (const auto entry_id : cache.GetKeysInPersistenceOrder(type)) {
          const auto record = cache.Get(type, entry_id);
          if (entry_id == 0 || !record.has_value()) {
            cache.ClearType(type);
            clear_decoded_cache();
            return false;
          }

          BuildWdbHydrationPacket(entry_id, record->data, packet);
          std::size_t consumed_bytes = 0;
          if (!decode_record(packet, consumed_bytes) ||
              consumed_bytes < sizeof(std::uint32_t) ||
              consumed_bytes > packet.size()) {

            cache.ClearType(type);
            clear_decoded_cache();
            return false;
          }

          if (consumed_bytes != packet.size()) {

            std::vector<std::uint8_t> canonical_payload(
                packet.begin() + static_cast<std::ptrdiff_t>(sizeof(std::uint32_t)),
                packet.begin() + static_cast<std::ptrdiff_t>(consumed_bytes));
            cache.Insert(type, entry_id, std::move(canonical_payload),
                         record->version);
          }
        }
        return true;
      };

  bool all_ok = true;
  all_ok = hydrate_type(
               openwow::data::WDBCacheType::Creature,
               [this](const std::vector<std::uint8_t> &packet,
                      std::size_t &consumed_bytes) {
                 return HandleCreatureQueryResponse(
                     packet.data(), packet.size(),
                     CacheUpdateOrigin::PersistentStorage, &consumed_bytes);
               },
               [this]() { ClearCreatureEntriesForClientCacheVersion(); }) &&
           all_ok;
  all_ok = hydrate_type(
               openwow::data::WDBCacheType::GameObject,
               [this](const std::vector<std::uint8_t> &packet,
                      std::size_t &consumed_bytes) {
                 return HandleGameObjectQueryResponse(
                     packet.data(), packet.size(),
                     CacheUpdateOrigin::PersistentStorage, &consumed_bytes);
               },
               [this]() { ClearGameObjectEntriesForClientCacheVersion(); }) &&
           all_ok;
  all_ok = hydrate_type(
               openwow::data::WDBCacheType::Item,
               [this](const std::vector<std::uint8_t> &packet,
                      std::size_t &consumed_bytes) {
                 return HandleItemQuerySingleResponse(
                     packet.data(), packet.size(),
                     CacheUpdateOrigin::PersistentStorage, &consumed_bytes);
               },
               [this]() { ClearItemEntriesForClientCacheVersion(); }) &&
           all_ok;
  all_ok = hydrate_type(
               openwow::data::WDBCacheType::NpcText,
               [this](const std::vector<std::uint8_t> &packet,
                      std::size_t &consumed_bytes) {
                 return HandleNpcTextUpdate(
                     packet.data(), packet.size(),
                     CacheUpdateOrigin::PersistentStorage, &consumed_bytes);
               },
               [this]() { ClearNpcTextEntriesForClientCacheVersion(); }) &&
           all_ok;
  return all_ok;
}

void QueryCache::SetNameQueryDispatcher(NameQueryDispatchFn dispatcher) {
  std::lock_guard lock(mutex_);
  if (!dispatcher) {
    name_queries_.SetDispatcher({});
    return;
  }

  name_queries_.SetDispatcher([dispatcher = std::move(dispatcher)](const std::uint64_t raw_guid,
                                                                   const std::uint64_t ) {
    dispatcher(raw_guid);
  });
}

void QueryCache::SetCreatureQueryDispatcher(QueryDispatchFn dispatcher) {
  std::lock_guard lock(mutex_);
  creature_queries_.SetDispatcher(std::move(dispatcher));
}

void QueryCache::SetGameObjectQueryDispatcher(QueryDispatchFn dispatcher) {
  std::lock_guard lock(mutex_);
  gameobject_queries_.SetDispatcher(std::move(dispatcher));
}

void QueryCache::SetItemQueryDispatcher(QueryDispatchFn dispatcher) {
  std::lock_guard lock(mutex_);
  item_queries_.SetDispatcher(std::move(dispatcher));
}

void QueryCache::SetTickCountProvider(std::function<std::uint32_t()> provider) {
  std::lock_guard lock(mutex_);
  tick_count_provider_ = std::move(provider);
  if (!tick_count_provider_) {
    tick_count_provider_ = []() { return openwow::core::GameClock::GetTickCount32(); };
  }
}

void QueryCache::PumpDispatchQueues(std::uint32_t current_tick_ms) {
  std::lock_guard lock(mutex_);
  creature_queries_.Pump(current_tick_ms);
  gameobject_queries_.Pump(current_tick_ms);
  item_queries_.Pump(current_tick_ms);
  npc_text_queries_.Pump(current_tick_ms);
  name_queries_.Pump(current_tick_ms);
}

void QueryCache::SetNameQueryMaxInFlight(std::uint32_t max_in_flight) {
  std::lock_guard lock(mutex_);
  name_queries_.SetMaxInFlight(max_in_flight);
}

void QueryCache::SetCreatureQueryMaxInFlight(std::uint32_t max_in_flight) {
  std::lock_guard lock(mutex_);
  creature_queries_.SetMaxInFlight(max_in_flight);
}

void QueryCache::SetGameObjectQueryMaxInFlight(std::uint32_t max_in_flight) {
  std::lock_guard lock(mutex_);
  gameobject_queries_.SetMaxInFlight(max_in_flight);
}

void QueryCache::SetItemQueryMaxInFlight(std::uint32_t max_in_flight) {
  std::lock_guard lock(mutex_);
  item_queries_.SetMaxInFlight(max_in_flight);
}

net::wotlk::WorldPacket QueryCache::BuildNameQuery(std::uint64_t guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_NAME_QUERY);
  pkt.AppendU64(guid);
  return pkt;
}

net::wotlk::WorldPacket QueryCache::BuildCreatureQuery(std::uint32_t entry, std::uint64_t guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_CREATURE_QUERY);
  pkt.AppendU32(entry);
  pkt.AppendU64(guid);
  return pkt;
}

net::wotlk::WorldPacket QueryCache::BuildGameObjectQuery(std::uint32_t entry, std::uint64_t guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_GAMEOBJECT_QUERY);
  pkt.AppendU32(entry);
  pkt.AppendU64(guid);
  return pkt;
}

net::wotlk::WorldPacket QueryCache::BuildItemQuery(std::uint32_t entry) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ITEM_QUERY_SINGLE);
  pkt.AppendU32(entry);
  return pkt;
}

net::wotlk::WorldPacket QueryCache::BuildNpcTextQuery(std::uint32_t text_id,
                                                      std::uint64_t npc_guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_NPC_TEXT_QUERY);
  pkt.AppendU32(text_id);
  pkt.AppendU64(npc_guid);
  return pkt;
}

net::wotlk::WorldPacket QueryCache::BuildPageTextQuery(std::uint32_t page_text_id) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_PAGE_TEXT_QUERY);
  pkt.AppendU32(page_text_id);
  return pkt;
}

namespace {

void AppendU32LE(std::vector<std::uint8_t> &out, const std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void AppendU64LE(std::vector<std::uint8_t> &out, const std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
  }
}

void AppendCStringBytes(std::vector<std::uint8_t> &out, const std::string &value) {
  out.insert(out.end(), value.begin(), value.end());
  out.push_back(0);
}

[[nodiscard]] bool ReadU32LE(const std::vector<std::uint8_t> &data, std::size_t &pos,
                             std::uint32_t &value) {
  if (pos + 4 > data.size()) {
    return false;
  }
  value = static_cast<std::uint32_t>(data[pos]) | (static_cast<std::uint32_t>(data[pos + 1]) << 8) |
          (static_cast<std::uint32_t>(data[pos + 2]) << 16) |
          (static_cast<std::uint32_t>(data[pos + 3]) << 24);
  pos += 4;
  return true;
}

[[nodiscard]] bool ReadU64LE(const std::vector<std::uint8_t> &data, std::size_t &pos,
                             std::uint64_t &value) {
  if (pos + 8 > data.size()) {
    return false;
  }
  value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(data[pos + i]) << (i * 8);
  }
  pos += 8;
  return true;
}

[[nodiscard]] bool ReadCStringBounded(const std::vector<std::uint8_t> &data, std::size_t &pos,
                                      const std::size_t max_bytes, std::string &value) {
  const std::size_t end = data.size();
  const std::size_t start = pos;
  while (pos < end && data[pos] != 0) {
    ++pos;
  }
  if (pos >= end) {
    return false;
  }
  const std::size_t byte_count = pos - start;
  if (byte_count + 1 > max_bytes) {
    return false;
  }
  value.assign(reinterpret_cast<const char *>(data.data() + start), byte_count);
  ++pos;
  return true;
}

[[nodiscard]] bool IsPersistedNameCachePlaceholder(const PlayerNameInfo &info) {
  return info.name == "?" && info.realm_name.empty() && info.race == 0 && info.sex == 0 &&
         info.class_id == 0 && !info.declined;
}

[[nodiscard]] bool IsNpcNameCacheGuid(const std::uint64_t raw_guid) {
  const auto high = static_cast<std::uint32_t>(raw_guid >> 32);
  return (high & 0xF0000000u) == 0x10000000u && (high & 0x0FF00000u) == 0x0FD00000u;
}

[[nodiscard]] bool ShouldPersistNameCacheEntry(const PlayerNameInfo &info) {
  return !IsPersistedNameCachePlaceholder(info) && !IsNpcNameCacheGuid(info.guid.GetRawValue());
}

[[nodiscard]] std::vector<std::uint8_t> SerializePlayerNameCacheRecord(const PlayerNameInfo &info) {
  std::vector<std::uint8_t> payload;
  AppendU64LE(payload, info.guid.GetRawValue());
  AppendCStringBytes(payload, info.name);
  payload.push_back(info.declined ? 1 : 0);
  if (info.declined) {
    for (const auto &declined_name : info.declined_names) {
      AppendCStringBytes(payload, declined_name);
    }
  }
  AppendCStringBytes(payload, info.realm_name);
  AppendU32LE(payload, info.race);
  AppendU32LE(payload, info.sex);
  AppendU32LE(payload, info.class_id);
  return payload;
}

[[nodiscard]] bool DeserializePlayerNameCacheRecord(const std::vector<std::uint8_t> &data,
                                                    PlayerNameInfo &info) {
  std::size_t pos = 0;
  std::uint64_t raw_guid = 0;
  if (!ReadU64LE(data, pos, raw_guid)) {
    return false;
  }
  info = PlayerNameInfo{};
  info.guid = ObjectGuid(raw_guid);
  if (!ReadCStringBounded(data, pos, kNameCacheMaxNameBytes, info.name)) {
    return false;
  }

  if (pos >= data.size()) {
    return false;
  }
  const std::uint8_t has_declined = data[pos++];
  info.declined = has_declined != 0;
  if (info.declined) {
    for (auto &declined_name : info.declined_names) {
      if (!ReadCStringBounded(data, pos, kNameCacheMaxDeclinedBytes, declined_name)) {
        return false;
      }
    }
  }

  if (!ReadCStringBounded(data, pos, kNameCacheMaxRealmBytes, info.realm_name)) {
    return false;
  }

  std::uint32_t race = 0;
  std::uint32_t sex = 0;
  std::uint32_t class_id = 0;
  if (!ReadU32LE(data, pos, race) || !ReadU32LE(data, pos, sex) ||
      !ReadU32LE(data, pos, class_id)) {
    return false;
  }
  info.race = static_cast<std::uint8_t>(race);
  info.sex = static_cast<std::uint8_t>(sex);
  info.class_id = static_cast<std::uint8_t>(class_id);
  return true;
}

}

bool QueryCache::LoadPlayerNameCacheWdb(const std::filesystem::path &cache_dir,
                                        const std::uint32_t locale_id) {
  const auto path = cache_dir / "namecache.wdb";
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return true;
  }

  const auto end_pos = file.tellg();
  if (end_pos < 0) {
    return false;
  }
  const auto file_size =
      static_cast<std::uintmax_t>(static_cast<std::streamoff>(end_pos));
  if (file_size > std::numeric_limits<std::size_t>::max() ||
      file_size > static_cast<std::uintmax_t>(
                      std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  file.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> data(static_cast<std::size_t>(file_size));
  if (!data.empty() &&
      !file.read(reinterpret_cast<char *>(data.data()),
                 static_cast<std::streamsize>(data.size()))) {
    return false;
  }

  if (data.size() < 24) {
    return false;
  }

  std::size_t pos = 0;
  if (std::memcmp(data.data(), openwow::data::wdb_magic::kName,
                  sizeof(openwow::data::wdb_magic::kName)) != 0) {
    return false;
  }
  pos += 4;

  std::uint32_t build = 0;
  std::uint32_t locale = 0;
  std::uint32_t record_size = 0;
  std::uint32_t version = 0;
  std::uint32_t cache_version_token = 0;
  if (!ReadU32LE(data, pos, build) || !ReadU32LE(data, pos, locale) ||
      !ReadU32LE(data, pos, record_size) || !ReadU32LE(data, pos, version) ||
      !ReadU32LE(data, pos, cache_version_token)) {
    return false;
  }
  if (build != openwow::data::wdb_format::kBuild_335a || locale != locale_id ||
      record_size != openwow::data::wdb_format::kRecordSize_Name ||
      version != openwow::data::wdb_format::kVersion_Name) {
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    player_name_cache_version_token_ = cache_version_token;
  }

  const auto clear_corrupt_cache = [this]() {
    std::lock_guard lock(mutex_);
    ClearPlayerNameCacheLocked();
  };

  std::vector<std::pair<std::uint64_t, PlayerNameInfo>> loaded_entries;

  while (true) {
    std::uint64_t outer_guid = 0;
    if (!ReadU64LE(data, pos, outer_guid)) {
      clear_corrupt_cache();
      return false;
    }

    std::uint32_t payload_size = 0;
    if (!ReadU32LE(data, pos, payload_size)) {
      clear_corrupt_cache();
      return false;
    }

    if (outer_guid == 0) {
      break;
    }
    if (payload_size > data.size() - pos) {
      clear_corrupt_cache();
      return false;
    }

    std::vector<std::uint8_t> payload(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                      data.begin() +
                                          static_cast<std::ptrdiff_t>(pos + payload_size));
    pos += payload_size;

    PlayerNameInfo info;
    if (!DeserializePlayerNameCacheRecord(payload, info)) {
      clear_corrupt_cache();
      return false;
    }

    info.guid = ObjectGuid(outer_guid);
    loaded_entries.emplace_back(outer_guid, std::move(info));
  }

  std::lock_guard lock(mutex_);

  for (auto &[raw_guid, info] : loaded_entries) {
    auto existing = player_names_.find(raw_guid);
    if (existing != player_names_.end()) {
      UnindexPlayerNameEntry(player_name_hash_index_, existing->second);
      existing->second = std::move(info);
      IndexPlayerNameEntry(player_name_hash_index_, existing->second);
      continue;
    }

    const auto [inserted, was_inserted] =
        player_names_.emplace(raw_guid, std::move(info));
    if (was_inserted) {
      player_name_persistence_serials_[raw_guid] =
          next_player_name_persistence_serial_++;
      IndexPlayerNameEntry(player_name_hash_index_, inserted->second);
    }
  }
  return true;
}

bool QueryCache::SavePlayerNameCacheWdb(const std::filesystem::path &cache_dir,
                                        const std::uint32_t locale_id) const {
  std::vector<std::uint8_t> file_data;
  file_data.insert(file_data.end(), std::begin(openwow::data::wdb_magic::kName),
                   std::end(openwow::data::wdb_magic::kName));
  AppendU32LE(file_data, openwow::data::wdb_format::kBuild_335a);
  AppendU32LE(file_data, locale_id);
  AppendU32LE(file_data, openwow::data::wdb_format::kRecordSize_Name);
  AppendU32LE(file_data, openwow::data::wdb_format::kVersion_Name);

  struct PersistedRecord {
    std::uint64_t serial = 0;
    std::uint64_t raw_guid = 0;
    std::vector<std::uint8_t> payload;
  };

  std::vector<PersistedRecord> records;
  std::uint32_t cache_version_token = 0;
  {
    std::lock_guard lock(mutex_);
    cache_version_token = player_name_cache_version_token_;
    records.reserve(player_names_.size());
    for (const auto &[raw_guid, info] : player_names_) {
      if (!ShouldPersistNameCacheEntry(info)) {
        continue;
      }
      const std::uint64_t *const serial =
          player_name_persistence_serials_.FindValue(raw_guid);
      records.push_back(PersistedRecord{
          .serial = serial != nullptr ? *serial : 0,
          .raw_guid = raw_guid,
          .payload = SerializePlayerNameCacheRecord(info),
      });
    }
  }
  std::sort(records.begin(), records.end(),
            [](const PersistedRecord &lhs, const PersistedRecord &rhs) {
              return lhs.serial > rhs.serial;
            });

  AppendU32LE(file_data, cache_version_token);
  for (const auto &record : records) {
    AppendU64LE(file_data, record.raw_guid);
    AppendU32LE(file_data, static_cast<std::uint32_t>(record.payload.size()));
    file_data.insert(file_data.end(), record.payload.begin(), record.payload.end());
  }
  AppendU64LE(file_data, 0);
  AppendU32LE(file_data, 0);

  std::error_code ec;
  std::filesystem::create_directories(cache_dir, ec);
  if (ec) {
    return false;
  }

  const auto path = cache_dir / "namecache.wdb";
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }
  file.write(reinterpret_cast<const char *>(file_data.data()),
             static_cast<std::streamsize>(file_data.size()));
  return file.good();
}

void QueryCache::ApplyPlayerNameCacheVersion(const std::uint32_t version) {
  std::lock_guard lock(mutex_);
  if (player_name_cache_version_token_ == version) {
    return;
  }

  ClearPlayerNameCacheLocked();
  player_name_cache_version_token_ = version;
}

std::uint32_t QueryCache::GetPlayerNameCacheVersion() const {
  std::lock_guard lock(mutex_);
  return player_name_cache_version_token_;
}

void QueryCache::PopulateObjectManagerNameCache(ObjectManager &objects) const {
  std::lock_guard lock(mutex_);
  for (const auto &[raw_guid, info] : player_names_) {
    objects.CachePlayerName(ObjectGuid(raw_guid), info.name, info.race, info.sex, info.class_id);
  }
}

void QueryCache::ClearPendingEntriesOnLogout() {
  std::lock_guard lock(mutex_);
  name_queries_.Clear();
  active_name_query_callback_drains_.clear();
  item_queries_.Clear();
  active_item_template_callback_drains_.clear();
  creature_queries_.Clear();
  active_creature_template_callback_drains_.clear();
  gameobject_queries_.Clear();
  active_go_template_callback_drains_.clear();
  npc_text_queries_.Clear();
  active_npc_text_callback_drains_.clear();
}

void QueryCache::ClearCreatureEntriesForClientCacheVersion() {
  std::lock_guard lock(mutex_);
  creature_templates_.clear();
  creature_queries_.Clear();
  active_creature_template_callback_drains_.clear();
}

void QueryCache::ClearGameObjectEntriesForClientCacheVersion() {
  std::lock_guard lock(mutex_);
  go_templates_.clear();
  gameobject_queries_.Clear();
  active_go_template_callback_drains_.clear();
}

void QueryCache::ClearItemEntriesForClientCacheVersion() {
  {
    std::lock_guard lock(mutex_);
    item_queries_.Clear();
    active_item_template_callback_drains_.clear();
  }
  item_definitions_.ClearItems();
}

void QueryCache::ClearNpcTextEntriesForClientCacheVersion() {
  std::lock_guard lock(mutex_);
  npc_texts_.clear();
  npc_text_queries_.Clear();
  active_npc_text_callback_drains_.clear();
}

void QueryCache::Clear() {
  {
    std::lock_guard lock(mutex_);
    ClearPlayerNameCacheLocked();
    player_name_cache_version_token_ = 0;
    creature_templates_.clear();
    active_creature_template_callback_drains_.clear();
    go_templates_.clear();
    active_go_template_callback_drains_.clear();
    active_item_template_callback_drains_.clear();
    npc_texts_.clear();
    active_npc_text_callback_drains_.clear();
    item_queries_.Clear();
    creature_queries_.Clear();
    gameobject_queries_.Clear();
    npc_text_queries_.Clear();
  }
  item_definitions_.ClearItems();
}

}
