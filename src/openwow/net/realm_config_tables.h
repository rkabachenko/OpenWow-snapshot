#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::net {

struct RealmCategoryRecord {
  std::uint32_t id{0};
  std::uint32_t locale_mask{0};
  std::uint32_t create_charset_mask{0};
  std::uint32_t flags{0};
  std::string name;

  [[nodiscard]] bool tournament() const { return (flags & 0x1u) != 0u; }
};

struct RealmTypeConfig {
  std::uint32_t realm_type{0};
  bool player_killing_allowed{false};
  bool roleplaying{false};
};

class RealmConfigTables {
 public:
  static RealmConfigTables& Get() {
    static RealmConfigTables instance;
    return instance;
  }

  void LoadFrom(const openwow::data::dbc::DbcLoader& loader);

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    categories_.clear();
    realm_types_.clear();
    selected_realm_player_killing_allowed_ = false;
  }

  void SetTablesForTesting(std::vector<RealmCategoryRecord> categories,
                           std::vector<RealmTypeConfig> realm_types) {
    std::lock_guard<std::mutex> lock(mutex_);
    categories_ = std::move(categories);
    realm_types_ = std::move(realm_types);
    selected_realm_player_killing_allowed_ = false;
  }

  [[nodiscard]] std::vector<RealmCategoryRecord> Categories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return categories_;
  }

  [[nodiscard]] std::optional<RealmCategoryRecord> FindCategoryById(
      const std::uint32_t category_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : categories_) {
      if (entry.id == category_id) {
        return entry;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<RealmTypeConfig> FindRealmTypeConfig(
      const std::uint32_t realm_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return FindRealmTypeConfigLocked(realm_type);
  }

  bool UpdateSelectedRealmPlayerKillingAllowed(const std::uint32_t realm_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto entry = FindRealmTypeConfigLocked(realm_type);
    if (!entry.has_value()) {
      return false;
    }
    selected_realm_player_killing_allowed_ = entry->player_killing_allowed;
    return true;
  }

  void ClearSelectedRealmPlayerKillingAllowed() {
    std::lock_guard<std::mutex> lock(mutex_);
    selected_realm_player_killing_allowed_ = false;
  }

  [[nodiscard]] bool GetSelectedRealmPlayerKillingAllowed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_realm_player_killing_allowed_;
  }

 private:
  [[nodiscard]] std::optional<RealmTypeConfig> FindRealmTypeConfigLocked(
      const std::uint32_t realm_type) const {
    for (const auto& entry : realm_types_) {
      if (entry.realm_type == realm_type) {
        return entry;
      }
    }
    return std::nullopt;
  }

  RealmConfigTables() = default;

  mutable std::mutex mutex_;
  std::vector<RealmCategoryRecord> categories_;
  std::vector<RealmTypeConfig> realm_types_;
  bool selected_realm_player_killing_allowed_{false};
};

}
