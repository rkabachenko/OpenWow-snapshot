#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::data {

enum class WDBCacheType : uint8_t {
    Creature   = 0,
    GameObject = 1,
    Item       = 2,
    Quest      = 3,
    PageText   = 4,
    NpcText    = 5,
    ItemName   = 6,
    Name       = 7,
    PetName    = 8,
    Petition   = 9,
    ArenaTeam  = 10,
    Guild      = 11,
};

inline constexpr std::size_t kWDBCacheTypeCount = 12;

struct WDBEntry {
    uint32_t id = 0;
    std::vector<uint8_t> data;
    uint64_t timestamp = 0;
    uint32_t version = 0;
};

using WDBCacheCallback = std::function<void(uint32_t ,
                                            const std::vector<uint8_t>& ,
                                            uintptr_t ,
                                            bool )>;

struct WDBCallbackHandle {
    WDBCacheType type = WDBCacheType::Creature;
    uint32_t entry_id = 0;
    uint32_t id = 0;
};

class WDBCache {
 public:
    WDBCache();
    ~WDBCache() = default;

    void Insert(WDBCacheType type, uint32_t id,
                std::vector<uint8_t> data, uint32_t version);

    [[nodiscard]] std::optional<WDBEntry> Get(WDBCacheType type, uint32_t id) const;
    [[nodiscard]] bool Has(WDBCacheType type, uint32_t id) const;

    void Remove(WDBCacheType type, uint32_t id);

    bool InvalidateEntry(WDBCacheType type, uint32_t id);

    void UpdateEntry(WDBCacheType type, uint32_t id,
                     std::vector<uint8_t> data, uint32_t version);

    [[nodiscard]] WDBCallbackHandle RegisterCallback(
        WDBCacheType type, uint32_t entry_id, WDBCacheCallback callback,
        uintptr_t user_data = 0);

    void CancelCallback(WDBCallbackHandle handle);

    [[nodiscard]] uint32_t GetEntryCount(WDBCacheType type) const;
    [[nodiscard]] uint32_t GetTotalEntryCount() const;
    [[nodiscard]] uint64_t GetMemoryUsage() const;

    void ClearType(WDBCacheType type);
    void ClearAll();

    [[nodiscard]] bool IsExpired(WDBCacheType type, uint32_t id,
                                 uint32_t currentVersion) const;
    [[nodiscard]] uint32_t GetVersion(WDBCacheType type, uint32_t id) const;

    [[nodiscard]] std::vector<uint32_t> GetKeys(WDBCacheType type) const;
    [[nodiscard]] std::vector<uint32_t> GetKeysInPersistenceOrder(
        WDBCacheType type) const;

    void SetMaxEntries(WDBCacheType type, uint32_t max);
    [[nodiscard]] uint32_t GetMaxEntries(WDBCacheType type) const;
    void EvictOldest(WDBCacheType type);

    void Reset();

 private:
    struct CallbackEntry {
        uint32_t handle_id;
        WDBCacheCallback callback;
        uintptr_t user_data;
    };

    struct EntryRuntimeState {
        std::vector<CallbackEntry> callbacks;
        bool dispatching_callbacks = false;
        bool deferred_invalidation = false;
    };

    struct Bucket {
        struct StoredEntry {
            WDBEntry entry;
            uint64_t persistence_serial = 0;
        };

        std::unordered_map<uint32_t, StoredEntry> entries;
        std::unordered_map<uint32_t, EntryRuntimeState> runtime_by_entry;
        uint32_t max_entries = 0;
        uint64_t next_persistence_serial = 1;
    };

    static constexpr std::size_t kBucketCount = kWDBCacheTypeCount;
    Bucket buckets_[kBucketCount]{};
    uint32_t next_callback_id_{1};

    [[nodiscard]] Bucket& GetBucket(WDBCacheType type);
    [[nodiscard]] const Bucket& GetBucket(WDBCacheType type) const;
    [[nodiscard]] EntryRuntimeState& EnsureRuntimeState(Bucket& bucket,
                                                        uint32_t entry_id);
    void MaybePruneRuntimeState(Bucket& bucket, uint32_t entry_id);
    void RemoveEntrySilently(Bucket& bucket, uint32_t entry_id);
    [[nodiscard]] bool FinalizeDeferredInvalidation(WDBCacheType type,
                                                    Bucket& bucket,
                                                    uint32_t entry_id);

    void FireCallbacks(WDBCacheType type, uint32_t entry_id,
                       const std::vector<uint8_t>& data, bool is_update);

    static uint64_t CurrentTimestamp();
};

}
