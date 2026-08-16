#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::data {

struct WowClientDbStorage;

struct WowClientDB {
  int loaded_flag = 0;
  int record_count = 0;
  int max_id = -1;
  int min_id = 0x0FFFFFFF;
  char *string_table = nullptr;
  int record_size = 0;
  void *record_data = nullptr;
  void **index = nullptr;
  std::shared_ptr<WowClientDbStorage> storage;
};

inline constexpr int kWowClientDbInitialMaxId = -1;
inline constexpr int kWowClientDbInitialMinId = 0x0FFFFFFF;

void BindErrorTableVfs(const openwow::vfs::VirtualFileSystem *vfs);
void ResetErrorTableForTests();
void SetErrorTableLocaleIndexForTests(int locale_index);

int InitErrorTable(WowClientDB *db, const char *source_file,
                   std::uint32_t exit_code);
void WowClientDB_Unload(WowClientDB *db, const char *source_file);

[[nodiscard]] const char *LookupErrorTableText(const WowClientDB *db,
                                                std::uint32_t id);
[[nodiscard]] const char *ResolveStartupWindowTitle(
    const WowClientDB *db, const char *fallback = "World of Warcraft");

[[nodiscard]] bool WowClientDB_IsCompressionEnabled();
void WowClientDB_SetCompressionEnabled(bool enabled);

}
