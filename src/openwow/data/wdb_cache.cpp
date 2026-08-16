#include "openwow/data/wdb_cache.h"

#include "openwow/core/console.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <numeric>

namespace openwow::data {

WDBCache::WDBCache() = default;

WDBCache::Bucket& WDBCache::GetBucket(WDBCacheType type) {
    return buckets_[static_cast<size_t>(type)];
}

const WDBCache::Bucket& WDBCache::GetBucket(WDBCacheType type) const {
    return buckets_[static_cast<size_t>(type)];
}

WDBCache::EntryRuntimeState& WDBCache::EnsureRuntimeState(
    Bucket& bucket, const uint32_t entry_id) {
    return bucket.runtime_by_entry[entry_id];
}

void WDBCache::MaybePruneRuntimeState(Bucket& bucket, const uint32_t entry_id) {
    const auto runtime_it = bucket.runtime_by_entry.find(entry_id);
    if (runtime_it == bucket.runtime_by_entry.end()) {
        return;
    }

    const auto& runtime = runtime_it->second;
    if (!runtime.callbacks.empty() || runtime.dispatching_callbacks ||
        runtime.deferred_invalidation) {
        return;
    }

    bucket.runtime_by_entry.erase(runtime_it);
}

void WDBCache::RemoveEntrySilently(Bucket& bucket, const uint32_t entry_id) {
    bucket.entries.erase(entry_id);
    bucket.runtime_by_entry.erase(entry_id);
}

bool WDBCache::FinalizeDeferredInvalidation(WDBCacheType type,
                                            Bucket& bucket,
                                            const uint32_t entry_id) {
    const auto runtime_it = bucket.runtime_by_entry.find(entry_id);
    if (runtime_it != bucket.runtime_by_entry.end() &&
        runtime_it->second.dispatching_callbacks) {
        runtime_it->second.deferred_invalidation = true;
        return true;
    }

    if (bucket.entries.contains(entry_id)) {
        RemoveEntrySilently(bucket, entry_id);
        return true;
    }

    if (runtime_it == bucket.runtime_by_entry.end()) {
        return false;
    }

    static const std::vector<uint8_t> kEmptyData;
    FireCallbacks(type, entry_id, kEmptyData, false);
    RemoveEntrySilently(bucket, entry_id);
    return true;
}

uint64_t WDBCache::CurrentTimestamp() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch())
            .count());
}

void WDBCache::Insert(WDBCacheType type, uint32_t id,
                      std::vector<uint8_t> data, uint32_t version) {
    auto& bucket = GetBucket(type);

    if (bucket.max_entries > 0 &&
        !bucket.entries.contains(id) &&
        static_cast<uint32_t>(bucket.entries.size()) >= bucket.max_entries) {
        EvictOldest(type);
    }

    auto it = bucket.entries.find(id);
    const auto timestamp = CurrentTimestamp();
    if (it != bucket.entries.end()) {
        it->second.entry.data = std::move(data);
        it->second.entry.timestamp = timestamp;
        it->second.entry.version = version;
        return;
    }

    WDBEntry entry;
    entry.id = id;
    entry.data = std::move(data);
    entry.timestamp = timestamp;
    entry.version = version;

    Bucket::StoredEntry stored_entry;
    stored_entry.entry = std::move(entry);
    stored_entry.persistence_serial = bucket.next_persistence_serial++;
    bucket.entries.emplace(id, std::move(stored_entry));
}

std::optional<WDBEntry> WDBCache::Get(WDBCacheType type, uint32_t id) const {
    const auto& bucket = GetBucket(type);
    auto it = bucket.entries.find(id);
    if (it == bucket.entries.end()) return std::nullopt;
    return it->second.entry;
}

bool WDBCache::Has(WDBCacheType type, uint32_t id) const {
    return GetBucket(type).entries.contains(id);
}

void WDBCache::Remove(WDBCacheType type, uint32_t id) {
    auto& bucket = GetBucket(type);
    const auto runtime_it = bucket.runtime_by_entry.find(id);
    if (runtime_it != bucket.runtime_by_entry.end() &&
        runtime_it->second.dispatching_callbacks) {

        bucket.entries.erase(id);
        return;
    }

    RemoveEntrySilently(bucket, id);
}

bool WDBCache::InvalidateEntry(WDBCacheType type, uint32_t id) {
    auto& bucket = GetBucket(type);
    auto it = bucket.entries.find(id);
    auto runtime_it = bucket.runtime_by_entry.find(id);
    if (runtime_it != bucket.runtime_by_entry.end() &&
        runtime_it->second.dispatching_callbacks) {

        runtime_it->second.deferred_invalidation = true;
        return true;
    }

    if (it == bucket.entries.end() && runtime_it == bucket.runtime_by_entry.end()) {
        return false;
    }

    static const std::vector<uint8_t> kEmptyData;

    const auto callback_data =
        it != bucket.entries.end() ? it->second.entry.data : kEmptyData;
    if (runtime_it != bucket.runtime_by_entry.end()) {
        runtime_it->second.dispatching_callbacks = true;
        runtime_it->second.deferred_invalidation = false;
    }
    FireCallbacks(type, id, callback_data, false);
    runtime_it = bucket.runtime_by_entry.find(id);
    if (runtime_it != bucket.runtime_by_entry.end()) {
        runtime_it->second.dispatching_callbacks = false;
    }

    RemoveEntrySilently(bucket, id);
    return true;
}

void WDBCache::UpdateEntry(WDBCacheType type, uint32_t id,
                           std::vector<uint8_t> data, uint32_t version) {
    Insert(type, id, std::move(data), version);

    auto& bucket = GetBucket(type);
    auto it = bucket.entries.find(id);
    if (it != bucket.entries.end()) {
        auto& runtime = EnsureRuntimeState(bucket, id);
        runtime.dispatching_callbacks = true;
        runtime.deferred_invalidation = false;

        const auto callback_data = it->second.entry.data;
        FireCallbacks(type, id, callback_data, true);

        auto runtime_it = bucket.runtime_by_entry.find(id);
        if (runtime_it == bucket.runtime_by_entry.end()) {
            return;
        }

        runtime_it->second.dispatching_callbacks = false;

        runtime_it->second.callbacks.clear();

        if (runtime_it->second.deferred_invalidation) {
            (void)FinalizeDeferredInvalidation(type, bucket, id);
        } else {
            MaybePruneRuntimeState(bucket, id);
        }
    }
}

WDBCallbackHandle WDBCache::RegisterCallback(
    WDBCacheType type, uint32_t entry_id, WDBCacheCallback callback, uintptr_t user_data) {
    auto& bucket = GetBucket(type);
    uint32_t handle_id = next_callback_id_++;
    EnsureRuntimeState(bucket, entry_id).callbacks.push_back(
        {handle_id, std::move(callback), user_data});
    return {type, entry_id, handle_id};
}

void WDBCache::CancelCallback(WDBCallbackHandle handle) {
    auto& bucket = GetBucket(handle.type);
    const auto entry_it = bucket.runtime_by_entry.find(handle.entry_id);
    if (entry_it == bucket.runtime_by_entry.end()) {
        return;
    }
    if (entry_it->second.dispatching_callbacks) {
        openwow::core::ida::ConsoleLog(
            "DBCache::CancelCallback ignored for id %016llX.",
            static_cast<unsigned long long>(static_cast<uint64_t>(
                static_cast<int64_t>(static_cast<int32_t>(handle.entry_id)))));
        return;
    }

    auto& callbacks = entry_it->second.callbacks;
    callbacks.erase(
        std::remove_if(callbacks.begin(), callbacks.end(),
                       [&](const CallbackEntry& e) {
                           return e.handle_id == handle.id;
                       }),
        callbacks.end());
    MaybePruneRuntimeState(bucket, handle.entry_id);
}

void WDBCache::FireCallbacks(WDBCacheType type, uint32_t entry_id,
                             const std::vector<uint8_t>& data,
                             const bool is_update) {
    auto& bucket = GetBucket(type);
    auto runtime_it = bucket.runtime_by_entry.find(entry_id);
    if (runtime_it == bucket.runtime_by_entry.end() ||
        runtime_it->second.callbacks.empty()) {
        return;
    }

    uint32_t current_handle = runtime_it->second.callbacks.front().handle_id;
    while (current_handle != 0) {
        runtime_it = bucket.runtime_by_entry.find(entry_id);
        if (runtime_it == bucket.runtime_by_entry.end()) {
            return;
        }

        const auto& callbacks = runtime_it->second.callbacks;
        const auto current_it = std::find_if(
            callbacks.begin(), callbacks.end(),
            [current_handle](const CallbackEntry& callback) {
                return callback.handle_id == current_handle;
            });
        if (current_it == callbacks.end()) {
            return;
        }

        const auto next_it = std::next(current_it);
        const uint32_t next_handle =
            next_it == callbacks.end() ? 0 : next_it->handle_id;
        const CallbackEntry callback = *current_it;
        callback.callback(entry_id, data, callback.user_data, is_update);
        current_handle = next_handle;
    }
}

uint32_t WDBCache::GetEntryCount(WDBCacheType type) const {
    return static_cast<uint32_t>(GetBucket(type).entries.size());
}

uint32_t WDBCache::GetTotalEntryCount() const {
    uint32_t total = 0;
    for (size_t i = 0; i < kBucketCount; ++i) {
        total += static_cast<uint32_t>(buckets_[i].entries.size());
    }
    return total;
}

uint64_t WDBCache::GetMemoryUsage() const {
    uint64_t total = 0;
    for (size_t i = 0; i < kBucketCount; ++i) {
        for (const auto& [_, entry] : buckets_[i].entries) {
            total += sizeof(WDBEntry) + entry.entry.data.size();
        }
    }
    return total;
}

void WDBCache::ClearType(WDBCacheType type) {
    auto& bucket = GetBucket(type);
    bucket.entries.clear();
    bucket.runtime_by_entry.clear();
    bucket.next_persistence_serial = 1;
}

void WDBCache::ClearAll() {
    for (size_t i = 0; i < kBucketCount; ++i) {
        buckets_[i].entries.clear();
        buckets_[i].runtime_by_entry.clear();
        buckets_[i].next_persistence_serial = 1;
    }
}

bool WDBCache::IsExpired(WDBCacheType type, uint32_t id,
                         uint32_t currentVersion) const {
    const auto& bucket = GetBucket(type);
    auto it = bucket.entries.find(id);
    if (it == bucket.entries.end()) return true;
    return it->second.entry.version < currentVersion;
}

uint32_t WDBCache::GetVersion(WDBCacheType type, uint32_t id) const {
    const auto& bucket = GetBucket(type);
    auto it = bucket.entries.find(id);
    if (it == bucket.entries.end()) return 0;
    return it->second.entry.version;
}

std::vector<uint32_t> WDBCache::GetKeys(WDBCacheType type) const {
    const auto& bucket = GetBucket(type);
    std::vector<uint32_t> keys;
    keys.reserve(bucket.entries.size());
    for (const auto& [k, _] : bucket.entries) {
        keys.push_back(k);
    }
    return keys;
}

std::vector<uint32_t> WDBCache::GetKeysInPersistenceOrder(
    WDBCacheType type) const {
    const auto& bucket = GetBucket(type);

    struct OrderedKey {
        uint32_t id = 0;
        uint64_t persistence_serial = 0;
    };

    std::vector<OrderedKey> ordered_keys;
    ordered_keys.reserve(bucket.entries.size());
    for (const auto& [id, stored_entry] : bucket.entries) {
        ordered_keys.push_back({id, stored_entry.persistence_serial});
    }

    std::sort(ordered_keys.begin(), ordered_keys.end(),
              [](const OrderedKey& lhs, const OrderedKey& rhs) {
                  return lhs.persistence_serial > rhs.persistence_serial;
              });

    std::vector<uint32_t> keys;
    keys.reserve(ordered_keys.size());
    for (const auto& ordered_key : ordered_keys) {
        keys.push_back(ordered_key.id);
    }
    return keys;
}

void WDBCache::SetMaxEntries(WDBCacheType type, uint32_t max) {
    GetBucket(type).max_entries = max;
}

uint32_t WDBCache::GetMaxEntries(WDBCacheType type) const {
    return GetBucket(type).max_entries;
}

void WDBCache::EvictOldest(WDBCacheType type) {
    auto& bucket = GetBucket(type);
    if (bucket.entries.empty()) return;

    auto oldest = bucket.entries.begin();
    for (auto it = bucket.entries.begin(); it != bucket.entries.end(); ++it) {
        if (it->second.persistence_serial <
                oldest->second.persistence_serial ||
            (it->second.persistence_serial ==
                 oldest->second.persistence_serial &&
             it->first < oldest->first)) {
            oldest = it;
        }
    }
    RemoveEntrySilently(bucket, oldest->first);
}

void WDBCache::Reset() {
    for (size_t i = 0; i < kBucketCount; ++i) {
        buckets_[i].entries.clear();
        buckets_[i].runtime_by_entry.clear();
        buckets_[i].max_entries = 0;
        buckets_[i].next_persistence_serial = 1;
    }
    next_callback_id_ = 1;
}

}
