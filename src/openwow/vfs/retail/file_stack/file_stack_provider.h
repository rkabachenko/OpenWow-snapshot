#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace openwow::vfs {

class RuntimeFile;
class FileStackEventRecord;

using FileStackDispatchFn = bool (*)(void *, void *);

void *GetActiveFileStackCallbackTable();
void *GetDefaultFileStackCallbackTable();
void SetActiveFileStackCallbackTable(void *callback_table);
void SetActiveFileStackCallbackTableForTests(void *callback_table);
void ResetActiveFileStackCallbackTableForTests();
void ShutdownFileStackProviderChain();

std::uint32_t RegisterFileStackCompatPointer(const void *pointer);
void UnregisterFileStackCompatPointer(std::uint32_t token);
void *ResolveFileStackCompatPointer(std::uint32_t token);
std::uint32_t RegisterFileStackDispatch(FileStackDispatchFn dispatch);
FileStackDispatchFn ResolveFileStackDispatch(std::uint32_t token);
void WriteFileStackDispatchSlot(void *callback_table, std::size_t slot_offset,
                                FileStackDispatchFn dispatch);
const void *ReadFileStackNextProvider(const void *callback_table);
FileStackDispatchFn ReadFileStackDispatchSlot(const void *callback_table,
                                              std::size_t slot_offset);
FileStackDispatchFn ReadFileStackDirectDispatchSlot(const void *callback_table,
                                                    std::size_t slot_offset);
bool DispatchFileStackEvent(void *callback_table, std::size_t slot_offset,
                            FileStackEventRecord &event_record);

class FileStackEventRecord {
public:
  static constexpr std::size_t kSize = 0x90;
  ~FileStackEventRecord() {
    for (const std::uint32_t token : pointer_tokens_) {
      UnregisterFileStackCompatPointer(token);
    }
  }
  void *data() { return storage_.data(); }
  template <typename T> void Write(std::size_t offset, const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr (std::is_pointer_v<T>) {
      const std::uint32_t token = RegisterFileStackCompatPointer(value);
      if (token != 0) pointer_tokens_.push_back(token);
      std::memcpy(storage_.data() + offset, &token, sizeof(token));
    } else {
      std::memcpy(storage_.data() + offset, &value, sizeof(value));
    }
  }
private:
  std::array<std::byte, kSize> storage_{};
  std::vector<std::uint32_t> pointer_tokens_;
};

template <typename T>
T ReadFileStackField(const void *record, std::size_t offset) {
  T value{};
  if (!record) {
    return value;
  }
  if constexpr (std::is_pointer_v<T>) {
    std::uint32_t token = 0;
    std::memcpy(&token, static_cast<const std::byte *>(record) + offset, sizeof(token));
    if (void *resolved = ResolveFileStackCompatPointer(token); resolved != nullptr) {
      return static_cast<T>(resolved);
    }
  }
  std::memcpy(&value, static_cast<const std::byte *>(record) + offset, sizeof(value));
  return value;
}

void SetIOUnitContainerFileStackCallbackTableForTests(void *callback_table);
void ResetIOUnitContainerFileStackCallbackTableForTests();
void *GetIOUnitContainerFileStackCallbackTableForTests();
void WriteFileStackDispatchSlotForTests(void *callback_table, std::size_t slot_offset,
                                        FileStackDispatchFn dispatch);
std::uint32_t RegisterFileStackCompatPointerForTests(const void *pointer);
void *ResolveFileStackCompatPointerForTests(std::uint32_t token);

bool OpenRuntimeFileFromFileStackOpen(const char *logical_path,
                                      std::uint32_t open_flags, int *out_handle);

}
