#include "openwow/platform/dynamic_libraries/dynamic_library.h"

#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace openwow::platform {
namespace {

[[nodiscard]] void* OpenNativeLibrary(const char* path) {
#ifdef _WIN32
  const int wide_length =
      ::MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
  if (wide_length <= 0) {
    return nullptr;
  }

  std::wstring wide_path(static_cast<std::size_t>(wide_length), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path.data(),
                            wide_length) <= 0) {
    return nullptr;
  }
  return static_cast<void*>(::LoadLibraryW(wide_path.c_str()));
#else
  return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void CloseNativeLibrary(void* handle) noexcept {
  if (handle == nullptr) {
    return;
  }
#ifdef _WIN32
  ::FreeLibrary(static_cast<HMODULE>(handle));
#else
  ::dlclose(handle);
#endif
}

[[nodiscard]] void* FindNativeSymbol(void* handle, const char* name) {
  if (handle == nullptr || name == nullptr) {
    return nullptr;
  }
#ifdef _WIN32
  return reinterpret_cast<void*>(
      ::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
  return ::dlsym(handle, name);
#endif
}

[[nodiscard]] void* FindNativeOrdinal(
    [[maybe_unused]] void* handle,
    [[maybe_unused]] const std::uint32_t ordinal) {
#ifdef _WIN32
  if (handle == nullptr || ordinal > 0xFFFFu) {
    return nullptr;
  }
  return reinterpret_cast<void*>(::GetProcAddress(
      static_cast<HMODULE>(handle), MAKEINTRESOURCEA(ordinal)));
#else
  return nullptr;
#endif
}

[[nodiscard]] constexpr std::string_view SharedLibrarySuffix() noexcept {
#ifdef _WIN32
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

}

DynamicLibrary::DynamicLibrary(void* handle) noexcept : handle_(handle) {}

DynamicLibrary::~DynamicLibrary() { CloseNativeLibrary(handle_); }

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    CloseNativeLibrary(handle_);
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

std::unique_ptr<DynamicLibrary> DynamicLibrary::Open(
    const std::string_view utf8_path) {
  const std::string null_terminated_path(utf8_path);
  void* handle = OpenNativeLibrary(null_terminated_path.c_str());
  if (handle == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(handle));
}

std::unique_ptr<DynamicLibrary> DynamicLibrary::OpenPlugin(
    const std::string_view base_name) {
  std::string path(base_name);
  path.append(SharedLibrarySuffix());
  return Open(path);
}

void* DynamicLibrary::FindOrdinal(const std::uint32_t ordinal) const {
  return FindNativeOrdinal(handle_, ordinal);
}

void* DynamicLibrary::FindSymbol(const char* name) const {
  return FindNativeSymbol(handle_, name);
}

}
