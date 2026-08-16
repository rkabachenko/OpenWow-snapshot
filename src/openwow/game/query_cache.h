
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/foundation/container/flat_hash_map.h"
#include "openwow/game/async_query_channel.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data {
class DBCacheRuntime;
class WDBCache;
}

namespace openwow::game {

class ObjectManager;

struct PlayerNameInfo {
  ObjectGuid guid{0};
  std::string name;
  std::string realm_name;
  std::uint8_t race = 0;
  std::uint8_t sex = 0;
  std::uint8_t class_id = 0;
  bool declined = false;
  std::array<std::string, 5> declined_names{};
};

struct CreatureTemplateInfo {
  std::uint32_t entry = 0;
  std::string name;
  std::array<std::string, 3> alternate_names{};
  std::string sub_name;
  std::string icon_name;
  std::uint32_t type_flags = 0;
  std::uint32_t creature_type = 0;
  std::uint32_t creature_family = 0;
  std::uint32_t rank = 0;
  std::array<std::uint32_t, 2> kill_credit{};
  std::array<std::uint32_t, 4> display_ids{};
  float mod_health = 1.0f;
  float mod_mana = 1.0f;
  std::uint8_t racial_leader = 0;
  std::array<std::uint32_t, 6> quest_items{};
  std::uint32_t movement_id = 0;
};

struct GameObjectTemplateInfo {
  std::uint32_t entry = 0;
  std::uint32_t type = 0;
  std::uint32_t display_id = 0;
  std::string name;
  std::array<std::string, 3> alternate_names{};
  std::string icon_name;
  std::string cast_bar_caption;
  std::string unk1;
  std::array<std::uint32_t, 24> raw_data{};
  float size = 1.0f;
  std::array<std::uint32_t, 6> quest_items{};
};

struct NpcTextBlock {
  float probability = 0.0f;
  std::string text_male;
  std::string text_female;
  std::uint32_t language = 0;
  std::array<std::uint32_t, 6> emotes{};
};

struct NpcTextInfo {
  std::uint32_t text_id = 0;
  std::array<NpcTextBlock, 8> blocks{};
};

class QueryCache {
public:
  using NameQueryDispatchFn = std::function<void(std::uint64_t raw_guid)>;
  using QueryDispatchFn = std::function<void(std::uint32_t entry, std::uint64_t context_guid)>;
  using CallbackKey = AsyncQueryChannel::CallbackKey;
  using QueryCallback = AsyncQueryChannel::Callback;
  using QueryRequestOptions = AsyncQueryChannel::RequestOptions;

  QueryCache(openwow::data::DBCacheRuntime& db_cache_runtime,
             ItemDefinitions& item_definitions);

  bool HandleNameQueryResponse(const std::uint8_t *data, std::size_t len);
  bool HandleCreatureQueryResponse(const std::uint8_t *data, std::size_t len);
  bool HandleGameObjectQueryResponse(const std::uint8_t *data, std::size_t len);
  bool HandleItemQuerySingleResponse(const std::uint8_t *data, std::size_t len);
  bool HandleItemQueryMultipleResponse(
      const std::uint8_t *data, std::size_t len,
      std::function<void(std::uint32_t response_entry)> on_processed = {});
  bool HandleNpcTextUpdate(const std::uint8_t *data, std::size_t len);

  [[nodiscard]] bool HydrateRetailWdbCaches(openwow::data::WDBCache &cache);

  [[nodiscard]] const PlayerNameInfo *GetPlayerName(std::uint64_t raw_guid) const;
  [[nodiscard]] std::optional<ObjectGuid> FindPlayerGuidByName(std::string_view name) const;
  [[nodiscard]] const PlayerNameInfo *GetOrRequestPlayerName(std::uint64_t raw_guid);
  [[nodiscard]] const PlayerNameInfo *GetOrRequestPlayerName(std::uint64_t raw_guid,
                                                             QueryRequestOptions options);
  [[nodiscard]] const CreatureTemplateInfo *GetCreatureTemplate(std::uint32_t entry) const;
  [[nodiscard]] const GameObjectTemplateInfo *GetGameObjectTemplate(std::uint32_t entry) const;
  [[nodiscard]] const ItemTemplate *GetItemTemplate(std::uint32_t entry) const;
  [[nodiscard]] const CreatureTemplateInfo *
  GetOrRequestCreatureTemplate(std::uint32_t entry, std::uint64_t context_guid = 0);
  [[nodiscard]] const CreatureTemplateInfo *
  GetOrRequestCreatureTemplate(std::uint32_t entry, QueryRequestOptions options);
  [[nodiscard]] const GameObjectTemplateInfo *
  GetOrRequestGameObjectTemplate(std::uint32_t entry, std::uint64_t context_guid = 0);
  [[nodiscard]] const GameObjectTemplateInfo *
  GetOrRequestGameObjectTemplate(std::uint32_t entry, QueryRequestOptions options);
  [[nodiscard]] const ItemTemplate *GetOrRequestItemTemplate(std::uint32_t entry);
  [[nodiscard]] const ItemTemplate *GetOrRequestItemTemplate(
      std::uint32_t entry, QueryRequestOptions options);
  [[nodiscard]] const ItemTemplate *GetItemTemplateByName(
      const std::string &name) const;
  [[nodiscard]] const NpcTextInfo *GetNpcText(std::uint32_t text_id) const;

  [[nodiscard]] bool HasPlayerName(std::uint64_t raw_guid) const;
  [[nodiscard]] bool IsNameQueryPending(std::uint64_t raw_guid) const;
  bool InvalidatePlayerName(std::uint64_t raw_guid);
  [[nodiscard]] bool HasCreatureTemplate(std::uint32_t entry) const;
  [[nodiscard]] bool HasGameObjectTemplate(std::uint32_t entry) const;
  [[nodiscard]] bool HasItemTemplate(std::uint32_t entry) const;
  [[nodiscard]] bool HasNpcText(std::uint32_t text_id) const;

  bool InvalidateCreatureTemplate(std::uint32_t entry);

  bool InvalidateItemTemplate(std::uint32_t entry);

  bool InvalidateGameObjectTemplate(std::uint32_t entry);

  void CancelCreatureTemplateCallback(std::uint32_t entry, CallbackKey key);
  void CancelGameObjectTemplateCallback(std::uint32_t entry, CallbackKey key);
  void CancelItemTemplateCallback(std::uint32_t entry, CallbackKey key);
  void CancelItemTemplateCallbacks(CallbackKey key);
  [[nodiscard]] bool HasNameQueryDispatcher() const;

  [[nodiscard]] std::size_t GetPlayerNameCount() const;
  [[nodiscard]] std::size_t GetCreatureCount() const;
  [[nodiscard]] std::size_t GetGameObjectCount() const;
  [[nodiscard]] std::size_t GetItemCount() const;
  [[nodiscard]] std::size_t GetNpcTextCount() const;

  [[nodiscard]] bool RequestNameQuery(std::uint64_t raw_guid);
  void MarkItemQueryPending(std::uint32_t entry);
  [[nodiscard]] bool MarkNameQueryPending(std::uint64_t raw_guid);
  void MarkCreatureQueryPending(std::uint32_t entry);
  void MarkGameObjectQueryPending(std::uint32_t entry);
  void MarkNpcTextQueryPending(std::uint32_t text_id);
  [[nodiscard]] bool IsItemQueryPending(std::uint32_t entry) const;
  [[nodiscard]] bool IsCreatureQueryPending(std::uint32_t entry) const;
  [[nodiscard]] bool IsGameObjectQueryPending(std::uint32_t entry) const;
  [[nodiscard]] bool IsNpcTextQueryPending(std::uint32_t text_id) const;

  [[nodiscard]] static net::wotlk::WorldPacket BuildNameQuery(std::uint64_t guid);
  [[nodiscard]] static net::wotlk::WorldPacket BuildCreatureQuery(std::uint32_t entry,
                                                                  std::uint64_t guid);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGameObjectQuery(std::uint32_t entry,
                                                                    std::uint64_t guid);
  [[nodiscard]] static net::wotlk::WorldPacket BuildItemQuery(std::uint32_t entry);
  [[nodiscard]] static net::wotlk::WorldPacket BuildNpcTextQuery(std::uint32_t text_id,
                                                                 std::uint64_t npc_guid);
  [[nodiscard]] static net::wotlk::WorldPacket BuildPageTextQuery(std::uint32_t page_text_id);

  void SetNameQueryDispatcher(NameQueryDispatchFn dispatcher);
  void SetCreatureQueryDispatcher(QueryDispatchFn dispatcher);
  void SetGameObjectQueryDispatcher(QueryDispatchFn dispatcher);
  void SetItemQueryDispatcher(QueryDispatchFn dispatcher);
  void SetTickCountProvider(std::function<std::uint32_t()> provider);
  void PumpDispatchQueues(std::uint32_t current_tick_ms);

  void SetNameQueryMaxInFlight(std::uint32_t max_in_flight);
  void SetCreatureQueryMaxInFlight(std::uint32_t max_in_flight);
  void SetGameObjectQueryMaxInFlight(std::uint32_t max_in_flight);
  void SetItemQueryMaxInFlight(std::uint32_t max_in_flight);

  [[nodiscard]] bool LoadPlayerNameCacheWdb(const std::filesystem::path &cache_dir,
                                            std::uint32_t locale_id);
  [[nodiscard]] bool SavePlayerNameCacheWdb(const std::filesystem::path &cache_dir,
                                            std::uint32_t locale_id) const;

  void ApplyPlayerNameCacheVersion(std::uint32_t version);
  [[nodiscard]] std::uint32_t GetPlayerNameCacheVersion() const;
  void PopulateObjectManagerNameCache(ObjectManager &objects) const;

  void ClearPendingEntriesOnLogout();

  void ClearCreatureEntriesForClientCacheVersion();

  void ClearGameObjectEntriesForClientCacheVersion();

  void ClearItemEntriesForClientCacheVersion();

  void ClearNpcTextEntriesForClientCacheVersion();

  void Clear();

private:
  enum class CacheUpdateOrigin : std::uint8_t {
    Network,
    PersistentStorage,
  };

  struct ActiveNameQueryCallbackDrain {
    std::uint64_t guid = 0;
    bool erase_on_finish = false;
    bool deferred_invalidation = false;
  };

  struct ActiveQueryCallbackDrain {
    std::uint32_t entry = 0;
    bool deferred_invalidation = false;
  };

  void UnindexPlayerNameLocked(std::uint64_t raw_guid);
  void ErasePlayerNameLocked(std::uint64_t raw_guid);
  void ClearPlayerNameCacheLocked();
  [[nodiscard]] std::vector<QueryCallback>
  ResolveNameQueryCallbacksLocked(std::uint64_t raw_guid, bool erase_on_finish);
  void FinishNameQueryCallbackDrainLocked(std::uint64_t raw_guid);

  bool HandleCreatureQueryResponse(const std::uint8_t *data, std::size_t len,
                                   CacheUpdateOrigin origin,
                                   std::size_t *consumed_bytes = nullptr);
  bool HandleGameObjectQueryResponse(const std::uint8_t *data, std::size_t len,
                                     CacheUpdateOrigin origin,
                                     std::size_t *consumed_bytes = nullptr);
  bool HandleItemQuerySingleResponse(const std::uint8_t *data, std::size_t len,
                                     CacheUpdateOrigin origin,
                                     std::size_t *consumed_bytes = nullptr);
  bool HandleNpcTextUpdate(const std::uint8_t *data, std::size_t len,
                           CacheUpdateOrigin origin,
                           std::size_t *consumed_bytes = nullptr);

  bool InvalidateCreatureTemplate(std::uint32_t entry, bool update_wdb_owner);
  bool InvalidateGameObjectTemplate(std::uint32_t entry, bool update_wdb_owner);
  bool InvalidateItemTemplate(std::uint32_t entry, bool update_wdb_owner);
  bool InvalidateNpcText(std::uint32_t text_id, bool update_wdb_owner);

  template <typename EntryMap>
  bool BeginUint32InvalidationLocked(
      EntryMap &entries, AsyncQueryChannel &queries,
      std::vector<ActiveQueryCallbackDrain> &active_drains,
      std::uint32_t entry, std::vector<QueryCallback> &callbacks,
      bool &started_callback_drain, bool &deferred_to_active_drain);

  template <typename EntryMap>
  [[nodiscard]] bool FinishUint32CallbackDrainLocked(
      EntryMap &entries,
      std::vector<ActiveQueryCallbackDrain> &active_drains,
      std::uint32_t entry);

  mutable std::mutex mutex_;
  openwow::data::DBCacheRuntime& db_cache_runtime_;
  ItemDefinitions& item_definitions_;
  openwow::foundation::StableFlatHashMap<std::uint64_t, PlayerNameInfo> player_names_;
  openwow::foundation::FlatHashMap<std::uint32_t, std::vector<std::uint64_t>>
      player_name_hash_index_;
  openwow::foundation::FlatHashMap<std::uint64_t, std::uint64_t>
      player_name_persistence_serials_;
  std::uint64_t next_player_name_persistence_serial_{1};
  std::uint32_t player_name_cache_version_token_{0};
  AsyncGuidQueryChannel name_queries_;
  std::vector<ActiveNameQueryCallbackDrain> active_name_query_callback_drains_;
  openwow::foundation::StableFlatHashMap<std::uint32_t, CreatureTemplateInfo>
      creature_templates_;
  std::vector<ActiveQueryCallbackDrain> active_creature_template_callback_drains_;
  openwow::foundation::StableFlatHashMap<std::uint32_t, GameObjectTemplateInfo>
      go_templates_;
  std::vector<ActiveQueryCallbackDrain> active_go_template_callback_drains_;
  std::vector<ActiveQueryCallbackDrain> active_item_template_callback_drains_;
  openwow::foundation::StableFlatHashMap<std::uint32_t, NpcTextInfo> npc_texts_;
  std::vector<ActiveQueryCallbackDrain> active_npc_text_callback_drains_;
  std::function<std::uint32_t()> tick_count_provider_;

  AsyncQueryChannel item_queries_;
  AsyncQueryChannel creature_queries_;
  AsyncQueryChannel gameobject_queries_;
  AsyncQueryChannel npc_text_queries_;
  friend struct QueryCacheTestAccess;
};

}
