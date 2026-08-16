#include "openwow/data/db_cache_instances.h"

#include "openwow/data/archive_system.h"
#include "openwow/data/db_cache_core.h"

#include <array>
#include <filesystem>

namespace openwow::data {
namespace {

constexpr std::array<WDBCacheType, 10> kStartupLoadOrder = {
    WDBCacheType::Creature, WDBCacheType::GameObject, WDBCacheType::Item,
    WDBCacheType::ItemName, WDBCacheType::NpcText,    WDBCacheType::Guild,
    WDBCacheType::Quest,    WDBCacheType::PageText,   WDBCacheType::PetName,
    WDBCacheType::Petition,
};

constexpr std::array<WDBCacheType, 11> kShutdownBeforeItemText = {
    WDBCacheType::Creature, WDBCacheType::GameObject, WDBCacheType::Item,
    WDBCacheType::ItemName, WDBCacheType::NpcText,    WDBCacheType::Name,
    WDBCacheType::Guild,    WDBCacheType::Quest,      WDBCacheType::PageText,
    WDBCacheType::PetName,  WDBCacheType::Petition,
};

constexpr std::array<WDBCacheType, 13> kClientVersionOrder = {
    WDBCacheType::Creature,   WDBCacheType::Creature,
    WDBCacheType::GameObject, WDBCacheType::Item,
    WDBCacheType::ItemName,   WDBCacheType::NpcText,
    WDBCacheType::Name,       WDBCacheType::Guild,
    WDBCacheType::Quest,      WDBCacheType::PageText,
    WDBCacheType::PetName,    WDBCacheType::Petition,
    WDBCacheType::ArenaTeam,
};

std::filesystem::path ResolveCacheDirectory() {
  char path[260]{};
  DBCache_GetCacheDirectory(path);
  return path;
}

std::uint32_t ResolveLocale() {
  const int locale = GetCurrentLocaleInfo().locale_index;
  return locale < 0 ? 0u : static_cast<std::uint32_t>(locale);
}

}

bool DBCacheVersionChanges::Changed(const WDBCacheType type) const noexcept {
  return cache_types.test(static_cast<std::size_t>(type));
}

bool DBCacheVersionChanges::Any() const noexcept {
  return cache_types.any() || item_text;
}

DBCacheRuntime::DBCacheRuntime() : persistence_(cache_) {
  persistence_.SetItemTextCache(item_text_cache_);
}

void DBCacheRuntime::ConfigureFromInstall() {
  persistence_.SetCache(cache_);
  persistence_.SetItemTextCache(item_text_cache_);
  persistence_.SetClientBuild(wdb_format::kBuild_335a);
  persistence_.SetLocale(ResolveLocale());
  persistence_.SetCacheDirectory(ResolveCacheDirectory());
}

void DBCacheRuntime::LoadBeforeWarden() {
  ConfigureFromInstall();
  for (const WDBCacheType type : kStartupLoadOrder) {
    (void)persistence_.LoadType(type);
  }
  (void)persistence_.LoadItemTextCache(item_text_cache_);
}

void DBCacheRuntime::LoadAfterWarden() {
  (void)persistence_.LoadType(WDBCacheType::ArenaTeam);
}

void DBCacheRuntime::Flush() {
  ConfigureFromInstall();
  (void)persistence_.FlushIfDirty();
}

void DBCacheRuntime::DestroyBeforeWarden() {
  ConfigureFromInstall();
  for (const WDBCacheType type : kShutdownBeforeItemText) {
    persistence_.DestroyType(type);
  }
  persistence_.DestroyItemTextCache();
}

void DBCacheRuntime::DestroyAfterWarden() {
  persistence_.DestroyType(WDBCacheType::ArenaTeam);
}

void DBCacheRuntime::FinishShutdown() {
  cache_.Reset();
}

void DBCacheRuntime::Reset() {
  cache_.Reset();
  persistence_.ClearItemTextEntries();
  persistence_.SetCache(cache_);
  persistence_.SetItemTextCache(item_text_cache_);
  persistence_.SetCacheDirectory({});
  persistence_.SetClientBuild(wdb_format::kBuild_335a);
  persistence_.SetLocale(0);
  persistence_.ClearDirty();
  persistence_.ResetCacheVersionTokens();
}

bool DBCacheRuntime::ApplyVersion(const WDBCacheType type,
                                  const std::uint32_t version) {
  if (persistence_.GetCacheVersionToken(type) == version) {
    return false;
  }
  cache_.ClearType(type);
  persistence_.SetCacheVersionToken(type, version);
  persistence_.SetDirty(type);
  return true;
}

bool DBCacheRuntime::ApplyItemTextVersion(const std::uint32_t version) {
  if (persistence_.GetItemTextCacheVersionToken() == version) {
    return false;
  }
  persistence_.ClearItemTextEntries();
  persistence_.SetItemTextCacheVersionToken(version);
  persistence_.SetItemTextDirty();
  return true;
}

DBCacheVersionChanges
DBCacheRuntime::ApplyClientVersion(const std::uint32_t version) {
  DBCacheVersionChanges changes;
  for (const WDBCacheType type : kClientVersionOrder) {
    const auto index = static_cast<std::size_t>(type);
    const bool changed = ApplyVersion(type, version);
    changes.cache_types.set(index, changes.cache_types.test(index) || changed);
  }
  changes.item_text = ApplyItemTextVersion(version);
  return changes;
}

}
