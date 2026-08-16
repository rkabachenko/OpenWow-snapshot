#include "openwow/game/battlenet_plugin.h"

#include "openwow/platform/dynamic_libraries/dynamic_library.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace openwow::game {
namespace detail {

struct BattleNetPluginSharedState {
  mutable std::mutex mutex;
  std::unique_ptr<IBattleNetPluginLoader> loader;
  std::unique_ptr<IBattleNetPluginModule> module;
  BattleNetPluginExports exports{};
  std::size_t active_connections{0};
};

}

namespace {

class DynamicBattleNetModule final : public IBattleNetPluginModule {
public:
  explicit DynamicBattleNetModule(
      std::unique_ptr<platform::DynamicLibrary> library)
      : library_(std::move(library)) {}

  void* ResolveOrdinal(std::uint32_t ordinal) const override {
#ifdef _WIN32
    return library_->FindOrdinal(ordinal);
#else
    std::array<char, 32> symbol{};
    std::snprintf(symbol.data(), symbol.size(), "ordinal%05u", ordinal);
    return library_->FindSymbol(symbol.data());
#endif
  }

private:
  std::unique_ptr<platform::DynamicLibrary> library_;
};

#ifdef __APPLE__
std::filesystem::path MainBundleFrameworksDirectory() {
  CFBundleRef bundle = CFBundleGetMainBundle();
  if (!bundle) {
    return {};
  }
  CFURLRef url = CFBundleCopyBundleURL(bundle);
  if (!url) {
    return {};
  }
  CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
  CFRelease(url);
  if (!path) {
    return {};
  }

  std::array<char, 4096> utf8{};
  const bool converted =
      CFStringGetCString(path, utf8.data(), utf8.size(), kCFStringEncodingUTF8);
  CFRelease(path);
  if (!converted) {
    return {};
  }
  return std::filesystem::path(utf8.data()) / "Contents" / "Frameworks";
}
#endif

class DynamicBattleNetLoader final : public IBattleNetPluginLoader {
public:
  std::unique_ptr<IBattleNetPluginModule>
  Open(std::string_view base_name) override {
#ifdef __APPLE__

    const std::string file_name = std::string(base_name) + ".bundle";
    std::array<std::filesystem::path, 2> candidates{};
    const std::filesystem::path frameworks = MainBundleFrameworksDirectory();
    if (!frameworks.empty()) {
      candidates[0] = frameworks / file_name;
    }
    std::error_code cwd_error;
    const std::filesystem::path cwd = std::filesystem::current_path(cwd_error);
    if (!cwd_error) {
      candidates[1] = cwd / file_name;
    }
    for (const auto& candidate : candidates) {
      if (candidate.empty()) {
        continue;
      }
      if (auto library = platform::DynamicLibrary::Open(candidate.string())) {
        return std::make_unique<DynamicBattleNetModule>(std::move(library));
      }
    }
    return nullptr;
#else
    if (auto library = platform::DynamicLibrary::OpenPlugin(base_name)) {
      return std::make_unique<DynamicBattleNetModule>(std::move(library));
    }
    return nullptr;
#endif
  }
};

template <typename Function>
Function Resolve(const IBattleNetPluginModule& module, std::uint32_t ordinal) {
  return reinterpret_cast<Function>(module.ResolveOrdinal(ordinal));
}

bool LoadLocked(detail::BattleNetPluginSharedState& state) {
  if (state.module) {
    return true;
  }
  if (!state.loader) {
    return false;
  }

  static constexpr std::array<std::string_view, 2> kNames = {
      "Battle.net-prearxan", "Battle.net"};
  for (const std::string_view name : kNames) {
    auto module = state.loader->Open(name);
    if (!module) {
      continue;
    }
    BattleNetPluginExports exports{
        Resolve<BNetPluginInitFn>(*module, 1),
        Resolve<BNetPluginShutdownFn>(*module, 2),
        Resolve<BNetPluginCreateConnectionFn>(*module, 3),
        Resolve<BNetPluginDestroyConnectionFn>(*module, 4),
    };
    if (!exports.Complete() || !exports.initialize()) {
      return false;
    }
    state.exports = exports;
    state.module = std::move(module);
    return true;
  }
  return false;
}

void UnloadLocked(detail::BattleNetPluginSharedState& state) {
  if (!state.module) {
    return;
  }
  state.exports.shutdown();
  state.exports = {};
  state.module.reset();
}

}

BattleNetPluginConnection::~BattleNetPluginConnection() {
  if (!state_) {
    return;
  }
  std::lock_guard lock(state_->mutex);
  if (state_->active_connections == 0) {
    return;
  }
  state_->exports.destroy_connection(handle_);
  --state_->active_connections;
  if (state_->active_connections == 0) {
    UnloadLocked(*state_);
  }
}

BattleNetPluginRuntime::BattleNetPluginRuntime()
    : BattleNetPluginRuntime(std::make_unique<DynamicBattleNetLoader>()) {}

BattleNetPluginRuntime::BattleNetPluginRuntime(
    std::unique_ptr<IBattleNetPluginLoader> loader)
    : state_(std::make_shared<detail::BattleNetPluginSharedState>()) {
  state_->loader = std::move(loader);
}

BattleNetPluginRuntime::~BattleNetPluginRuntime() {
  UnloadIfIdle();
}

bool BattleNetPluginRuntime::Load() {
  std::lock_guard lock(state_->mutex);
  return LoadLocked(*state_);
}

std::unique_ptr<BattleNetPluginConnection>
BattleNetPluginRuntime::CreateConnection(
    std::span<const std::uint8_t> serialized_node, void* callbacks) {
  if (serialized_node.size() > std::numeric_limits<std::uint32_t>::max()) {
    return nullptr;
  }

  std::lock_guard lock(state_->mutex);
  if (!LoadLocked(*state_)) {
    return nullptr;
  }

  ++state_->active_connections;
  void* handle = state_->exports.create_connection(
      serialized_node.data(), static_cast<std::uint32_t>(serialized_node.size()),
      callbacks);
  return std::unique_ptr<BattleNetPluginConnection>(
      new BattleNetPluginConnection(state_, handle));
}

void BattleNetPluginRuntime::UnloadIfIdle() {
  std::lock_guard lock(state_->mutex);
  if (state_->active_connections == 0) {
    UnloadLocked(*state_);
  }
}

BattleNetPluginSnapshot BattleNetPluginRuntime::Snapshot() const {
  std::lock_guard lock(state_->mutex);
  return {state_->module != nullptr, state_->active_connections};
}

BattleNetPluginRuntime& GetBattleNetPluginRuntime() {
  static BattleNetPluginRuntime runtime;
  return runtime;
}

bool BattleNet_LoadPlugin() {
  return GetBattleNetPluginRuntime().Load();
}

void BattleNet_UnloadPlugin() {
  GetBattleNetPluginRuntime().UnloadIfIdle();
}

}
