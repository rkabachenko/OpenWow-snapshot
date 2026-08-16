#pragma once

#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"

#include <bitset>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace openwow::data {

struct DBCacheVersionChanges {
  std::bitset<kWDBCacheTypeCount> cache_types;
  bool item_text = false;

  [[nodiscard]] bool Changed(WDBCacheType type) const noexcept;
  [[nodiscard]] bool Any() const noexcept;
};

class DBCacheRuntime final {
 public:
  DBCacheRuntime();
  ~DBCacheRuntime() = default;
  DBCacheRuntime(const DBCacheRuntime&) = delete;
  DBCacheRuntime& operator=(const DBCacheRuntime&) = delete;

  [[nodiscard]] WDBCache& cache() noexcept { return cache_; }
  [[nodiscard]] const WDBCache& cache() const noexcept { return cache_; }
  [[nodiscard]] WDBPersistence& persistence() noexcept {
    return persistence_;
  }
  [[nodiscard]] const WDBPersistence& persistence() const noexcept {
    return persistence_;
  }
  [[nodiscard]] std::unordered_map<std::uint64_t, std::vector<std::uint8_t>>&
  item_text_cache() noexcept {
    return item_text_cache_;
  }
  [[nodiscard]] const std::unordered_map<std::uint64_t,
                                         std::vector<std::uint8_t>>&
  item_text_cache() const noexcept {
    return item_text_cache_;
  }

  void ConfigureFromInstall();
  void LoadBeforeWarden();
  void LoadAfterWarden();
  void Flush();
  void DestroyBeforeWarden();
  void DestroyAfterWarden();
  void FinishShutdown();
  void Reset();

  [[nodiscard]] bool ApplyVersion(WDBCacheType type,
                                  std::uint32_t version);
  [[nodiscard]] bool ApplyItemTextVersion(std::uint32_t version);
  [[nodiscard]] DBCacheVersionChanges
  ApplyClientVersion(std::uint32_t version);

 private:
  WDBCache cache_;
  WDBPersistence persistence_;
  std::unordered_map<std::uint64_t, std::vector<std::uint8_t>>
      item_text_cache_;
};

}
