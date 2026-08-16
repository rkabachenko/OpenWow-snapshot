#include "openwow/data/dbc_loader.h"

#include "openwow/core/storm_error.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::data {
namespace {

constexpr std::uint32_t kStartupWindowTitleId = 1;
constexpr std::uint32_t kDbcVersionMismatchError = 0x85100079u;

enum class StartupStringField : std::uint32_t {
  kId,
  kName,
  kLocalizedText,
};

constexpr std::uint32_t FieldIndex(const StartupStringField field) {
  return static_cast<std::uint32_t>(field);
}

struct StartupStringRecord {
  std::uint32_t id;
  const char *name;
  const char *text;
};

}

struct WowClientDbStorage {
  std::vector<StartupStringRecord> records;
  std::vector<char> strings;
  std::vector<void *> index;
};

namespace {

const openwow::vfs::VirtualFileSystem *&BoundVfs() {
  static const openwow::vfs::VirtualFileSystem *vfs = nullptr;
  return vfs;
}

int &ForcedLocale() {
  static int locale = -1;
  return locale;
}

bool &CompressionEnabled() {
  static bool enabled = true;
  return enabled;
}

const dbc::RetailDbcDescriptor &StartupStringsSchema() {
  return dbc::FindRetailDbcDescriptor("Startup_Strings.dbc");
}

dbc::DbcLocale CurrentLocale() {

  const int locale = ForcedLocale() >= 0
                         ? std::clamp(ForcedLocale(), dbc::kFirstDbcLocaleIndex,
                                      dbc::kLastDbcLocaleIndex)
                         : std::clamp(GetCurrentLocaleInfo().locale_index,
                                      dbc::kFirstDbcLocaleIndex,
                                      dbc::kLastDbcLocaleIndex);
  return static_cast<dbc::DbcLocale>(locale);
}

char *AppendString(char *destination, const std::string_view value) {
  if (!value.empty()) {
    std::memcpy(destination, value.data(), value.size());
  }
  destination[value.size()] = '\0';
  return destination + value.size() + 1u;
}

void Reset(WowClientDB &db) {
  db.loaded_flag = 0;
  db.record_count = 0;
  db.max_id = kWowClientDbInitialMaxId;
  db.min_id = kWowClientDbInitialMinId;
  db.string_table = nullptr;
  db.record_size = 0;
  db.record_data = nullptr;
  db.index = nullptr;
  db.storage.reset();
}

}

void BindErrorTableVfs(const openwow::vfs::VirtualFileSystem *vfs) {
  BoundVfs() = vfs;
}

void ResetErrorTableForTests() {
  BoundVfs() = nullptr;
  ForcedLocale() = -1;
}

void SetErrorTableLocaleIndexForTests(const int locale_index) {
  ForcedLocale() = locale_index;
}

int InitErrorTable(WowClientDB *const db, const char *,
                   const std::uint32_t) {
  const auto &schema = StartupStringsSchema();
  const char *const retail_path = schema.retail_path.data();
  if (db == nullptr) {
    openwow::core::SErrFatalCondition(
        "%s: invalid database load request", retail_path);
  }
  if (db->loaded_flag != 0) {
    return 1;
  }

  const auto *const vfs = BoundVfs();
  if (vfs == nullptr) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError, "Unable to open %s",
        retail_path);
  }
  auto bytes = vfs->ReadFileBytes(retail_path);
  if (!bytes.has_value()) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError, "Unable to open %s",
        retail_path);
  }

  dbc::DbcFile file{CurrentLocale()};
  const auto error = file.LoadFromBytes(std::move(*bytes));
  if (error != dbc::DbcError::kOk) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError, "Invalid DBC %s (%s)",
        retail_path, dbc::DbcFile::GetErrorName(error));
  }
  if (!file.loaded()) {
    return 1;
  }
  if (file.field_count() != schema.field_count) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError,
        "%s has wrong number of columns (found %i, expected %i)",
        retail_path, static_cast<int>(file.field_count()),
        static_cast<int>(schema.field_count));
  }
  if (file.record_size() != schema.record_size) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError,
        "%s has wrong row size (found %i, expected %i)",
        retail_path, static_cast<int>(file.record_size()),
        static_cast<int>(schema.record_size));
  }
  if (file.record_count() >
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    openwow::core::SErrFatalCondition("%s: Record count is too large",
                                      retail_path);
  }

  std::size_t string_bytes = 0;
  for (std::uint32_t row = 0; row < file.record_count(); ++row) {
    string_bytes +=
        file.GetString(row, FieldIndex(StartupStringField::kName)).size() + 1u;
    string_bytes += file
                        .GetLocalizedString(
                            row,
                            FieldIndex(StartupStringField::kLocalizedText))
                        .size() +
                    1u;
  }

  auto storage = std::make_shared<WowClientDbStorage>();
  storage->records.resize(file.record_count());
  storage->strings.resize(string_bytes);
  char *next_string = storage->strings.data();
  auto min_id = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t max_id = 0;
  for (std::uint32_t row = 0; row < file.record_count(); ++row) {
    auto &record = storage->records[row];
    record.id = file.GetUInt32(row, FieldIndex(StartupStringField::kId));
    record.name = next_string;
    next_string = AppendString(
        next_string,
        file.GetString(row, FieldIndex(StartupStringField::kName)));
    record.text = next_string;
    next_string = AppendString(
        next_string,
        file.GetLocalizedString(
            row, FieldIndex(StartupStringField::kLocalizedText)));
    min_id = std::min(min_id, record.id);
    max_id = std::max(max_id, record.id);
  }

  const auto index_size = static_cast<std::size_t>(
      static_cast<std::uint64_t>(max_id) - min_id + 1u);
  storage->index.resize(index_size);

  for (std::uint32_t row = 0; row < file.record_count(); ++row) {
    storage->index[storage->records[row].id - min_id] =
        &storage->records[row];
  }

  db->storage = std::move(storage);
  db->loaded_flag = 1;
  db->record_count = static_cast<int>(file.record_count());
  db->max_id = static_cast<int>(max_id);
  db->min_id = static_cast<int>(min_id);
  db->string_table = db->storage->strings.data();
  db->record_size = static_cast<int>(sizeof(StartupStringRecord));
  db->record_data = db->storage->records.data();
  db->index = db->storage->index.data();
  return 1;
}

void WowClientDB_Unload(WowClientDB *const db, const char *) {
  if (db == nullptr) {
    return;
  }
  Reset(*db);
}

const char *LookupErrorTableText(const WowClientDB *const db,
                                 const std::uint32_t id) {
  if (db == nullptr || db->loaded_flag == 0 || db->index == nullptr ||
      id < static_cast<std::uint32_t>(db->min_id) ||
      id > static_cast<std::uint32_t>(db->max_id)) {
    return nullptr;
  }
  const auto index = static_cast<std::size_t>(
      static_cast<std::uint64_t>(id) -
      static_cast<std::uint32_t>(db->min_id));
  const auto *const record =
      static_cast<const StartupStringRecord *>(db->index[index]);
  return record != nullptr ? record->text : nullptr;
}

const char *ResolveStartupWindowTitle(const WowClientDB *const db,
                                      const char *const fallback) {
  if (const char *const title =
          LookupErrorTableText(db, kStartupWindowTitleId);
      title != nullptr) {
    return title;
  }
  return fallback;
}

bool WowClientDB_IsCompressionEnabled() {
  return CompressionEnabled();
}

void WowClientDB_SetCompressionEnabled(const bool enabled) {
  CompressionEnabled() = enabled;
}

}
