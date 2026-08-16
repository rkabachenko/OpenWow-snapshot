#pragma once

#include "openwow/data/wdb_cache.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::data {

struct WDBFileHeader {
  char signature[4]{};
  uint32_t build{0};
  uint32_t locale{0};
  uint32_t record_size{0};
  uint32_t version{0};
  uint32_t cache_version_token{0};
};

static_assert(sizeof(WDBFileHeader) == 24, "WDBFileHeader must be 24 bytes");

namespace wdb_magic {
inline constexpr char kCreature[4] = {'B', 'O', 'M', 'W'};
inline constexpr char kGameObject[4] = {'B', 'O', 'G', 'W'};
inline constexpr char kItem[4] = {'B', 'D', 'I', 'W'};
inline constexpr char kItemName[4] = {'B', 'D', 'N', 'W'};
inline constexpr char kQuest[4] = {'T', 'S', 'Q', 'W'};
inline constexpr char kPageText[4] = {'X', 'T', 'P', 'W'};
inline constexpr char kNpcText[4] = {'C', 'P', 'N', 'W'};
inline constexpr char kName[4] = {'M', 'A', 'N', 'W'};
inline constexpr char kPetName[4] = {'M', 'N', 'P', 'W'};
inline constexpr char kPetition[4] = {'N', 'T', 'P', 'W'};
inline constexpr char kArenaTeam[4] = {'M', 'T', 'A', 'W'};
inline constexpr char kGuild[4] = {'D', 'L', 'G', 'W'};
inline constexpr char kItemText[4] = {'X', 'T', 'I', 'W'};
inline constexpr char kWarden[4] = {'N', 'D', 'R', 'W'};
inline constexpr char kDance[4] = {'N', 'A', 'D', 'W'};
}

namespace wdb_format {
inline constexpr uint32_t kBuild_335a = 12340;

inline constexpr uint32_t kRecordSize_Creature = 108;
inline constexpr uint32_t kRecordSize_GameObject = 160;
inline constexpr uint32_t kRecordSize_Item = 516;
inline constexpr uint32_t kRecordSize_ItemName = 8;
inline constexpr uint32_t kRecordSize_Quest = 10468;
inline constexpr uint32_t kRecordSize_PageText = 12;
inline constexpr uint32_t kRecordSize_NpcText = 320;
inline constexpr uint32_t kRecordSize_Name = 328;
inline constexpr uint32_t kRecordSize_PetName = 92;
inline constexpr uint32_t kRecordSize_Petition = 5056;
inline constexpr uint32_t kRecordSize_GuildStats = 764;
inline constexpr uint32_t kRecordSize_ArenaTeam = 124;
inline constexpr uint32_t kRecordSize_Dance = 152;

inline constexpr uint32_t kRecordSize_ItemText = 8000;

inline constexpr uint32_t kVersion_Creature = 1;
inline constexpr uint32_t kVersion_GameObject = 1;
inline constexpr uint32_t kVersion_Item = 5;
inline constexpr uint32_t kVersion_ItemName = 1;
inline constexpr uint32_t kVersion_Quest = 3;
inline constexpr uint32_t kVersion_PageText = 1;
inline constexpr uint32_t kVersion_NpcText = 1;
inline constexpr uint32_t kVersion_PetName = 1;
inline constexpr uint32_t kVersion_Petition = 1;
inline constexpr uint32_t kVersion_GuildStats = 1;
inline constexpr uint32_t kVersion_ArenaTeam = 1;
inline constexpr uint32_t kVersion_Name = 4;
inline constexpr uint32_t kVersion_Dance = 1;

inline constexpr uint32_t kVersion_ItemText = 1;
}

struct WDBGuidEntry {
  uint64_t guid = 0;
  std::vector<uint8_t> data;
};

struct WDBGuidTypeInfo {
  char signature[4];
  const char *filename;
  uint32_t record_size;
  uint32_t version;
};

[[nodiscard]] const WDBGuidTypeInfo &GetItemTextWDBTypeInfo();

struct ItemNameWdbPayload {
  std::string name;
  uint32_t inventory_type = 0;
};

[[nodiscard]] bool ParseItemNameWdbPayload(std::span<const uint8_t> data,
                                           ItemNameWdbPayload &out);

[[nodiscard]] std::vector<uint8_t> SerializeItemNameWdbPayload(
    const std::string &name, uint32_t inventory_type);

struct WDBTypeInfo {
  WDBCacheType type;
  char signature[4];
  const char *filename;
  uint32_t record_size;
  uint32_t version;
};

[[nodiscard]] const WDBTypeInfo &GetWDBTypeInfo(WDBCacheType type);

[[nodiscard]] const char *GetWDBFilename(WDBCacheType type);

[[nodiscard]] std::string GetCacheDirectory(bool has_common_archive_layout,
                                            const std::string &locale);

class WDBPersistence {
public:
  WDBPersistence() = default;
  explicit WDBPersistence(WDBCache &cache);

  void SetCache(WDBCache &cache) {
    cache_ = &cache;
  }

  void SetItemTextCache(
      std::unordered_map<uint64_t, std::vector<uint8_t>> &cache) {
    if (item_text_cache_ != &cache) {
      item_text_persistence_serials_.clear();
      next_item_text_persistence_serial_ = 1;
    }
    item_text_cache_ = &cache;
  }

  void StoreItemTextEntry(uint64_t guid, std::vector<uint8_t> data);
  void StoreItemTextEntry(uint64_t guid, std::string_view text);
  void ClearItemTextEntries();

  void SetCacheDirectory(const std::filesystem::path &dir) {
    cache_dir_ = dir;
  }
  [[nodiscard]] const std::filesystem::path &GetCacheDirectory() const {
    return cache_dir_;
  }

  void SetClientBuild(uint32_t build) {
    client_build_ = build;
  }
  [[nodiscard]] uint32_t GetClientBuild() const {
    return client_build_;
  }

  void SetLocale(uint32_t locale) {
    locale_ = locale;
  }
  [[nodiscard]] uint32_t GetLocale() const {
    return locale_;
  }

  [[nodiscard]] bool SaveType(WDBCacheType type) const;

  [[nodiscard]] bool SaveTypeTo(WDBCacheType type, const std::filesystem::path &path) const;

  [[nodiscard]] bool SaveAll() const;

  [[nodiscard]] bool LoadType(WDBCacheType type);

  [[nodiscard]] bool LoadTypeFrom(WDBCacheType type, const std::filesystem::path &path);

  [[nodiscard]] bool LoadAll();

  void SetDirty();
  void SetDirty(WDBCacheType type);
  void ClearDirty();
  void ClearDirty(WDBCacheType type);
  [[nodiscard]] bool IsDirty() const;

  void SetItemTextDirty();
  void ClearItemTextDirty();
  [[nodiscard]] bool IsItemTextDirty() const;

  void SetCacheVersionToken(WDBCacheType type, uint32_t token);
  [[nodiscard]] uint32_t GetCacheVersionToken(WDBCacheType type) const;
  void SetItemTextCacheVersionToken(uint32_t token);
  [[nodiscard]] uint32_t GetItemTextCacheVersionToken() const;
  void ResetCacheVersionTokens();

  [[nodiscard]] bool FlushIfDirty();

  void DestroyType(WDBCacheType type);

  void DestroyItemTextCache();

  void DestroyAll();

  [[nodiscard]] static std::vector<uint8_t> Serialize(const WDBFileHeader &header,
                                                      const std::vector<WDBEntry> &entries);

  struct DeserializeResult {
    bool ok{false};
    WDBFileHeader header{};
    std::vector<WDBEntry> entries;
    std::string error;
  };
  [[nodiscard]] static DeserializeResult Deserialize(std::span<const uint8_t> data);

  [[nodiscard]] static std::vector<uint8_t> SerializeGuidKeyed(
      const WDBFileHeader &header, const std::vector<WDBGuidEntry> &entries);

  struct DeserializeGuidResult {
    bool ok{false};
    WDBFileHeader header{};
    std::vector<WDBGuidEntry> entries;
    std::string error;
  };
  [[nodiscard]] static DeserializeGuidResult DeserializeGuidKeyed(
      std::span<const uint8_t> data);

  [[nodiscard]] bool LoadItemTextCache(
      std::unordered_map<uint64_t, std::vector<uint8_t>> &out);

  [[nodiscard]] bool LoadItemTextCacheFrom(
      const std::filesystem::path &path,
      std::unordered_map<uint64_t, std::vector<uint8_t>> &out);

  [[nodiscard]] bool SaveItemTextCache(
      const std::unordered_map<uint64_t, std::vector<uint8_t>> &entries) const;

  [[nodiscard]] bool SaveItemTextCacheTo(
      const std::filesystem::path &path,
      const std::unordered_map<uint64_t, std::vector<uint8_t>> &entries) const;

private:
  WDBCache *cache_{nullptr};
  std::unordered_map<uint64_t, std::vector<uint8_t>> *item_text_cache_{nullptr};
  std::filesystem::path cache_dir_;
  uint32_t client_build_{12340};
  uint32_t locale_{0};
  std::array<bool, kWDBCacheTypeCount> dirty_types_{};
  std::array<uint32_t, kWDBCacheTypeCount> cache_version_tokens_{};
  bool item_text_dirty_{false};
  uint32_t item_text_cache_version_token_{0};
  std::unordered_map<uint64_t, uint64_t> item_text_persistence_serials_;
  uint64_t next_item_text_persistence_serial_{1};

  [[nodiscard]] std::filesystem::path PathForType(WDBCacheType type) const;
  [[nodiscard]] bool SaveMarkedTypes(bool only_dirty_types, bool include_empty_dirty_types) const;
};

}
