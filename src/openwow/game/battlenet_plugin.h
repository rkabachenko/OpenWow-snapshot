#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace openwow::game {

namespace detail {
struct BattleNetPluginSharedState;
}

using BNetPluginInitFn = bool (*)();
using BNetPluginShutdownFn = void (*)();
using BNetPluginCreateConnectionFn = void* (*)(const std::uint8_t* bytes,
                                                std::uint32_t byte_count,
                                                void* callbacks);
using BNetPluginDestroyConnectionFn = void (*)(void* connection);

struct BattleNetPluginExports {
  BNetPluginInitFn initialize{nullptr};
  BNetPluginShutdownFn shutdown{nullptr};
  BNetPluginCreateConnectionFn create_connection{nullptr};
  BNetPluginDestroyConnectionFn destroy_connection{nullptr};

  [[nodiscard]] bool Complete() const noexcept {
    return initialize && shutdown && create_connection && destroy_connection;
  }
};

class IBattleNetPluginModule {
public:
  virtual ~IBattleNetPluginModule() = default;
  [[nodiscard]] virtual void* ResolveOrdinal(std::uint32_t ordinal) const = 0;
};

class IBattleNetPluginLoader {
public:
  virtual ~IBattleNetPluginLoader() = default;
  [[nodiscard]] virtual std::unique_ptr<IBattleNetPluginModule>
  Open(std::string_view base_name) = 0;
};

class BattleNetPluginRuntime;

class BattleNetPluginConnection final {
public:
  ~BattleNetPluginConnection();

  BattleNetPluginConnection(const BattleNetPluginConnection&) = delete;
  BattleNetPluginConnection& operator=(const BattleNetPluginConnection&) = delete;
  BattleNetPluginConnection(BattleNetPluginConnection&&) = delete;
  BattleNetPluginConnection& operator=(BattleNetPluginConnection&&) = delete;

  [[nodiscard]] bool IsConnected() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] void* NativeHandle() const noexcept { return handle_; }

private:
  friend class BattleNetPluginRuntime;
  BattleNetPluginConnection(
      std::shared_ptr<detail::BattleNetPluginSharedState> state, void* handle)
      : state_(std::move(state)), handle_(handle) {}

  std::shared_ptr<detail::BattleNetPluginSharedState> state_;
  void* handle_;
};

struct BattleNetPluginSnapshot {
  bool loaded{false};
  std::size_t active_connections{0};
};

class BattleNetPluginRuntime final {
public:
  BattleNetPluginRuntime();
  explicit BattleNetPluginRuntime(std::unique_ptr<IBattleNetPluginLoader> loader);
  ~BattleNetPluginRuntime();

  BattleNetPluginRuntime(const BattleNetPluginRuntime&) = delete;
  BattleNetPluginRuntime& operator=(const BattleNetPluginRuntime&) = delete;

  [[nodiscard]] bool Load();

  [[nodiscard]] std::unique_ptr<BattleNetPluginConnection> CreateConnection(
      std::span<const std::uint8_t> serialized_node, void* callbacks);

  void UnloadIfIdle();

  [[nodiscard]] BattleNetPluginSnapshot Snapshot() const;

private:
  std::shared_ptr<detail::BattleNetPluginSharedState> state_;
};

BattleNetPluginRuntime& GetBattleNetPluginRuntime();

[[nodiscard]] bool BattleNet_LoadPlugin();
void BattleNet_UnloadPlugin();

}
