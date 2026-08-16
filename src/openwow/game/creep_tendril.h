#pragma once

#include "openwow/game/battlenet_plugin.h"
#include "openwow/game/tumor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace openwow::game {

enum class TumorEventCode : std::uint8_t {
  kCode1 = 1,
  kCode2 = 2,
  kCode3 = 3,
  kCode5 = 5,
  kCode6 = 6,
  kCode8 = 8,
  kCode9 = 9,
  kCode10 = 10,
  kCode11 = 11,
  kCode14 = 14,
  kCode15 = 15,
};

inline constexpr std::size_t kCreepNodeWordCount = 451;
using CreepNodeData = std::array<std::uint32_t, kCreepNodeWordCount>;

enum class CreepEventDispatchResult : std::uint8_t {
  kDispatched,
  kUnknownEvent,
  kUnknownMessageType,
  kInvalidPayload,
};

class ICreepTendrilEventSink {
public:
  virtual ~ICreepTendrilEventSink() = default;
  virtual int OnCreepTendrilReference(TumorEventCode event_code,
                                      const std::uint32_t& payload,
                                      std::uintptr_t context) = 0;
  virtual int OnCreepTendrilValue(TumorEventCode event_code,
                                  std::uint32_t payload,
                                  std::uintptr_t context) = 0;
};

enum class CreepAdapterMethod : std::uint8_t {
  kSlot0 = 0,
  kSlot2 = 2,
  kSlot3 = 3,
  kSlot4 = 4,
  kSlot5 = 5,
  kSlot7 = 7,
  kSlot8 = 8,
  kSlot9 = 9,
  kSlot11 = 11,
  kSlot12 = 12,
};

struct DecodedCreepPluginEvent {
  std::uint8_t event_id{0};
  TumorMessageType message_type{TumorMessageType::kByReference};
  std::uint32_t payload{0};
};

class ICreepTendrilCallbackTarget {
public:
  virtual ~ICreepTendrilCallbackTarget() = default;
  virtual std::uintptr_t ForwardCreepAdapter(CreepAdapterMethod method,
                                             std::uintptr_t first,
                                             std::uintptr_t second) = 0;
  virtual std::uintptr_t ResolveCreepAddress(
      std::array<std::uint8_t, 6> address,
      std::uintptr_t callback_context) = 0;
  virtual void SendToCreepAddress(
      std::uintptr_t first, std::uintptr_t second, std::uintptr_t third,
      std::array<std::uint8_t, 6> address) = 0;
  virtual bool DecodeIncomingCreepEvent(
      void* reflected_event, DecodedCreepPluginEvent& decoded) = 0;
};

class RetailCreepPluginCallbacks {
public:
  using AbiFunction = void (*)();

  void CompleteDestructor();
  void DeletingDestructor();
  std::uintptr_t Forward0();
  std::uintptr_t ResolveAddress(const std::uint8_t*, std::uint32_t);
  std::uintptr_t Forward2();
  std::uintptr_t Forward3();
  std::uintptr_t Forward4(std::uintptr_t);
  std::uintptr_t Forward5();
  void SendAddress(std::uintptr_t, std::uintptr_t, std::uintptr_t,
                   const std::uint8_t*, std::uint32_t);
  std::uintptr_t Forward7();
  std::uintptr_t Forward8();
  std::uintptr_t Forward9(std::uintptr_t, std::uintptr_t);
  std::uintptr_t Forward9WithKind5(std::uintptr_t);
  std::uintptr_t Forward11(std::uintptr_t, std::uintptr_t);
  bool Forward12(std::uintptr_t);
  void IncomingEvent(void* reflected_event, std::uintptr_t context);

private:
  friend class CreepTendrilConnection;
  const AbiFunction* vptr_{nullptr};
  void* bridge_{nullptr};
};

class CreepTendrilConnection final : public ITumorTendril {
public:
  [[nodiscard]] static std::shared_ptr<CreepTendrilConnection> Create(
      const CreepNodeData& node,
      std::shared_ptr<ICreepTendrilEventSink> event_sink,
      std::shared_ptr<ICreepTendrilCallbackTarget> callback_target,
      BattleNetPluginRuntime& plugin_runtime = GetBattleNetPluginRuntime());

  ~CreepTendrilConnection() override = default;

  CreepTendrilConnection(const CreepTendrilConnection&) = delete;
  CreepTendrilConnection& operator=(const CreepTendrilConnection&) = delete;

  void OnAddedToTumorManager() override { added_to_manager_ = true; }

  [[nodiscard]] CreepEventDispatchResult DispatchIncomingEvent(
      std::uint8_t event_id, TumorMessageType message_type,
      const std::uint32_t* payload, std::uintptr_t context);

  [[nodiscard]] bool IsPluginConnected() const noexcept {
    return plugin_connection_ && plugin_connection_->IsConnected();
  }
  [[nodiscard]] bool HasPluginLease() const noexcept {
    return plugin_connection_ != nullptr;
  }
  [[nodiscard]] bool WasAddedToManager() const noexcept {
    return added_to_manager_;
  }
  [[nodiscard]] std::span<const std::uint32_t, kCreepNodeWordCount> Node() const {
    return node_;
  }
  [[nodiscard]] std::span<const std::uint8_t> SerializedNode() const {
    return serialized_node_;
  }

private:
  static_assert(offsetof(RetailCreepPluginCallbacks, vptr_) == 0);
  static void InitializeRetailCallbacks(
      RetailCreepPluginCallbacks& callbacks,
      const RetailCreepPluginCallbacks::AbiFunction* vtable,
      void* bridge) noexcept {
    callbacks.vptr_ = vtable;
    callbacks.bridge_ = bridge;
  }
  static void* RetailCallbacksBridge(
      RetailCreepPluginCallbacks& callbacks) noexcept {
    return callbacks.bridge_;
  }

  class RetailCallbackBridge final {
  public:
    RetailCallbackBridge(
        CreepTendrilConnection& owner,
        std::shared_ptr<ICreepTendrilCallbackTarget> target);

    RetailCreepPluginCallbacks* Callbacks() noexcept { return &callbacks_; }

  private:
    static const RetailCreepPluginCallbacks::AbiFunction* Vtable();
    static RetailCallbackBridge& From(void* callbacks);
    static void CompleteDestructorThunk(void* callbacks);
    static void DeletingDestructorThunk(void* callbacks);
    static std::uintptr_t Forward0Thunk(void* callbacks);
    static std::uintptr_t ResolveAddressThunk(void* callbacks,
                                              const std::uint8_t* bytes,
                                              std::uint32_t byte_count);
    static std::uintptr_t Forward2Thunk(void* callbacks);
    static std::uintptr_t Forward3Thunk(void* callbacks);
    static std::uintptr_t Forward4Thunk(void* callbacks,
                                        std::uintptr_t argument);
    static std::uintptr_t Forward5Thunk(void* callbacks);
    static void SendAddressThunk(void* callbacks, std::uintptr_t first,
                                 std::uintptr_t second, std::uintptr_t third,
                                 const std::uint8_t* bytes,
                                 std::uint32_t byte_count);
    static std::uintptr_t Forward7Thunk(void* callbacks);
    static std::uintptr_t Forward8Thunk(void* callbacks);
    static std::uintptr_t Forward9Thunk(void* callbacks,
                                        std::uintptr_t first,
                                        std::uintptr_t second);
    static std::uintptr_t Forward9WithKind5Thunk(void* callbacks,
                                                 std::uintptr_t argument);
    static std::uintptr_t Forward11Thunk(void* callbacks,
                                         std::uintptr_t first,
                                         std::uintptr_t second);
    static bool Forward12Thunk(void* callbacks, std::uintptr_t argument);
    static void IncomingEventThunk(void* callbacks, void* reflected_event,
                                   std::uintptr_t context);
    [[nodiscard]] bool DecodeAddress(
        const std::uint8_t* bytes, std::uint32_t byte_count,
        std::array<std::uint8_t, 6>& address) const;

    [[maybe_unused]] std::array<std::byte, 0x1c> retail_prefix_{};
    RetailCreepPluginCallbacks callbacks_{};
    CreepTendrilConnection* owner_{nullptr};
    std::shared_ptr<ICreepTendrilCallbackTarget> target_;
  };

  CreepTendrilConnection(CreepNodeData node,
                         std::vector<std::uint8_t> serialized_node,
                         std::shared_ptr<ICreepTendrilEventSink> event_sink,
                         std::shared_ptr<ICreepTendrilCallbackTarget>
                             callback_target);

  CreepNodeData node_{};
  std::vector<std::uint8_t> serialized_node_;
  std::shared_ptr<ICreepTendrilEventSink> event_sink_;
  bool added_to_manager_{false};
  RetailCallbackBridge plugin_callbacks_;

  std::unique_ptr<BattleNetPluginConnection> plugin_connection_;
};

}
