
#include "openwow/data/db_cache_core.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/warden_module.h"
#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/vfs/sfile_core.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::data {

static void *SMemAlloc(std::size_t size, const char * , int , int ) {
  return std::calloc(1, size);
}

static void SMemFree(void *ptr, const char * , int , int ) {
  std::free(ptr);
}

static void *SMemReAlloc(void *ptr, std::size_t size, const char * , int ,
                         int ) {
  return std::realloc(ptr, size);
}

static void SStrCopy(void *dest, const char *src, int max_len) {
  std::strncpy(static_cast<char *>(dest), src, max_len);
  static_cast<char *>(dest)[max_len - 1] = '\0';
}

OPENWOW_PRINTF_FORMAT(3, 4) static void SStrPrintf(char *dest, unsigned int max_len,
                                                              const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(dest, max_len, fmt, args);
  va_end(args);
}

static constexpr const char *kNameCacheSource =
    "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\"
    "wow-patch-3_3_5_A-BNet\\WoW\\Source\\Game/GameClient/NameCache.h";

static constexpr const char *kPetNameCacheSource =
    "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\"
    "wow-patch-3_3_5_A-BNet\\WoW\\Source\\Game/GameClient/PetNameCache.h";

static constexpr const char *kCreatureStatsSource =
    "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\"
    "wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/CreatureStats.h";

static constexpr const char *kGameObjectStatsSource =
    "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\"
    "wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/GameObjectStats.h";

static constexpr const char *kGameObjectDbCacheHashTag =
    "AUDBCACHEHASH<DBCache<GameObjectStats_C>,int,VHASHKEY_INT>";

static constexpr const char *kItemStatsSource =
    "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\"
    "wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/ItemStats.h";

static constexpr const char *kItemStatsDbCacheHashTag =
    "AUDBCACHEHASH<DBCache<ItemStats_C>,int,VHASHKEY_INT>";

static constexpr const char *kDanceCacheDbCacheHashTag =
    "AUDBCACHEHASH<DBCache<DanceCache>,int,VHASHKEY_INT>";

static constexpr const char *kItemTextCacheDbCacheHashTag =
    "AUDBCACHEHASH<DBCache<ItemTextCache_C>,unsigned __int64,CHashKeyGUID>";

static constexpr const char *kWardenCachedModuleDbCacheHashTag =
    "AUDBCACHEHASH<DBCache<WardenCachedModule>,CWardenKey,CWardenKey>";

static constexpr const char *kCGPetitionDbCacheHashTag =
    ".?AUDBCACHEHASH@?$DBCache@VCGPetition@@HVHASHKEY_INT@@@@";

static constexpr const char *kArenaTeamCacheDbCacheHashTag =
    ".?AUDBCACHEHASH@?$DBCache@VArenaTeamCache@@HVHASHKEY_INT@@@@";

static bool HasCommonArchiveLayout() {
  return ProbeCommonArchiveLayout();
}

static void TSHashTable_Clear(void *hash_table, int ) {

  (void)hash_table;
}

static void CMap_DestroyLightArray(void *bucket) {
  if (bucket == nullptr) {
    return;
  }

  auto &list = *static_cast<openwow::core::StormIntrusiveListRootWords<std::uintptr_t> *>(bucket);

  while ((list.head_node & openwow::core::kStormIntrusiveSentinelBit<std::uintptr_t>) == 0 &&
         list.head_node != 0) {
    void *node_base = reinterpret_cast<void *>(list.head_node);
    auto *link_words = openwow::core::GetStormIntrusiveNodeLinkWords(list, node_base);
    (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(link_words));
  }
}

static char g_locale[16] = "enUS";

namespace {

using StormIntrusiveLinkWords = openwow::core::StormIntrusiveLinkWords<std::uintptr_t>;
using StormIntrusiveListRootWords = openwow::core::StormIntrusiveListRootWords<std::uintptr_t>;

static constexpr const char *kDbCacheCallbackTag = "au::DBCACHECALLBACK";
static constexpr const char *kDbCacheReverseEntryTag = "au::ReverseEntry";

struct DBCacheCallbackNodeStorage {
  StormIntrusiveLinkWords link{};
  std::uint32_t payload_words[6]{};
};

struct TSExplicitListLightArrayRootEntry {
  std::int32_t node_link_offset = 0;
  std::uintptr_t tail_link = 0;
  std::uintptr_t head_node = 0;
};

struct TSExplicitListLightArrayRootList {
  std::uint32_t capacity = 0;
  std::uint32_t count = 0;
  TSExplicitListLightArrayRootEntry *data = nullptr;
  std::uint32_t growth_quantum = 0;
};

struct DBCacheHashBucketTable {
  std::uintptr_t vtable = 0;
  StormIntrusiveListRootWords root{};
  std::uint32_t state_word = 0;
  TSExplicitListLightArrayRootList bucket_storage{};
  std::int32_t cursor = -1;
};

struct CreatureStatsPayload {
  std::uintptr_t vtable = 0;
  void *primary_owned_block = nullptr;
  void *secondary_owned_block = nullptr;
  std::uint8_t opaque_state_bytes[80]{};
  void *auxiliary_owned_blocks[4]{};
  std::uint8_t trailing_state_bytes[8]{};
};

struct DBCacheCreatureStatsEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  CreatureStatsPayload creature_stats{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};

struct GameObjectStatsPayload {
  std::uint32_t type = 0;
  std::uint32_t display_id = 0;
  void *icon_name = nullptr;
  void *cast_bar_caption = nullptr;
  void *unk1 = nullptr;
  std::uint32_t raw_data[24]{};
  float size = 0.0f;
  std::uint32_t quest_items[6]{};
  void *name_ptrs[4]{};
  std::uint8_t trailing_state_bytes[8]{};
};

struct ItemStatsPayload {
  std::uintptr_t vtable = 0;
  std::uint8_t opaque_fields_a[376]{};
  void *primary_owned_block = nullptr;
  std::uint8_t opaque_fields_b[116]{};
  void *auxiliary_owned_blocks[4]{};
  std::uint8_t trailing_bytes[8]{};
};

struct DBCacheItemStatsEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  ItemStatsPayload item_stats{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
  StormIntrusiveLinkWords callback_list_owner_link{};
};

struct DBCacheGameObjectStatsEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  GameObjectStatsPayload game_object_stats{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};

  StormIntrusiveLinkWords callback_list_owner_link{};
};

struct DBCacheGuildStatsEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint8_t record_payload[764]{};
  std::uint32_t opaque_state = 0;
  std::uint8_t update_flag = 0;
  std::uint8_t opaque_padding[3]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};

struct DBCacheItemTextHashEntry {
  std::uint32_t key_low = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint32_t key_words[2]{};
  std::uint8_t text_payload[8000]{};
  std::uint64_t cached_guid = 0;
  std::uint8_t loaded_flag = 0;
  std::uint8_t state_bytes[3]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};

struct DBCachePetNameHashEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint8_t record_payload[84]{};
  void *auxiliary_name_buffer = nullptr;
  std::uint8_t opaque_state_bytes[16]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};

struct DBCacheDanceCacheEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint32_t secondary_key = 0;
  std::uint32_t dance_id = 0;
  std::uint64_t creator_guid = 0;
  char name[128]{};
  void *auxiliary_data = nullptr;
  std::uint32_t move_count = 0;
  std::uint32_t checksum = 0;
  std::uint32_t cached_id = 0;
  std::uint8_t loaded_flag = 0;
  std::uint8_t opaque_state_bytes[3]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};
static_assert(sizeof(void*) != 4 || sizeof(DBCacheDanceCacheEntry) == 208,
              "DanceCache DBCACHEHASH entry must be exactly 208 bytes (32-bit layout)");

struct DBCacheWardenModuleEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint8_t record_payload[16]{};
  openwow::game::WardenModuleData warden_module_data{};
  std::uint8_t opaque_state_bytes[20]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};
static_assert(sizeof(void*) != 4 || sizeof(DBCacheWardenModuleEntry) == 88,
              "WardenCachedModule DBCACHEHASH entry must be exactly 88 bytes (32-bit layout)");

struct DBCacheArenaTeamCacheEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint8_t record_payload[124]{};
  std::uint8_t opaque_state_bytes[12]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};
static_assert(sizeof(void*) != 4 || sizeof(DBCacheArenaTeamCacheEntry) == 180,
              "ArenaTeamCache DBCACHEHASH entry must be exactly 180 bytes (32-bit layout)");

struct DBCacheCGPetitionEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
  std::uint8_t record_payload[5072]{};
  std::uint8_t loaded_flag = 0;
  std::uint8_t opaque_state_bytes[3]{};
  StormIntrusiveListRootWords callback_list{};
  std::uint8_t callback_state_bytes[4]{};
  StormIntrusiveLinkWords pending_callback_owner_link{};
};
static_assert(sizeof(void*) != 4 || sizeof(DBCacheCGPetitionEntry) == 5120,
              "CGPetition DBCACHEHASH entry must be exactly 5120 bytes (32-bit layout)");

struct DBCacheReverseEntry {
  std::uint32_t key = 0;
  StormIntrusiveLinkWords primary_hash_link{};
  StormIntrusiveLinkWords reverse_hash_link{};
};

using ExplicitBucketStorageDestroyAllFn = void (*)(void *);
using ExplicitBucketStorageResizeFn = void (*)(void *, unsigned int);

constexpr std::uint32_t kDbCacheHashInitialBucketCount = 4u;
constexpr std::int32_t kDbCacheHashRetailBucketLinkOffset = 4;
constexpr std::int32_t kDbCacheHashCtorRootLinkOffset = 12;
constexpr std::int32_t kDbCacheHashCtorDebugFill = static_cast<std::int32_t>(0xDDDDDDDDu);
constexpr std::uintptr_t kCreatureStatsPayloadVTable = 0xA29530u;

[[nodiscard]] std::uint32_t
ResolveDbCacheHashAutoGrowthQuantum(const std::uint32_t requested_count) {
  if (requested_count >= 64u) {
    return 64u;
  }

  std::uint32_t quantum = requested_count;
  for (std::uint32_t value = requested_count & (requested_count - 1u); value != 0;
       value &= (value - 1u)) {
    quantum = value;
  }

  return quantum == 0 ? 1u : quantum;
}

[[nodiscard]] void *GetFirstStormIntrusiveNode(StormIntrusiveListRootWords &list) {
  const auto head_node = list.head_node;
  if ((head_node & openwow::core::kStormIntrusiveSentinelBit<std::uintptr_t>) != 0 ||
      head_node == 0) {
    return nullptr;
  }

  return reinterpret_cast<void *>(head_node);
}

void InitializeExplicitListLightArrayRoot(TSExplicitListLightArrayRootEntry &entry,
                                          const std::int32_t link_offset) {
  openwow::core::InitializeStormIntrusiveListRoot(
      reinterpret_cast<StormIntrusiveListRootWords &>(entry), link_offset);
}

void UnlinkExplicitListLightArrayRoot(TSExplicitListLightArrayRootEntry &entry) {
  if (entry.tail_link == 0) {
    return;
  }

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&entry.tail_link));
}

void DestroyExplicitListLightArrayRoots(void *list, const char *debug_tag) {
  auto &storage = *static_cast<TSExplicitListLightArrayRootList *>(list);
  if (storage.data == nullptr) {
    return;
  }

  for (std::uint32_t index = 0; index < storage.count; ++index) {
    auto &entry = storage.data[index];
    CMap_DestroyLightArray(&entry);
    UnlinkExplicitListLightArrayRoot(entry);
  }

  SMemFree(storage.data, debug_tag, -2, 0);
}

void ResizeExplicitListLightArrayRootStorage(TSExplicitListLightArrayRootList &storage,
                                             const std::uint32_t new_capacity,
                                             const char *debug_tag) {
  auto *const old_data = storage.data;
  const auto old_count = storage.count;

  if (old_data != nullptr && new_capacity < old_count) {
    for (std::uint32_t index = new_capacity; index < old_count; ++index) {
      auto &removed = old_data[index];
      CMap_DestroyLightArray(&removed);
      UnlinkExplicitListLightArrayRoot(removed);
    }
  }

  storage.capacity = new_capacity;

  auto *resized = static_cast<TSExplicitListLightArrayRootEntry *>(SMemReAlloc(
      old_data, sizeof(TSExplicitListLightArrayRootEntry) * static_cast<std::size_t>(new_capacity),
      debug_tag, -2, 16));
  storage.data = resized;
  if (resized != nullptr) {
    return;
  }

  auto *const rebuilt = static_cast<TSExplicitListLightArrayRootEntry *>(
      SMemAlloc(sizeof(TSExplicitListLightArrayRootEntry) * static_cast<std::size_t>(new_capacity),
                debug_tag, -2, 0));
  storage.data = rebuilt;

  if (old_data == nullptr) {
    return;
  }

  const auto copy_count = new_capacity < old_count ? new_capacity : old_count;
  for (std::uint32_t index = 0; index < copy_count; ++index) {
    auto &source = old_data[index];
    if (rebuilt != nullptr) {
      auto &destination = rebuilt[index];
      destination.node_link_offset = source.node_link_offset;
      InitializeExplicitListLightArrayRoot(destination, destination.node_link_offset);
    }

    CMap_DestroyLightArray(&source);
    UnlinkExplicitListLightArrayRoot(source);
  }

  SMemFree(old_data, debug_tag, -2, 0);
}

void DestroyDbCacheCallbackNodes(StormIntrusiveListRootWords &callback_list) {
  for (auto *node = GetFirstStormIntrusiveNode(callback_list); node != nullptr;
       node = GetFirstStormIntrusiveNode(callback_list)) {
    auto *const link_words = openwow::core::GetStormIntrusiveNodeLinkWords(callback_list, node);
    (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(link_words));
    SMemFree(node, kDbCacheCallbackTag, -2, 0);
  }
}

void UnlinkStormLinkedNodePair(StormIntrusiveLinkWords &first_link,
                               StormIntrusiveLinkWords &second_link) {
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&second_link));
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&first_link));
}

void DestroyDbCacheReverseEntry(void *entry, const char *debug_tag) {
  if (entry == nullptr) {
    return;
  }

  auto &reverse_entry = *static_cast<DBCacheReverseEntry *>(entry);
  UnlinkStormLinkedNodePair(reverse_entry.primary_hash_link, reverse_entry.reverse_hash_link);
  SMemFree(entry, debug_tag, -2, 0);
}

template <typename TEntry> void DestroyDbCacheHashEntryWithCallbacks(TEntry &entry) {
  DestroyDbCacheCallbackNodes(entry.callback_list);
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&entry.pending_callback_owner_link));
  UnlinkStormLinkedNodePair(entry.primary_hash_link, entry.reverse_hash_link);
}

template <typename TEntry>
void DestroyDbCacheHashEntryWithCallbacksAndOwnedBuffer(TEntry &entry, void *owned_buffer) {
  DestroyDbCacheCallbackNodes(entry.callback_list);
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&entry.pending_callback_owner_link));
  if (owned_buffer != nullptr) {
    SMemFree(owned_buffer, "delete", -1, 0);
  }
  UnlinkStormLinkedNodePair(entry.primary_hash_link, entry.reverse_hash_link);
}

void DestroyCreatureStatsPayload(CreatureStatsPayload &payload) {
  payload.vtable = kCreatureStatsPayloadVTable;

  for (void *owned_block : payload.auxiliary_owned_blocks) {
    if (owned_block != nullptr) {
      SMemFree(owned_block, kCreatureStatsSource, 19, 0);
    }
  }

  if (payload.primary_owned_block != nullptr) {
    SMemFree(payload.primary_owned_block, kCreatureStatsSource, 20, 0);
  }

  if (payload.secondary_owned_block != nullptr) {
    SMemFree(payload.secondary_owned_block, kCreatureStatsSource, 21, 0);
  }
}

void DestroyGameObjectStatsPayload(GameObjectStatsPayload &payload) {
  for (void *&name_ptr : payload.name_ptrs) {
    if (name_ptr != nullptr) {
      SMemFree(name_ptr, kGameObjectStatsSource, 24, 0);
    }
  }

  if (payload.icon_name != nullptr) {
    SMemFree(payload.icon_name, kGameObjectStatsSource, 26, 0);
  }

  if (payload.cast_bar_caption != nullptr) {
    SMemFree(payload.cast_bar_caption, kGameObjectStatsSource, 27, 0);
  }

  if (payload.unk1 != nullptr) {
    SMemFree(payload.unk1, kGameObjectStatsSource, 28, 0);
  }
}

static constexpr std::uintptr_t kItemStatsPayloadVTable = 0xA29668u;

void DestroyItemStatsPayload(ItemStatsPayload &payload) {
  payload.vtable = kItemStatsPayloadVTable;

  for (void *&owned_block : payload.auxiliary_owned_blocks) {
    if (owned_block != nullptr) {
      SMemFree(owned_block, kItemStatsSource, 21, 0);
    }
  }

  if (payload.primary_owned_block != nullptr) {
    SMemFree(payload.primary_owned_block, kItemStatsSource, 22, 0);
  }
}

void ConstructDbCacheHashBucketTable(DBCacheHashBucketTable &table, const std::uintptr_t vtable) {
  table.vtable = vtable;
  table.root.head_node = 0;
  openwow::core::InitializeStormIntrusiveListRoot(table.root, kDbCacheHashCtorDebugFill);
  table.state_word = 0;
  table.bucket_storage.capacity = 0;
  table.bucket_storage.count = 0;
  table.bucket_storage.data = nullptr;
  table.bucket_storage.growth_quantum = 0;

  if (table.root.node_link_offset != kDbCacheHashCtorRootLinkOffset) {
    CMap_DestroyLightArray(&table.root);
    openwow::core::InitializeStormIntrusiveListRoot(table.root, kDbCacheHashCtorRootLinkOffset);
  }

  table.cursor = -1;
}

void DrainDbCacheHashBucketRoots(DBCacheHashBucketTable &table) {
  CMap_DestroyLightArray(&table.root);

  for (std::uint32_t index = 0; index < table.bucket_storage.count; ++index) {
    CMap_DestroyLightArray(&table.bucket_storage.data[index]);
  }
}

void ResetDbCacheHashBucketTable(DBCacheHashBucketTable &table,
                                 ExplicitBucketStorageDestroyAllFn destroy_bucket_storage) {
  table.state_word = 0;
  DrainDbCacheHashBucketRoots(table);
  table.cursor = -1;
  destroy_bucket_storage(&table.bucket_storage);
  table.bucket_storage.capacity = 0;
  table.bucket_storage.count = 0;
  table.bucket_storage.data = nullptr;
}

void DestroyDbCacheHashBucketTable(DBCacheHashBucketTable &table, std::uintptr_t vtable,
                                   ExplicitBucketStorageDestroyAllFn destroy_bucket_storage) {
  table.vtable = vtable;
  table.state_word = 0;
  DrainDbCacheHashBucketRoots(table);
  destroy_bucket_storage(&table.bucket_storage);
  CMap_DestroyLightArray(&table.root);
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&table.root.tail_link));
}

void SetDbCacheHashBucketStorageCount(TSExplicitListLightArrayRootList &storage,
                                      const std::uint32_t new_count,
                                      ExplicitBucketStorageResizeFn resize_storage) {
  if (new_count > storage.count) {
    if (new_count > storage.capacity) {
      auto growth_quantum = storage.growth_quantum;
      if (growth_quantum == 0) {
        growth_quantum = ResolveDbCacheHashAutoGrowthQuantum(new_count);
      }

      auto aligned_capacity = new_count;
      if (aligned_capacity % growth_quantum != 0) {
        aligned_capacity += growth_quantum - (aligned_capacity % growth_quantum);
      }

      resize_storage(&storage, aligned_capacity);
    }

    if (storage.data != nullptr && storage.count < new_count) {
      for (std::uint32_t index = storage.count; index < new_count; ++index) {
        auto &bucket = storage.data[index];
        bucket.node_link_offset = static_cast<std::int32_t>(0xDDDDDDDDu);
        InitializeExplicitListLightArrayRoot(bucket, bucket.node_link_offset);
      }
    }

    storage.count = new_count;
    return;
  }

  if (new_count < storage.count && storage.data != nullptr) {
    for (std::uint32_t index = new_count; index < storage.count; ++index) {
      auto &bucket = storage.data[index];
      CMap_DestroyLightArray(&bucket);
      UnlinkExplicitListLightArrayRoot(bucket);
    }
  }

  storage.count = new_count;
}

[[nodiscard]] std::uint32_t
InitializeDbCacheHashTableWithFourBuckets(DBCacheHashBucketTable &table,
                                          ExplicitBucketStorageResizeFn resize_storage) {
  table.cursor = static_cast<std::int32_t>(kDbCacheHashInitialBucketCount - 1u);
  SetDbCacheHashBucketStorageCount(table.bucket_storage, kDbCacheHashInitialBucketCount,
                                   resize_storage);

  if (table.bucket_storage.data == nullptr) {
    return 0;
  }

  for (std::uint32_t bucket_index = 0; bucket_index < table.bucket_storage.count &&
                                       bucket_index <= static_cast<std::uint32_t>(table.cursor);
       ++bucket_index) {
    auto &bucket = table.bucket_storage.data[bucket_index];
    if (bucket.node_link_offset != kDbCacheHashRetailBucketLinkOffset) {
      CMap_DestroyLightArray(&bucket);
      bucket.node_link_offset = kDbCacheHashRetailBucketLinkOffset;
      InitializeExplicitListLightArrayRoot(bucket, bucket.node_link_offset);
    }
  }

  return table.bucket_storage.count;
}

}

void DBCache_GetCacheDirectory(char *out) {
  if (HasCommonArchiveLayout()) {
    SStrPrintf(out, 0x104, "Cache/%s/%s", "WDB", g_locale);
  } else {
    SStrCopy(out, "WDB", 260);
  }
  (void)openwow::vfs::FileSystem_CreateDirectory(out, true);
}

void DBCache_UpdateEntry_NameCache(void *dest, const void *source) {
  auto *d = static_cast<char *>(dest);
  const auto *s = static_cast<const char *>(source);

  std::memcpy(d + 312, s + 312, 4);
  std::memcpy(d + 316, s + 316, 4);

  SStrCopy(d, s, 48);

  auto src_declined = *reinterpret_cast<const std::uint32_t *>(s + 48);
  if (src_declined) {
    auto *dest_declined_ptr = reinterpret_cast<std::uint32_t *>(d + 48);
    if (!*dest_declined_ptr) {
      *dest_declined_ptr =
          static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(SMemAlloc(320, kNameCacheSource, 0x2D, 0)));
    }
    auto dest_declined = *dest_declined_ptr;
    for (unsigned int i = 0; i < 320; i += 64) {
      SStrCopy(reinterpret_cast<void *>(dest_declined + i),
               reinterpret_cast<const char *>(src_declined + i), 64);
    }
  }

  SStrCopy(d + 52, s + 52, 256);

  std::memcpy(d + 320, s + 320, 4);
  std::memcpy(d + 324, s + 324, 4);
  std::memcpy(d + 328, s + 328, 4);
}

void DBCache_UpdateEntry_CreatureCache(void *dest, const void *source) {
  auto *d = static_cast<char *>(dest);
  const auto *s = static_cast<const char *>(source);

  std::memcpy(d + 84, s + 84, 4);

  SStrCopy(d, s, 80);

  auto src_extended = *reinterpret_cast<const std::uint32_t *>(s + 80);
  if (src_extended) {
    auto *dest_extended_ptr = reinterpret_cast<std::uint32_t *>(d + 80);
    if (!*dest_extended_ptr) {
      *dest_extended_ptr =
          static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(SMemAlloc(480, kPetNameCacheSource, 0x28, 0)));
    }
    auto dest_extended = *dest_extended_ptr;
    for (unsigned int i = 0; i < 480; i += 96) {
      SStrCopy(reinterpret_cast<void *>(dest_extended + i),
               reinterpret_cast<const char *>(src_extended + i), 96);
    }
  }

  std::memcpy(d + 88, s + 88, 4);
}

void DBCache_Load_NameCache(void *dest, const void *source) {
  std::memcpy(dest, source, 16);
}

void TSExplicitList_LightArrayRoot_ResizeCapacity(void *list, unsigned int new_size) {
  ResizeExplicitListLightArrayRootStorage(*static_cast<TSExplicitListLightArrayRootList *>(list),
                                          new_size, "av::TSExplicitList");
}

void CEquipmentSet_Serialize(const CEquipmentSetRecord &record,
                             openwow::net::CDataStore &store) {
  openwow::net::CDataStore_PutUInt32(store, record.field_0);
  openwow::net::CDataStore_PutPackedGuid(store, record.guid);
  openwow::net::CDataStore_PutString(store, record.name);
  openwow::net::CDataStore_PutUInt32(store, record.itemCount);
  if (record.itemCount > 0 && record.items != nullptr) {
    openwow::net::CDataStore_PutBytes(store, record.items,
                                      record.itemCount * sizeof(uint32_t));
  }
  openwow::net::CDataStore_PutUInt32(store, record.field_98);
}

void ArenaTeamCacheRecord_Serialize(const ArenaTeamCacheRecord &record,
                                    openwow::net::CDataStore &store) {
  openwow::net::CDataStore_PutUInt32(store, record.team_type);
  openwow::net::CDataStore_PutString(store, record.team_name);
  openwow::net::CDataStore_PutUInt32(store, record.background_color);
  openwow::net::CDataStore_PutUInt32(store, record.emblem_style);
  openwow::net::CDataStore_PutUInt32(store, record.emblem_color);
  openwow::net::CDataStore_PutUInt32(store, record.border_style);
  openwow::net::CDataStore_PutUInt32(store, record.border_color);
  openwow::net::CDataStore_PutUInt32(store, record.field_78);
}

void DBCache_ClearAllEntries(void *cache) {
  auto *base = static_cast<char *>(cache);
  TSHashTable_Clear(base + 8, 0);
  TSHashTable_Clear(base + 96, 0);
}

void *DBCACHECALLBACK_Alloc(void *list, int insert_mode, int payload_size, char flags) {
  auto *node = static_cast<DBCacheCallbackNodeStorage *>(SMemAlloc(
      payload_size + sizeof(DBCacheCallbackNodeStorage), kDbCacheCallbackTag, -2, flags | 8));

  if (node) {
    node->link.previous_link = 0;
    node->link.next_node = 0;
  } else {
    node = nullptr;
  }

  if (insert_mode != 0 && node != nullptr) {
    auto &callback_list = *static_cast<StormIntrusiveListRootWords *>(list);
    openwow::core::RelinkStormIntrusiveNode(callback_list, node, insert_mode, nullptr);
  }

  return node;
}

void TSExplicitList_UDBCACHEHASH_Realloc(void *list, unsigned int new_size, const char *debug_tag) {
  ResizeExplicitListLightArrayRootStorage(*static_cast<TSExplicitListLightArrayRootList *>(list),
                                          new_size, debug_tag);
}

static constexpr const char *kTag_VCre = "av::TSExplicitList<UDBCACHEHASH,DBCache::VCre>";
static constexpr const char *kTag_VGam = "av::TSExplicitList<UDBCACHEHASH,DBCache::VGam>";
static constexpr const char *kTag_VIte = "av::TSExplicitList<UDBCACHEHASH,DBCache::VIte>";
static constexpr const char *kTag_VNPC = "av::TSExplicitList<UDBCACHEHASH,DBCache::VNPC>";
static constexpr const char *kTag_VNam = "av::TSExplicitList<UDBCACHEHASH,DBCache::VNam>";
static constexpr const char *kTag_VGui = "av::TSExplicitList<UDBCACHEHASH,DBCache::VGui>";
static constexpr const char *kTag_VQue = "av::TSExplicitList<UDBCACHEHASH,DBCache::VQue>";
static constexpr const char *kTag_VPag = "av::TSExplicitList<UDBCACHEHASH,DBCache::VPag>";
static constexpr const char *kTag_VPet = "av::TSExplicitList<UDBCACHEHASH,DBCache::VPet>";
static constexpr const char *kTag_VCGP = "av::TSExplicitList<UDBCACHEHASH,DBCache::VCGP>";
static constexpr const char *kTag_VWar = "av::TSExplicitList<UDBCACHEHASH,DBCache::VWar>";
static constexpr const char *kTag_VAre = "av::TSExplicitList<UDBCACHEHASH,DBCache::VAre>";
static constexpr const char *kTag_VDan = "av::TSExplicitList<UDBCACHEHASH,DBCache::VDan>";

void TSExplicitList_UDBCACHEHASH_VCre_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VCre);
}
void TSExplicitList_UDBCACHEHASH_VGam_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VGam);
}
void TSExplicitList_UDBCACHEHASH_VIte_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VIte);
}
void TSExplicitList_UDBCACHEHASH_VNPC_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VNPC);
}
void TSExplicitList_UDBCACHEHASH_VNam_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VNam);
}
void TSExplicitList_UDBCACHEHASH_VGui_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VGui);
}
void TSExplicitList_UDBCACHEHASH_VQue_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VQue);
}
void TSExplicitList_UDBCACHEHASH_VPag_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VPag);
}
void TSExplicitList_UDBCACHEHASH_VPet_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VPet);
}
void TSExplicitList_UDBCACHEHASH_VCGP_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VCGP);
}
void TSExplicitList_UDBCACHEHASH_VWar_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VWar);
}
void TSExplicitList_UDBCACHEHASH_VAre_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VAre);
}
void TSExplicitList_UDBCACHEHASH_VDan_Realloc(void *list, unsigned int new_size) {
  TSExplicitList_UDBCACHEHASH_Realloc(list, new_size, kTag_VDan);
}

void TSExplicitList_UREVERSEENTRY_DestroyAll(void *list, const char *debug_tag) {
  DestroyExplicitListLightArrayRoots(list, debug_tag);
}

static constexpr const char *kRTag_VCr = "av::TSExplicitList<UREVERSEENTRY,DBCache::VCr>";
static constexpr const char *kRTag_VGa = "av::TSExplicitList<UREVERSEENTRY,DBCache::VGa>";
static constexpr const char *kRTag_VIt = "av::TSExplicitList<UREVERSEENTRY,DBCache::VIt>";
static constexpr const char *kRTag_VNP = "av::TSExplicitList<UREVERSEENTRY,DBCache::VNP>";
static constexpr const char *kRTag_VGu = "av::TSExplicitList<UREVERSEENTRY,DBCache::VGu>";
static constexpr const char *kRTag_VQu = "av::TSExplicitList<UREVERSEENTRY,DBCache::VQu>";
static constexpr const char *kRTag_VPa = "av::TSExplicitList<UREVERSEENTRY,DBCache::VPa>";
static constexpr const char *kRTag_VPe = "av::TSExplicitList<UREVERSEENTRY,DBCache::VPe>";
static constexpr const char *kRTag_VCG = "av::TSExplicitList<UREVERSEENTRY,DBCache::VCG>";
static constexpr const char *kRTag_VWa = "av::TSExplicitList<UREVERSEENTRY,DBCache::VWa>";
static constexpr const char *kRTag_VAr = "av::TSExplicitList<UREVERSEENTRY,DBCache::VAr>";
static constexpr const char *kRTag_VDa = "av::TSExplicitList<UREVERSEENTRY,DBCache::VDa>";

void TSExplicitList_UREVERSEENTRY_VCr_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VCr);
}
void TSExplicitList_UREVERSEENTRY_VGa_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VGa);
}
void TSExplicitList_UREVERSEENTRY_VIt_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VIt);
}
void TSExplicitList_UREVERSEENTRY_VNP_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VNP);
}
void TSExplicitList_UREVERSEENTRY_VGu_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VGu);
}
void TSExplicitList_UREVERSEENTRY_VQu_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VQu);
}
void TSExplicitList_UREVERSEENTRY_VPa_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VPa);
}
void TSExplicitList_UREVERSEENTRY_VPe_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VPe);
}
void TSExplicitList_UREVERSEENTRY_VCG_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VCG);
}
void TSExplicitList_UREVERSEENTRY_VWa_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VWa);
}
void TSExplicitList_UREVERSEENTRY_VAr_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VAr);
}
void TSExplicitList_UREVERSEENTRY_VDa_DestroyAll(void *list) {
  TSExplicitList_UREVERSEENTRY_DestroyAll(list, kRTag_VDa);
}

void DBCacheReverseEntry_VCreatureStats_Destroy(void *entry) {
  DestroyDbCacheReverseEntry(entry, kDbCacheReverseEntryTag);
}

void DBCacheReverseEntry_VCGPetition_Destroy(void *entry) {
  DestroyDbCacheReverseEntry(entry, kDbCacheReverseEntryTag);
}

void TSExplicitList_UDBCACHEHASH_VIte_DestroyAll(void *list) {
  DestroyExplicitListLightArrayRoots(list, kTag_VIte);
}

void TSExplicitList_UDBCACHEHASH_VGam_DestroyAll(void *list) {
  DestroyExplicitListLightArrayRoots(list, kTag_VGam);
}

void TSExplicitList_UDBCACHEHASH_VCGP_DestroyAll(void *list) {
  DestroyExplicitListLightArrayRoots(list, kTag_VCGP);
}

void TSExplicitList_UDBCACHEHASH_VGui_DestroyAll(void *list) {
  DestroyExplicitListLightArrayRoots(list, kTag_VGui);
}

void TSExplicitList_UDBCACHEHASH_VQue_DestroyAll(void *list) {
  DestroyExplicitListLightArrayRoots(list, kTag_VQue);
}

void DBCACHEHASH_VCreatureStats_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &creature_entry = *static_cast<DBCacheCreatureStatsEntry *>(entry);
  DestroyDbCacheCallbackNodes(creature_entry.callback_list);
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&creature_entry.pending_callback_owner_link));
  DestroyCreatureStatsPayload(creature_entry.creature_stats);
  UnlinkStormLinkedNodePair(creature_entry.primary_hash_link, creature_entry.reverse_hash_link);
}

void DBCACHEHASH_VCreatureStats_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VCreatureStats_Destroy(entry);
  SMemFree(entry, "AUDBCACHEHASH<DBCache<CreatureStats_C>,int,VHASHKEY_INT>", -2, 0);
}

void DBCACHEHASH_VGameObjectStats_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &game_object_entry = *static_cast<DBCacheGameObjectStatsEntry *>(entry);
  DestroyDbCacheCallbackNodes(game_object_entry.callback_list);
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&game_object_entry.pending_callback_owner_link));
  CMap_DestroyLightArray(&game_object_entry.callback_list);
  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&game_object_entry.callback_list_owner_link));
  DestroyGameObjectStatsPayload(game_object_entry.game_object_stats);
  UnlinkStormLinkedNodePair(game_object_entry.primary_hash_link,
                            game_object_entry.reverse_hash_link);
}

void DBCACHEHASH_VGameObjectStats_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VGameObjectStats_Destroy(entry);
  SMemFree(entry, kGameObjectDbCacheHashTag, -2, 0);
}

void DBCACHEHASH_VItemStats_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &item_entry = *static_cast<DBCacheItemStatsEntry *>(entry);

  DestroyDbCacheCallbackNodes(item_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&item_entry.pending_callback_owner_link));

  CMap_DestroyLightArray(&item_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&item_entry.callback_list_owner_link));

  DestroyItemStatsPayload(item_entry.item_stats);

  UnlinkStormLinkedNodePair(item_entry.primary_hash_link, item_entry.reverse_hash_link);
}

void DBCACHEHASH_VItemStats_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VItemStats_Destroy(entry);
  SMemFree(entry, kItemStatsDbCacheHashTag, -2, 0);
}

void DBCACHEHASH_VGuildStats_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &guild_entry = *static_cast<DBCacheGuildStatsEntry *>(entry);
  DestroyDbCacheHashEntryWithCallbacks(guild_entry);
}

void DBCACHEHASH_VItemTextCache_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &item_text_entry = *static_cast<DBCacheItemTextHashEntry *>(entry);
  DestroyDbCacheHashEntryWithCallbacks(item_text_entry);
}

void DBCACHEHASH_VItemTextCache_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VItemTextCache_Destroy(entry);
  SMemFree(entry, kItemTextCacheDbCacheHashTag, -2, 0);
}

void DBCACHEHASH_VPetNameCache_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &pet_name_entry = *static_cast<DBCachePetNameHashEntry *>(entry);
  DestroyDbCacheHashEntryWithCallbacksAndOwnedBuffer(pet_name_entry,
                                                     pet_name_entry.auxiliary_name_buffer);
}

void DBCACHEHASH_VDanceCache_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &dance_entry = *static_cast<DBCacheDanceCacheEntry *>(entry);

  DestroyDbCacheCallbackNodes(dance_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&dance_entry.pending_callback_owner_link));

  CMap_DestroyLightArray(&dance_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&dance_entry.callback_list.tail_link));

  if (dance_entry.auxiliary_data != nullptr) {
    SMemFree(dance_entry.auxiliary_data, "delete[]", -1, 0);
  }
  dance_entry.auxiliary_data = nullptr;

  UnlinkStormLinkedNodePair(dance_entry.primary_hash_link,
                            dance_entry.reverse_hash_link);
}

void DBCACHEHASH_VDanceCache_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VDanceCache_Destroy(entry);
  SMemFree(entry, kDanceCacheDbCacheHashTag, -2, 0);
}

void DBCACHEHASH_VWardenCachedModule_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &warden_entry = *static_cast<DBCacheWardenModuleEntry *>(entry);

  DestroyDbCacheCallbackNodes(warden_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&warden_entry.pending_callback_owner_link));

  CMap_DestroyLightArray(&warden_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&warden_entry.callback_list.tail_link));

  openwow::game::WardenClient_FreeModuleData(&warden_entry.warden_module_data);

  UnlinkStormLinkedNodePair(warden_entry.primary_hash_link,
                            warden_entry.reverse_hash_link);
}

void DBCACHEHASH_VWardenCachedModule_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VWardenCachedModule_Destroy(entry);
  SMemFree(entry, kWardenCachedModuleDbCacheHashTag, -2, 0);
}

void DBCACHEHASH_VArenaTeamCache_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &arena_entry = *static_cast<DBCacheArenaTeamCacheEntry *>(entry);

  DestroyDbCacheCallbackNodes(arena_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&arena_entry.pending_callback_owner_link));

  CMap_DestroyLightArray(&arena_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&arena_entry.callback_list.tail_link));

  UnlinkStormLinkedNodePair(arena_entry.primary_hash_link, arena_entry.reverse_hash_link);
}

void DBCACHEHASH_VArenaTeamCache_FreeNode(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VArenaTeamCache_Destroy(entry);
  SMemFree(entry, kArenaTeamCacheDbCacheHashTag, -2, 0);
}

void DBCACHEHASH_VCGPetition_Destroy(void *entry) {
  if (entry == nullptr) {
    return;
  }

  auto &petition_entry = *static_cast<DBCacheCGPetitionEntry *>(entry);

  DestroyDbCacheCallbackNodes(petition_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&petition_entry.pending_callback_owner_link));

  CMap_DestroyLightArray(&petition_entry.callback_list);

  (void)openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(&petition_entry.callback_list.tail_link));

  UnlinkStormLinkedNodePair(petition_entry.primary_hash_link, petition_entry.reverse_hash_link);
}

void DBCACHEHASH_VCGPetition_ScalarDeletingDestructor(void *entry) {
  if (entry == nullptr) {
    return;
  }

  DBCACHEHASH_VCGPetition_Destroy(entry);
  SMemFree(entry, kCGPetitionDbCacheHashTag, -2, 0);
}

void TSHashTable_UDBCACHEHASH_VPet_Ctor(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kPetNamePrimaryHashTableVTable = 0xA29A34u;

  ConstructDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                  kPetNamePrimaryHashTableVTable);
}

void TSHashTable_UREVERSEENTRY_VPe_Ctor(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kPetNameReverseHashTableVTable = 0xA29A44u;

  ConstructDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                  kPetNameReverseHashTableVTable);
}

void TSHashTable_UDBCACHEHASH_VGui_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UDBCACHEHASH_VGui_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VGui_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kGuildPrimaryHashTableVTable = 0xA299D4u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kGuildPrimaryHashTableVTable,
                                &TSExplicitList_UDBCACHEHASH_VGui_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VGam_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kGameObjectPrimaryHashTableVTable = 0xA29934u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kGameObjectPrimaryHashTableVTable,
                                &TSExplicitList_UDBCACHEHASH_VGam_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VGu_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kGuildReverseHashTableVTable = 0xA299E4u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kGuildReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VGu_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VGa_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kGameObjectReverseHashTableVTable = 0xA29944u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kGameObjectReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VGa_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VIte_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kItemStatsPrimaryHashTableVTable = 0xA29974u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kItemStatsPrimaryHashTableVTable,
                                &TSExplicitList_UDBCACHEHASH_VIte_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VIt_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kItemStatsReverseHashTableVTable = 0xA29984u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kItemStatsReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VIt_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VGu_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UREVERSEENTRY_VGu_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VPe_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kPetNameReverseHashTableVTable = 0xA29A44u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kPetNameReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VPe_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VPe_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UREVERSEENTRY_VPe_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VCGP_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kCGPetitionPrimaryHashTableVTable = 0xA29A54u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kCGPetitionPrimaryHashTableVTable,
                                &TSExplicitList_UDBCACHEHASH_VCGP_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VCGP_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UDBCACHEHASH_VCGP_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VQue_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kQuestPrimaryHashTableVTable = 0xA299F4u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kQuestPrimaryHashTableVTable,
                                &TSExplicitList_UDBCACHEHASH_VQue_DestroyAll);
}

void TSHashTable_UDBCACHEHASH_VQue_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UDBCACHEHASH_VQue_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VQu_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kQuestReverseHashTableVTable = 0xA29A04u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kQuestReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VQu_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VQu_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UREVERSEENTRY_VQu_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VPa_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kPageTextReverseHashTableVTable = 0xA29A24u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kPageTextReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VPa_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VCG_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kCGPetitionReverseHashTableVTable = 0xA29A64u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kCGPetitionReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VCG_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VCG_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UREVERSEENTRY_VCG_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VDa_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kDanceReverseHashTableVTable = 0xA29AE4u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kDanceReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VDa_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VDa_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UREVERSEENTRY_VDa_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VWa_Destroy(void *table) {
  if (table == nullptr) {
    return;
  }

  static constexpr std::uintptr_t kWardenReverseHashTableVTable = 0xA29AA4u;

  DestroyDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                                kWardenReverseHashTableVTable,
                                &TSExplicitList_UREVERSEENTRY_VWa_DestroyAll);
}

void TSHashTable_UREVERSEENTRY_VWa_Reset(void *table) {
  if (table == nullptr) {
    return;
  }

  ResetDbCacheHashBucketTable(*static_cast<DBCacheHashBucketTable *>(table),
                              &TSExplicitList_UREVERSEENTRY_VWa_DestroyAll);
}

unsigned int TSHashTable_UDBCACHEHASH_VCGP_InitWithFourBuckets(void *table) {
  if (table == nullptr) {
    return 0;
  }

  return InitializeDbCacheHashTableWithFourBuckets(*static_cast<DBCacheHashBucketTable *>(table),
                                                   &TSExplicitList_UDBCACHEHASH_VCGP_Realloc);
}

unsigned int TSHashTable_UDBCACHEHASH_VPet_InitWithFourBuckets(void *table) {
  if (table == nullptr) {
    return 0;
  }

  return InitializeDbCacheHashTableWithFourBuckets(*static_cast<DBCacheHashBucketTable *>(table),
                                                   &TSExplicitList_UDBCACHEHASH_VPet_Realloc);
}

unsigned int TSHashTable_UDBCACHEHASH_VIte_InitWithFourBuckets(void *table) {
  if (table == nullptr) {
    return 0;
  }

  return InitializeDbCacheHashTableWithFourBuckets(*static_cast<DBCacheHashBucketTable *>(table),
                                                   &TSExplicitList_UDBCACHEHASH_VIte_Realloc);
}

unsigned int TSHashTable_UDBCACHEHASH_VAre_InitWithFourBuckets(void *table) {
  if (table == nullptr) {
    return 0;
  }

  return InitializeDbCacheHashTableWithFourBuckets(*static_cast<DBCacheHashBucketTable *>(table),
                                                   &TSExplicitList_UDBCACHEHASH_VAre_Realloc);
}

unsigned int TSHashTable_UDBCACHEHASH_VDan_InitWithFourBuckets(void *table) {
  if (table == nullptr) {
    return 0;
  }

  return InitializeDbCacheHashTableWithFourBuckets(*static_cast<DBCacheHashBucketTable *>(table),
                                                   &TSExplicitList_UDBCACHEHASH_VDan_Realloc);
}

void DualLinkNode_Detach(void *entry) {
  using Word = std::uintptr_t;
  auto *base = static_cast<char *>(entry);

  auto &primary =
      *reinterpret_cast<StormIntrusiveLinkWords *>(base + 4);

  if (primary.next_node == 0) {
    return;
  }

  if (primary.previous_link != 0) {
    openwow::core::UnlinkStormIntrusiveNativeLink<Word>(
        reinterpret_cast<Word>(&primary));
  }

  auto &reverse =
      *reinterpret_cast<StormIntrusiveLinkWords *>(base + 12);

  if (reverse.previous_link != 0) {
    openwow::core::UnlinkStormIntrusiveNativeLink<Word>(
        reinterpret_cast<Word>(&reverse));
  }
}

void DBCache_ClearPendingEntries(void *cache, std::uint32_t loaded_flag_offset) {
  using Word = std::uintptr_t;
  auto *base = static_cast<char *>(cache);

  auto is_valid = [](Word token) -> bool {
    return (token & openwow::core::kStormIntrusiveSentinelBit<Word>) == 0 &&
           token != 0;
  };

  Word node_token = *reinterpret_cast<Word *>(base + 20);
  if (!is_valid(node_token)) {
    node_token = 0;
  }

  while (is_valid(node_token)) {
    auto *node = reinterpret_cast<char *>(static_cast<std::uintptr_t>(node_token));

    const auto link_offset =
        static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t *>(base + 12));

    Word next_token =
        *reinterpret_cast<Word *>(node + link_offset + sizeof(Word));

    if (*reinterpret_cast<std::uint8_t *>(node + loaded_flag_offset)) {

      if (!is_valid(next_token)) {
        node_token = 0;
      } else {
        node_token = next_token;
      }
    } else {

      Word saved_next = is_valid(next_token) ? next_token : 0;

      DualLinkNode_Detach(node);

      auto **vtable = *reinterpret_cast<void (***)(void *, void *)>(base + 8);
      if (vtable && *vtable) {
        (*vtable)(base + 8, node);
      }

      node_token = saved_next;
    }
  }
}

void DBCache_Creature_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 136);
}

void DBCache_GameObject_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 188);
}

void DBCache_ItemName_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 36);
}

void DBCache_Item_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 544);
}

void DBCache_NpcText_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 348);
}

void DBCache_Name_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 376);
}

void DBCache_Guild_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 792);
}

void DBCache_Quest_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 10496);
}

void DBCache_PageText_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 40);
}

void DBCache_PetName_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 120);
}

void DBCache_Petition_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 5092);
}

void DBCache_ItemText_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 8040);
}

void DBCache_WardenModule_ClearRecords(void *cache) {
  DBCache_ClearPendingEntries(cache, 60);
}

static void DBCache_ApplyCacheVersion_Generic(void *cache, int version) {
  auto *base = static_cast<char *>(cache);

  if (!base[69])
    return;

  if (*reinterpret_cast<int *>(base + 56) == version)
    return;

  TSHashTable_Clear(base + 8, 0);
  TSHashTable_Clear(base + 96, 0);
  *reinterpret_cast<int *>(base + 56) = version;
}

void DBCache_ApplyCacheVersion_ItemName(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_Item(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_NpcText(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_Name(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_Guild(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_Quest(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_PageText(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_PetName(void *cache, int version) {
  auto *base = static_cast<char *>(cache);
  if (!base[69]) return;
  if (*reinterpret_cast<int *>(base + 56) == version) return;
  DBCache_PetName_ClearRecords(cache);
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_Petition(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_ItemText(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

void DBCache_ApplyCacheVersion_WardenModule(void *cache, int version) {
  DBCache_ApplyCacheVersion_Generic(cache, version);
}

}
