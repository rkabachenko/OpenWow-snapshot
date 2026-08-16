#include "openwow/data/formats/dbc/dbc_loader.h"

#include "openwow/core/storm_error.h"
#include "openwow/data/loading/dbc_locale_adapter.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openwow::data::dbc {

namespace {

constexpr std::uint32_t kDbcVersionMismatchError = 0x85100079u;

#include "dbc_retail_catalog.inc"

bool ReadHeaderWord(const std::vector<std::uint8_t> &bytes, std::size_t *const cursor,
                    std::uint32_t *const out) {
  if (*cursor > bytes.size() || bytes.size() - *cursor < sizeof(*out)) {
    return false;
  }

  const auto *const value = bytes.data() + *cursor;
  *out = static_cast<std::uint32_t>(value[0]) |
         (static_cast<std::uint32_t>(value[1]) << 8u) |
         (static_cast<std::uint32_t>(value[2]) << 16u) |
         (static_cast<std::uint32_t>(value[3]) << 24u);
  *cursor += sizeof(*out);
  return true;
}

std::string BuildVfsPath(const std::string &root, const RetailDbcDescriptor &descriptor) {
  std::string path = root;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path.push_back('/');
  }
  path.append(descriptor.filename());
  return path;
}

template <typename T>
bool LoadRetailStrictDbcStore(DbcStore<T> &store, const openwow::vfs::VirtualFileSystem &vfs,
                              const std::string &path,
                              const DbcTableSchema<T> &schema) {
  if (!store.empty()) {
    return true;
  }

  const char *const retail_path = schema.retail_path.data();
  auto bytes = vfs.ReadFileBytes(path);
  if (!bytes.has_value()) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError, "Unable to open %s",
                                        retail_path);
  }

  std::size_t cursor = 0;
  std::uint32_t signature = 0;
  if (!ReadHeaderWord(*bytes, &cursor, &signature)) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError,
                                        "Unable to read signature from %s", retail_path);
  }
  if (signature != DbcHeader::kWdbcSignature) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError,
                                        "Invalid signature 0x%x from %s", signature,
                                        retail_path);
  }

  std::uint32_t record_count = 0;
  if (!ReadHeaderWord(*bytes, &cursor, &record_count)) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError,
                                        "Unable to read record count from %s", retail_path);
  }

  if (record_count == 0u) {
    return true;
  }

  std::uint32_t field_count = 0;
  if (!ReadHeaderWord(*bytes, &cursor, &field_count)) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError,
                                        "Unable to read column count from %s", retail_path);
  }
  if (field_count != schema.field_count) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError,
        "%s has wrong number of columns (found %i, expected %i)", retail_path,
        static_cast<int>(field_count), static_cast<int>(schema.field_count));
  }

  std::uint32_t record_size = 0;
  if (!ReadHeaderWord(*bytes, &cursor, &record_size)) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError,
                                        "Unable to read row size from %s", retail_path);
  }
  if (record_size != schema.record_size) {
    openwow::core::SErrFatalError_VArgs(
        kDbcVersionMismatchError, "%s has wrong row size (found %i, expected %i)", retail_path,
        static_cast<int>(record_size), static_cast<int>(schema.record_size));
  }

  std::uint32_t string_block_size = 0;
  if (!ReadHeaderWord(*bytes, &cursor, &string_block_size)) {
    openwow::core::SErrFatalError_VArgs(kDbcVersionMismatchError,
                                        "Unable to read string size from %s", retail_path);
  }

  const auto expected_size =
      static_cast<std::uint64_t>(DbcHeader::kEncodedSize) +
      static_cast<std::uint64_t>(record_count) * record_size +
      string_block_size;
  if (expected_size > static_cast<std::uint64_t>(bytes->size())) {
    openwow::core::SErrFatalCondition("%s: Cannot read string table", retail_path);
  }

  DbcFile file{openwow::data::loading::CurrentDbcLocale()};
  if (file.LoadFromBytes(std::move(*bytes)) != DbcError::kOk ||
      !store.LoadFromFile(std::move(file), schema, path)) {
    openwow::core::SErrFatalCondition("%s: Cannot read string table", retail_path);
  }

  return true;
}

}

template <typename T>
bool DbcLoader::LoadOne(DbcStore<T> &store, const openwow::vfs::VirtualFileSystem &vfs,
                        const std::string &path, const RetailDbcDescriptor &descriptor) {
  const DbcTableSchema<T> schema{
      .retail_path = descriptor.retail_path,
      .field_count = descriptor.field_count,
      .record_size = descriptor.record_size,
      .decode = &T::Load,
  };
  return LoadRetailStrictDbcStore(store, vfs, path, schema);
}

int DbcLoader::LoadAll(const openwow::vfs::VirtualFileSystem &vfs,
                       const std::string &dbc_root_path) {
  using openwow::diagnostics::Log;
  using openwow::diagnostics::LogLevel;

  vfs_ = &vfs;
  int loaded = 0;

  Log(LogLevel::kInfo, "DBC: Loading all client data files from '" + dbc_root_path + "' ...");

  const auto load = [&](auto &store, const RetailDbcDescriptor &descriptor) {
    if (LoadOne(store, vfs, BuildVfsPath(dbc_root_path, descriptor), descriptor)) {
      ++loaded;
    }
  };

#define OPENWOW_LOAD_DBC(type, member, path, fields, size)                                          \
  load(static_cast<DbcStore<type> &>(member),                                  \
       RetailDbcDescriptor{path, fields, size});
  OPENWOW_RETAIL_DBC_CATALOG(OPENWOW_LOAD_DBC)
#undef OPENWOW_LOAD_DBC

  Log(LogLevel::kInfo,
      "DBC: Finished. " + std::to_string(loaded) + " loaded, 0 failed.");
  return loaded;
}

bool DbcLoader::LoadAreaTableRetailStrict(const openwow::vfs::VirtualFileSystem &vfs,
                                          const std::string &path) {
  vfs_ = &vfs;
  return LoadOne(core_.area_table_, vfs, path,
                 FindRetailDbcDescriptor("AreaTable.dbc"));
}

#undef OPENWOW_RETAIL_DBC_CATALOG

}
