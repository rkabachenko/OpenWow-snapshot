
#include "openwow/data/wdb_persistence.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace openwow::data {

namespace {

constexpr WDBTypeInfo MakeTypeInfo(const WDBCacheType type,
                                   const char (&signature)[4],
                                   const char *filename,
                                   const uint32_t record_size,
                                   const uint32_t version) {
  WDBTypeInfo info{type, {}, filename, record_size, version};
  for (std::size_t index = 0; index < sizeof(info.signature); ++index) {
    info.signature[index] = signature[index];
  }
  return info;
}

constexpr WDBGuidTypeInfo MakeGuidTypeInfo(const char (&signature)[4],
                                           const char *filename,
                                           const uint32_t record_size,
                                           const uint32_t version) {
  WDBGuidTypeInfo info{{}, filename, record_size, version};
  for (std::size_t index = 0; index < sizeof(info.signature); ++index) {
    info.signature[index] = signature[index];
  }
  return info;
}

}

static constexpr WDBTypeInfo kTypeInfoTable[] = {
    MakeTypeInfo(WDBCacheType::Creature, wdb_magic::kCreature,
                 "creaturecache.wdb", wdb_format::kRecordSize_Creature,
                 wdb_format::kVersion_Creature),
    MakeTypeInfo(WDBCacheType::GameObject, wdb_magic::kGameObject,
                 "gameobjectcache.wdb", wdb_format::kRecordSize_GameObject,
                 wdb_format::kVersion_GameObject),
    MakeTypeInfo(WDBCacheType::Item, wdb_magic::kItem, "itemcache.wdb",
                 wdb_format::kRecordSize_Item, wdb_format::kVersion_Item),
    MakeTypeInfo(WDBCacheType::Quest, wdb_magic::kQuest, "questcache.wdb",
                 wdb_format::kRecordSize_Quest, wdb_format::kVersion_Quest),
    MakeTypeInfo(WDBCacheType::PageText, wdb_magic::kPageText,
                 "pagetextcache.wdb", wdb_format::kRecordSize_PageText,
                 wdb_format::kVersion_PageText),
    MakeTypeInfo(WDBCacheType::NpcText, wdb_magic::kNpcText, "npccache.wdb",
                 wdb_format::kRecordSize_NpcText, wdb_format::kVersion_NpcText),
    MakeTypeInfo(WDBCacheType::ItemName, wdb_magic::kItemName,
                 "itemnamecache.wdb", wdb_format::kRecordSize_ItemName,
                 wdb_format::kVersion_ItemName),
    MakeTypeInfo(WDBCacheType::Name, wdb_magic::kName, "namecache.wdb",
                 wdb_format::kRecordSize_Name, wdb_format::kVersion_Name),
    MakeTypeInfo(WDBCacheType::PetName, wdb_magic::kPetName,
                 "petnamecache.wdb", wdb_format::kRecordSize_PetName,
                 wdb_format::kVersion_PetName),
    MakeTypeInfo(WDBCacheType::Petition, wdb_magic::kPetition,
                 "petitioncache.wdb", wdb_format::kRecordSize_Petition,
                 wdb_format::kVersion_Petition),
    MakeTypeInfo(WDBCacheType::ArenaTeam, wdb_magic::kArenaTeam,
                 "arenateamcache.wdb", wdb_format::kRecordSize_ArenaTeam,
                 wdb_format::kVersion_ArenaTeam),
    MakeTypeInfo(WDBCacheType::Guild, wdb_magic::kGuild, "guildcache.wdb",
                 wdb_format::kRecordSize_GuildStats,
                 wdb_format::kVersion_GuildStats),
};

static_assert(std::size(kTypeInfoTable) == kWDBCacheTypeCount);

const WDBTypeInfo &GetWDBTypeInfo(WDBCacheType type) {
  return kTypeInfoTable[static_cast<size_t>(type)];
}

const char *GetWDBFilename(WDBCacheType type) {
  return kTypeInfoTable[static_cast<size_t>(type)].filename;
}

static constexpr WDBGuidTypeInfo kItemTextTypeInfo =
    MakeGuidTypeInfo(wdb_magic::kItemText, "itemtextcache.wdb",
                     wdb_format::kRecordSize_ItemText,
                     wdb_format::kVersion_ItemText);

const WDBGuidTypeInfo &GetItemTextWDBTypeInfo() {
  return kItemTextTypeInfo;
}

std::string GetCacheDirectory(bool has_common_archive_layout, const std::string &locale) {
  if (has_common_archive_layout) {

    return "Cache/WDB/" + locale;
  }

  return "WDB";
}

bool ParseItemNameWdbPayload(std::span<const uint8_t> data,
                             ItemNameWdbPayload &out) {

  const auto *begin = data.data();
  const auto *end = begin + data.size();
  const auto *nul = static_cast<const uint8_t *>(
      std::memchr(begin, '\0', data.size()));
  if (!nul || (nul + 1 + 4) > end) {
    return false;
  }
  out.name.assign(reinterpret_cast<const char *>(begin),
                  reinterpret_cast<const char *>(nul));
  const uint8_t *p = nul + 1;
  out.inventory_type = static_cast<uint32_t>(p[0]) |
                       (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) |
                       (static_cast<uint32_t>(p[3]) << 24);
  return true;
}

std::vector<uint8_t> SerializeItemNameWdbPayload(const std::string &name,
                                                  uint32_t inventory_type) {
  std::vector<uint8_t> bytes;
  bytes.reserve(name.size() + 5);
  bytes.insert(bytes.end(), name.begin(), name.end());
  bytes.push_back(0);
  bytes.push_back(static_cast<uint8_t>(inventory_type & 0xFFu));
  bytes.push_back(static_cast<uint8_t>((inventory_type >> 8) & 0xFFu));
  bytes.push_back(static_cast<uint8_t>((inventory_type >> 16) & 0xFFu));
  bytes.push_back(static_cast<uint8_t>((inventory_type >> 24) & 0xFFu));
  return bytes;
}

namespace {

[[nodiscard]] std::optional<std::vector<uint8_t>> ReadWholeFile(
    const std::filesystem::path &path) {
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec || file_size > std::numeric_limits<size_t>::max() ||
      file_size > static_cast<std::uintmax_t>(
                      std::numeric_limits<std::streamsize>::max())) {
    return std::nullopt;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }

  std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
  if (!bytes.empty() &&
      !file.read(reinterpret_cast<char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()))) {
    return std::nullopt;
  }
  return bytes;
}

void WriteU32LE(std::vector<uint8_t> &buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void WriteU64LE(std::vector<uint8_t> &buf, uint64_t val) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
  }
}

uint32_t ReadU32LE(const uint8_t *ptr) {
  return static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8) |
         (static_cast<uint32_t>(ptr[2]) << 16) | (static_cast<uint32_t>(ptr[3]) << 24);
}

uint64_t ReadU64LE(const uint8_t *ptr) {
  uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val |= static_cast<uint64_t>(ptr[i]) << (i * 8);
  }
  return val;
}

WDBFileHeader ReadHeader(std::span<const uint8_t> data) {
  WDBFileHeader header{};
  std::memcpy(header.signature, data.data(), 4);
  header.build = ReadU32LE(data.data() + 4);
  header.locale = ReadU32LE(data.data() + 8);
  header.record_size = ReadU32LE(data.data() + 12);
  header.version = ReadU32LE(data.data() + 16);
  header.cache_version_token = ReadU32LE(data.data() + 20);
  return header;
}

bool ValidateHeader(const WDBFileHeader &header, const WDBTypeInfo &info,
                    const uint32_t client_build, const uint32_t locale) {
  if (std::memcmp(header.signature, info.signature, 4) != 0) {
    return false;
  }
  if (header.build != client_build) {
    return false;
  }
  if (header.locale != locale) {
    return false;
  }
  if (info.record_size != 0 && header.record_size != info.record_size) {
    return false;
  }
  if (header.version != info.version) {
    return false;
  }
  return true;
}

bool ParseEntries(std::span<const uint8_t> data, std::vector<WDBEntry> &entries,
                  std::string &error) {
  size_t pos = sizeof(WDBFileHeader);

  while (pos + 8 <= data.size()) {
    const uint32_t entry_id = ReadU32LE(data.data() + pos);
    pos += 4;

    const uint32_t data_size = ReadU32LE(data.data() + pos);
    pos += 4;

    if (entry_id == 0) {

      return true;
    }
    if (data_size > data.size() - pos) {
      error = "Truncated record data (id=" + std::to_string(entry_id) +
              ", need=" + std::to_string(data_size) +
              ", have=" + std::to_string(data.size() - pos) + ")";
      return false;
    }

    WDBEntry entry;
    entry.id = entry_id;
    entry.data.assign(data.data() + pos, data.data() + pos + data_size);
    entries.push_back(std::move(entry));
    pos += data_size;
  }

  error = "Missing EOF sentinel";
  return false;
}

bool ValidateGuidKeyedHeader(const WDBFileHeader &header,
                             const WDBGuidTypeInfo &info,
                             const uint32_t client_build,
                             const uint32_t locale) {
  if (std::memcmp(header.signature, info.signature, 4) != 0) {
    return false;
  }
  if (header.build != client_build) {
    return false;
  }
  if (header.locale != locale) {
    return false;
  }
  if (header.record_size != info.record_size) {
    return false;
  }
  if (header.version != info.version) {
    return false;
  }
  return true;
}

bool ParseGuidEntries(std::span<const uint8_t> data,
                      std::vector<WDBGuidEntry> &entries,
                      std::string &error) {
  size_t pos = sizeof(WDBFileHeader);

  while (pos + 12 <= data.size()) {
    const uint64_t guid = ReadU64LE(data.data() + pos);
    pos += 8;

    const uint32_t data_size = ReadU32LE(data.data() + pos);
    pos += 4;

    if (guid == 0) {
      return true;
    }

    if (data_size > data.size() - pos) {
      error = "Truncated GUID record data (guid=" +
              std::to_string(guid) +
              ", need=" + std::to_string(data_size) +
              ", have=" + std::to_string(data.size() - pos) + ")";
      return false;
    }

    WDBGuidEntry entry;
    entry.guid = guid;
    entry.data.assign(data.data() + pos, data.data() + pos + data_size);
    entries.push_back(std::move(entry));
    pos += data_size;
  }

  error = "Missing GUID EOF sentinel";
  return false;
}

}

WDBPersistence::WDBPersistence(WDBCache &cache) : cache_(&cache) {}

std::filesystem::path WDBPersistence::PathForType(WDBCacheType type) const {
  return cache_dir_ / GetWDBFilename(type);
}

std::vector<uint8_t> WDBPersistence::Serialize(const WDBFileHeader &header,
                                               const std::vector<WDBEntry> &entries) {
  std::vector<uint8_t> buf;

  size_t estimate = sizeof(WDBFileHeader) + 8;
  for (const auto &e : entries) {
    estimate += 8 + e.data.size();
  }
  buf.reserve(estimate);

  buf.insert(buf.end(), header.signature, header.signature + 4);
  WriteU32LE(buf, header.build);
  WriteU32LE(buf, header.locale);
  WriteU32LE(buf, header.record_size);
  WriteU32LE(buf, header.version);
  WriteU32LE(buf, header.cache_version_token);

  for (const auto &entry : entries) {
    WriteU32LE(buf, entry.id);
    WriteU32LE(buf, static_cast<uint32_t>(entry.data.size()));
    buf.insert(buf.end(), entry.data.begin(), entry.data.end());
  }

  WriteU32LE(buf, 0);
  WriteU32LE(buf, 0);

  return buf;
}

WDBPersistence::DeserializeResult WDBPersistence::Deserialize(std::span<const uint8_t> data) {
  DeserializeResult result;

  if (data.size() < sizeof(WDBFileHeader)) {
    result.error = "Data too small for WDB header (need 24 bytes)";
    return result;
  }

  result.header = ReadHeader(data);
  if (!ParseEntries(data, result.entries, result.error)) {
    return result;
  }

  for (auto &entry : result.entries) {
    entry.version = result.header.version;
  }

  result.ok = true;
  return result;
}

std::vector<uint8_t> WDBPersistence::SerializeGuidKeyed(
    const WDBFileHeader &header, const std::vector<WDBGuidEntry> &entries) {
  std::vector<uint8_t> buf;

  size_t estimate = sizeof(WDBFileHeader) + 12;
  for (const auto &e : entries) {
    estimate += 12 + e.data.size();
  }
  buf.reserve(estimate);

  buf.insert(buf.end(), header.signature, header.signature + 4);
  WriteU32LE(buf, header.build);
  WriteU32LE(buf, header.locale);
  WriteU32LE(buf, header.record_size);
  WriteU32LE(buf, header.version);
  WriteU32LE(buf, header.cache_version_token);

  for (const auto &entry : entries) {
    WriteU64LE(buf, entry.guid);
    WriteU32LE(buf, static_cast<uint32_t>(entry.data.size()));
    buf.insert(buf.end(), entry.data.begin(), entry.data.end());
  }

  WriteU64LE(buf, 0);
  WriteU32LE(buf, 0);

  return buf;
}

WDBPersistence::DeserializeGuidResult WDBPersistence::DeserializeGuidKeyed(
    std::span<const uint8_t> data) {
  DeserializeGuidResult result;

  if (data.size() < sizeof(WDBFileHeader)) {
    result.error = "Data too small for WDB header (need 24 bytes)";
    return result;
  }

  result.header = ReadHeader(data);
  if (!ParseGuidEntries(data, result.entries, result.error)) {
    return result;
  }

  result.ok = true;
  return result;
}

bool WDBPersistence::LoadItemTextCacheFrom(
    const std::filesystem::path &path,
    std::unordered_map<uint64_t, std::vector<uint8_t>> &out) {

  auto file_bytes = ReadWholeFile(path);
  if (!file_bytes)
    return false;
  auto &buf = *file_bytes;

  if (buf.size() < sizeof(WDBFileHeader)) {
    return false;
  }

  const auto &info = GetItemTextWDBTypeInfo();
  const auto header = ReadHeader(buf);
  if (!ValidateGuidKeyedHeader(header, info, client_build_, locale_)) {
    return false;
  }
  item_text_cache_version_token_ = header.cache_version_token;

  std::vector<WDBGuidEntry> entries;
  std::string error;
  if (!ParseGuidEntries(buf, entries, error)) {
    out.clear();
    item_text_persistence_serials_.clear();
    next_item_text_persistence_serial_ = 1;
    return false;
  }

  for (auto &entry : entries) {
    if (!out.contains(entry.guid)) {
      item_text_persistence_serials_[entry.guid] =
          next_item_text_persistence_serial_++;
    }
    out[entry.guid] = std::move(entry.data);
  }
  return true;
}

bool WDBPersistence::LoadItemTextCache(
    std::unordered_map<uint64_t, std::vector<uint8_t>> &out) {
  auto path = cache_dir_ / "itemtextcache.wdb";
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return true;
  return LoadItemTextCacheFrom(path, out);
}

bool WDBPersistence::SaveItemTextCacheTo(
    const std::filesystem::path &path,
    const std::unordered_map<uint64_t, std::vector<uint8_t>> &entries) const {
  const auto &info = GetItemTextWDBTypeInfo();

  WDBFileHeader header{};
  std::memcpy(header.signature, info.signature, sizeof(header.signature));
  header.build = client_build_;
  header.locale = locale_;
  header.record_size = info.record_size;
  header.version = info.version;
  header.cache_version_token = item_text_cache_version_token_;

  std::vector<WDBGuidEntry> ordered_entries;
  struct OrderedItemTextEntry {
    uint64_t guid = 0;
    uint64_t serial = 0;
    const std::vector<uint8_t> *data = nullptr;
  };
  std::vector<OrderedItemTextEntry> ordered;
  ordered.reserve(entries.size());
  for (const auto &[guid, data] : entries) {
    if (guid != 0) {
      const auto serial_it = item_text_persistence_serials_.find(guid);
      ordered.push_back(OrderedItemTextEntry{
          .guid = guid,
          .serial = serial_it == item_text_persistence_serials_.end()
                        ? 0
                        : serial_it->second,
          .data = &data,
      });
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const OrderedItemTextEntry &lhs,
               const OrderedItemTextEntry &rhs) {
              if (lhs.serial != rhs.serial) {
                return lhs.serial > rhs.serial;
              }
              return lhs.guid < rhs.guid;
            });
  ordered_entries.reserve(ordered.size());
  for (const auto &entry : ordered) {
    ordered_entries.push_back(WDBGuidEntry{entry.guid, *entry.data});
  }

  const auto bytes = SerializeGuidKeyed(header, ordered_entries);
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return file.good();
}

bool WDBPersistence::SaveItemTextCache(
    const std::unordered_map<uint64_t, std::vector<uint8_t>> &entries) const {
  return SaveItemTextCacheTo(cache_dir_ / GetItemTextWDBTypeInfo().filename, entries);
}

bool WDBPersistence::SaveTypeTo(WDBCacheType type, const std::filesystem::path &path) const {
  if (!cache_)
    return false;

  const auto &info = GetWDBTypeInfo(type);

  WDBFileHeader header{};
  std::memcpy(header.signature, info.signature, 4);
  header.build = client_build_;
  header.locale = locale_;
  header.record_size = info.record_size;
  header.version = info.version;

  auto keys = cache_->GetKeysInPersistenceOrder(type);
  std::vector<WDBEntry> entries;
  entries.reserve(keys.size());
  for (uint32_t k : keys) {
    auto opt = cache_->Get(type, k);
    if (opt) {
      entries.push_back(std::move(*opt));
    }
  }

  header.cache_version_token = GetCacheVersionToken(type);
  auto buf = Serialize(header, entries);

  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
      return false;
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file)
    return false;
  file.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));
  return file.good();
}

bool WDBPersistence::SaveType(WDBCacheType type) const {
  return SaveTypeTo(type, PathForType(type));
}

bool WDBPersistence::SaveAll() const {
  const bool typed_ok = SaveMarkedTypes(false, false);
  const bool item_text_ok =
      item_text_cache_ == nullptr || SaveItemTextCache(*item_text_cache_);
  return typed_ok && item_text_ok;
}

bool WDBPersistence::LoadTypeFrom(WDBCacheType type, const std::filesystem::path &path) {
  if (!cache_)
    return false;

  auto file_bytes = ReadWholeFile(path);
  if (!file_bytes)
    return false;
  auto &buf = *file_bytes;

  const auto &info = GetWDBTypeInfo(type);
  if (buf.size() < sizeof(WDBFileHeader)) {
    return false;
  }

  const auto header = ReadHeader(buf);
  if (!ValidateHeader(header, info, client_build_, locale_)) {
    return false;
  }
  SetCacheVersionToken(type, header.cache_version_token);

  std::vector<WDBEntry> entries;
  std::string error;
  if (!ParseEntries(buf, entries, error)) {

    cache_->ClearType(type);
    return false;
  }

  for (auto &entry : entries) {
    cache_->Insert(type, entry.id, std::move(entry.data), header.version);
  }
  return true;
}

bool WDBPersistence::LoadType(WDBCacheType type) {
  auto path = PathForType(type);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return true;
  return LoadTypeFrom(type, path);
}

bool WDBPersistence::LoadAll() {
  bool ok = true;
  for (size_t i = 0; i < dirty_types_.size(); ++i) {
    if (!LoadType(static_cast<WDBCacheType>(i)))
      ok = false;
  }
  return ok;
}

void WDBPersistence::SetDirty() {
  dirty_types_.fill(true);
  if (item_text_cache_ != nullptr) {
    item_text_dirty_ = true;
  }
}

void WDBPersistence::SetDirty(WDBCacheType type) {
  dirty_types_[static_cast<size_t>(type)] = true;
}

void WDBPersistence::ClearDirty() {
  dirty_types_.fill(false);
  item_text_dirty_ = false;
}

void WDBPersistence::ClearDirty(WDBCacheType type) {
  dirty_types_[static_cast<size_t>(type)] = false;
}

bool WDBPersistence::IsDirty() const {
  return item_text_dirty_ ||
         std::any_of(dirty_types_.begin(), dirty_types_.end(),
                     [](const bool dirty) { return dirty; });
}

void WDBPersistence::StoreItemTextEntry(const uint64_t guid,
                                        std::vector<uint8_t> data) {
  if (guid == 0 || item_text_cache_ == nullptr) {
    return;
  }

  if (!item_text_cache_->contains(guid)) {
    item_text_persistence_serials_[guid] =
        next_item_text_persistence_serial_++;
  }
  item_text_cache_->insert_or_assign(guid, std::move(data));
  item_text_dirty_ = true;
}

void WDBPersistence::StoreItemTextEntry(
    const uint64_t guid, const std::string_view text) {
  std::vector<std::uint8_t> payload;
  payload.reserve(text.size() + 1u);
  payload.insert(payload.end(), text.begin(), text.end());
  payload.push_back(0);
  StoreItemTextEntry(guid, std::move(payload));
}

void WDBPersistence::ClearItemTextEntries() {
  if (item_text_cache_ != nullptr) {
    item_text_cache_->clear();
  }
  item_text_persistence_serials_.clear();
  next_item_text_persistence_serial_ = 1;
}

void WDBPersistence::SetItemTextDirty() {
  item_text_dirty_ = true;
}

void WDBPersistence::ClearItemTextDirty() {
  item_text_dirty_ = false;
}

bool WDBPersistence::IsItemTextDirty() const {
  return item_text_dirty_;
}

void WDBPersistence::SetCacheVersionToken(const WDBCacheType type,
                                          const uint32_t token) {
  cache_version_tokens_[static_cast<size_t>(type)] = token;
}

uint32_t WDBPersistence::GetCacheVersionToken(const WDBCacheType type) const {
  return cache_version_tokens_[static_cast<size_t>(type)];
}

void WDBPersistence::SetItemTextCacheVersionToken(const uint32_t token) {
  item_text_cache_version_token_ = token;
}

uint32_t WDBPersistence::GetItemTextCacheVersionToken() const {
  return item_text_cache_version_token_;
}

void WDBPersistence::ResetCacheVersionTokens() {
  cache_version_tokens_.fill(0);
  item_text_cache_version_token_ = 0;
}

bool WDBPersistence::SaveMarkedTypes(bool only_dirty_types,
                                     bool include_empty_dirty_types) const {
  if (!cache_) {
    return false;
  }

  bool ok = true;
  for (size_t i = 0; i < dirty_types_.size(); ++i) {
    const auto type = static_cast<WDBCacheType>(i);
    const bool dirty = dirty_types_[i];
    if (only_dirty_types && !dirty) {
      continue;
    }
    if (!include_empty_dirty_types && cache_->GetEntryCount(type) == 0) {
      continue;
    }
    if (!SaveType(type)) {
      ok = false;
    }
  }
  return ok;
}

bool WDBPersistence::FlushIfDirty() {
  if (!IsDirty())
    return true;

  bool typed_ok = true;
  if (std::any_of(dirty_types_.begin(), dirty_types_.end(),
                  [](const bool dirty) { return dirty; })) {
    typed_ok = SaveMarkedTypes(true, true);
    if (typed_ok) {
      dirty_types_.fill(false);
    }
  }

  bool item_text_ok = true;
  if (item_text_dirty_) {
    item_text_ok = item_text_cache_ != nullptr &&
                   SaveItemTextCache(*item_text_cache_);
    if (item_text_ok) {
      item_text_dirty_ = false;
    }
  }

  return typed_ok && item_text_ok;
}

void WDBPersistence::DestroyType(const WDBCacheType type) {
  const auto index = static_cast<std::size_t>(type);
  if (cache_ != nullptr) {
    if (dirty_types_[index]) {
      (void)SaveType(type);
    }
    cache_->ClearType(type);
  }
  dirty_types_[index] = false;
}

void WDBPersistence::DestroyItemTextCache() {
  if (item_text_cache_ != nullptr) {
    if (item_text_dirty_) {
      (void)SaveItemTextCache(*item_text_cache_);
    }
    ClearItemTextEntries();
  }
  item_text_dirty_ = false;
}

void WDBPersistence::DestroyAll() {
  constexpr std::array<WDBCacheType, 11> kBeforeItemText = {
      WDBCacheType::Creature, WDBCacheType::GameObject, WDBCacheType::Item,
      WDBCacheType::ItemName, WDBCacheType::NpcText,    WDBCacheType::Name,
      WDBCacheType::Guild,    WDBCacheType::Quest,      WDBCacheType::PageText,
      WDBCacheType::PetName,  WDBCacheType::Petition,
  };
  for (const auto type : kBeforeItemText) {
    DestroyType(type);
  }
  DestroyItemTextCache();
  DestroyType(WDBCacheType::ArenaTeam);
}

}
