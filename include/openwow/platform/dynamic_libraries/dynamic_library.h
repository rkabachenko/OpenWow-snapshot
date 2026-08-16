#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace openwow::platform {

class DynamicLibrary final {
 public:
  ~DynamicLibrary();

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  DynamicLibrary(DynamicLibrary&& other) noexcept;
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

  [[nodiscard]] static std::unique_ptr<DynamicLibrary> Open(
      std::string_view utf8_path);
  [[nodiscard]] static std::unique_ptr<DynamicLibrary> OpenPlugin(
      std::string_view base_name);

  [[nodiscard]] void* FindOrdinal(std::uint32_t ordinal) const;
  [[nodiscard]] void* FindSymbol(const char* name) const;

  template <typename Function>
  [[nodiscard]] Function FindFunction(const char* name) const {
    return reinterpret_cast<Function>(FindSymbol(name));
  }

  [[nodiscard]] bool loaded() const noexcept { return handle_ != nullptr; }
  explicit operator bool() const noexcept { return loaded(); }

 private:
  explicit DynamicLibrary(void* handle) noexcept;

  void* handle_{nullptr};
};

}
