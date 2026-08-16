#include "openwow/game/creep_tendril.h"

#include "openwow/game/bnet_bit_stream.h"
#include "openwow/debug/diagnostics/error_handler.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <typeinfo>
#include <utility>

namespace openwow::game {
namespace {

constexpr std::array<std::int8_t, 15> kEventRoutes = {
    1, 15, 14, -1, 8, 9, -1, 6, 5, 11, 10, -1, -1, 3, 2};

class FixedBufferBitStream final : public BNetBitStream {
public:
  ~FixedBufferBitStream() override = default;
};

std::vector<std::uint8_t> SerializeNode(const CreepNodeData& node) {
  BNetBitSizeStream sizer;
  if (CalcBitSize_CreepNode(node.data(), &sizer) != BNetBitStream::kSuccess) {
    return {};
  }
  const std::uint32_t byte_count = sizer.BytesRequired();
  if (byte_count == 0) {
    return {};
  }

  std::vector<std::uint8_t> bytes(byte_count, 0);
  FixedBufferBitStream stream;
  if (!stream.SetBuffer(std::span<std::uint8_t>{bytes})) {
    return {};
  }
  if (SerializeCreepNode(node.data(), stream) != BNetBitStream::kSuccess) {
    return {};
  }
  return bytes;
}

}

CreepTendrilConnection::CreepTendrilConnection(
    CreepNodeData node, std::vector<std::uint8_t> serialized_node,
    std::shared_ptr<ICreepTendrilEventSink> event_sink,
    std::shared_ptr<ICreepTendrilCallbackTarget> callback_target)
    : node_(std::move(node)), serialized_node_(std::move(serialized_node)),
      event_sink_(std::move(event_sink)),
      plugin_callbacks_(*this, std::move(callback_target)) {}

std::shared_ptr<CreepTendrilConnection> CreepTendrilConnection::Create(
    const CreepNodeData& node,
    std::shared_ptr<ICreepTendrilEventSink> event_sink,
    std::shared_ptr<ICreepTendrilCallbackTarget> callback_target,
    BattleNetPluginRuntime& plugin_runtime) {
  if (!event_sink || !callback_target) {
    return nullptr;
  }
  std::vector<std::uint8_t> serialized = SerializeNode(node);
  if (serialized.empty()) {
    return nullptr;
  }

  auto connection = std::shared_ptr<CreepTendrilConnection>(
      new CreepTendrilConnection(node, std::move(serialized),
                                 std::move(event_sink),
                                 std::move(callback_target)));
  connection->plugin_connection_ = plugin_runtime.CreateConnection(
      connection->serialized_node_, connection->plugin_callbacks_.Callbacks());
  if (!connection->plugin_connection_) {
    return nullptr;
  }
  return connection;
}

CreepEventDispatchResult CreepTendrilConnection::DispatchIncomingEvent(
    std::uint8_t event_id, TumorMessageType message_type,
    const std::uint32_t* payload, std::uintptr_t context) {
  if (event_id >= kEventRoutes.size() || kEventRoutes[event_id] < 0) {
    return CreepEventDispatchResult::kUnknownEvent;
  }
  if (message_type != TumorMessageType::kByReference &&
      message_type != TumorMessageType::kByValue) {
    return CreepEventDispatchResult::kUnknownMessageType;
  }
  if (!payload || !event_sink_) {
    return CreepEventDispatchResult::kInvalidPayload;
  }

  const auto code = static_cast<TumorEventCode>(kEventRoutes[event_id]);
  if (message_type == TumorMessageType::kByReference) {
    event_sink_->OnCreepTendrilReference(code, *payload, context);
  } else {

    event_sink_->OnCreepTendrilValue(code, *payload, context);
  }
  return CreepEventDispatchResult::kDispatched;
}

void RetailCreepPluginCallbacks::CompleteDestructor() {
  using Function = void (*)(void*);
  reinterpret_cast<Function>(vptr_[0])(this);
}

void RetailCreepPluginCallbacks::DeletingDestructor() {
  using Function = void (*)(void*);
  reinterpret_cast<Function>(vptr_[1])(this);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward0() {
  using Function = std::uintptr_t (*)(void*);
  return reinterpret_cast<Function>(vptr_[2])(this);
}

std::uintptr_t RetailCreepPluginCallbacks::ResolveAddress(
    const std::uint8_t* bytes, std::uint32_t byte_count) {
  using Function = std::uintptr_t (*)(void*, const std::uint8_t*,
                                      std::uint32_t);
  return reinterpret_cast<Function>(vptr_[3])(this, bytes, byte_count);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward2() {
  using Function = std::uintptr_t (*)(void*);
  return reinterpret_cast<Function>(vptr_[4])(this);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward3() {
  using Function = std::uintptr_t (*)(void*);
  return reinterpret_cast<Function>(vptr_[5])(this);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward4(
    std::uintptr_t argument) {
  using Function = std::uintptr_t (*)(void*, std::uintptr_t);
  return reinterpret_cast<Function>(vptr_[6])(this, argument);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward5() {
  using Function = std::uintptr_t (*)(void*);
  return reinterpret_cast<Function>(vptr_[7])(this);
}

void RetailCreepPluginCallbacks::SendAddress(
    std::uintptr_t first, std::uintptr_t second, std::uintptr_t third,
    const std::uint8_t* bytes, std::uint32_t byte_count) {
  using Function = void (*)(void*, std::uintptr_t, std::uintptr_t,
                            std::uintptr_t, const std::uint8_t*,
                            std::uint32_t);
  reinterpret_cast<Function>(vptr_[8])(this, first, second, third, bytes,
                                       byte_count);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward7() {
  using Function = std::uintptr_t (*)(void*);
  return reinterpret_cast<Function>(vptr_[9])(this);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward8() {
  using Function = std::uintptr_t (*)(void*);
  return reinterpret_cast<Function>(vptr_[10])(this);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward9(
    std::uintptr_t first, std::uintptr_t second) {
  using Function = std::uintptr_t (*)(void*, std::uintptr_t,
                                      std::uintptr_t);
  return reinterpret_cast<Function>(vptr_[11])(this, first, second);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward9WithKind5(
    std::uintptr_t argument) {
  using Function = std::uintptr_t (*)(void*, std::uintptr_t);
  return reinterpret_cast<Function>(vptr_[12])(this, argument);
}

std::uintptr_t RetailCreepPluginCallbacks::Forward11(
    std::uintptr_t first, std::uintptr_t second) {
  using Function = std::uintptr_t (*)(void*, std::uintptr_t,
                                      std::uintptr_t);
  return reinterpret_cast<Function>(vptr_[13])(this, first, second);
}

bool RetailCreepPluginCallbacks::Forward12(std::uintptr_t argument) {
  using Function = bool (*)(void*, std::uintptr_t);
  return reinterpret_cast<Function>(vptr_[14])(this, argument);
}

void RetailCreepPluginCallbacks::IncomingEvent(
    void* reflected_event, std::uintptr_t context) {
  using Function = void (*)(void*, void*, std::uintptr_t);
  reinterpret_cast<Function>(vptr_[15])(this, reflected_event, context);
}

CreepTendrilConnection::RetailCallbackBridge::RetailCallbackBridge(
    CreepTendrilConnection& owner,
    std::shared_ptr<ICreepTendrilCallbackTarget> target)
    : owner_(&owner), target_(std::move(target)) {
  CreepTendrilConnection::InitializeRetailCallbacks(
      callbacks_, Vtable(), this);
}

const RetailCreepPluginCallbacks::AbiFunction*
CreepTendrilConnection::RetailCallbackBridge::Vtable() {

  struct RetailVtable final {
    std::intptr_t offset_to_top{-0x1c};
    const std::type_info* rtti{&typeid(RetailCreepPluginCallbacks)};
    std::array<RetailCreepPluginCallbacks::AbiFunction, 16> entries{
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &CompleteDestructorThunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &DeletingDestructorThunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward0Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &ResolveAddressThunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward2Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward3Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward4Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward5Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &SendAddressThunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward7Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward8Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward9Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward9WithKind5Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward11Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &Forward12Thunk),
        reinterpret_cast<RetailCreepPluginCallbacks::AbiFunction>(
            &IncomingEventThunk),
    };
  };
  static const RetailVtable table;
  return table.entries.data();
}

CreepTendrilConnection::RetailCallbackBridge&
CreepTendrilConnection::RetailCallbackBridge::From(void* callbacks) {
  auto* facade = static_cast<RetailCreepPluginCallbacks*>(callbacks);
  return *static_cast<RetailCallbackBridge*>(
      CreepTendrilConnection::RetailCallbacksBridge(*facade));
}

void CreepTendrilConnection::RetailCallbackBridge::CompleteDestructorThunk(
    [[maybe_unused]] void* callbacks) {

}

void CreepTendrilConnection::RetailCallbackBridge::DeletingDestructorThunk(
    [[maybe_unused]] void* callbacks) {

}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward0Thunk(void* callbacks) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot0, 0, 0);
}

bool CreepTendrilConnection::RetailCallbackBridge::DecodeAddress(
    const std::uint8_t* bytes, std::uint32_t byte_count,
    std::array<std::uint8_t, 6>& address) const {
  if (!bytes || byte_count < address.size()) {
    return false;
  }
  std::array<std::uint8_t, 6> input{};
  std::memcpy(input.data(), bytes, input.size());
  FixedBufferBitStream stream;
  if (!stream.SetBuffer(input)) {
    return false;
  }
  return ReadCreepAddress(address.data(), stream) == BNetBitStream::kSuccess;
}

std::uintptr_t CreepTendrilConnection::RetailCallbackBridge::ResolveAddressThunk(
    void* callbacks, const std::uint8_t* bytes, std::uint32_t byte_count) {
  RetailCallbackBridge& bridge = From(callbacks);
  std::array<std::uint8_t, 6> address{};
  if (!bridge.DecodeAddress(bytes, byte_count, address)) {
    openwow::debug::ErrorHandler::Get().Report(
        openwow::debug::ErrorSeverity::Assert, "Bad address from Creep",
        __FILE__, __LINE__);
    return 0;
  }
  return bridge.target_->ResolveCreepAddress(
      address, reinterpret_cast<std::uintptr_t>(bridge.owner_));
}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward2Thunk(void* callbacks) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot2, 0, 0);
}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward3Thunk(void* callbacks) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot3, 0, 0);
}

std::uintptr_t CreepTendrilConnection::RetailCallbackBridge::Forward4Thunk(
    void* callbacks, std::uintptr_t argument) {
  RetailCallbackBridge& bridge = From(callbacks);
  return bridge.target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot4, argument,
      reinterpret_cast<std::uintptr_t>(bridge.owner_));
}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward5Thunk(void* callbacks) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot5, 0, 0);
}

void CreepTendrilConnection::RetailCallbackBridge::SendAddressThunk(
    void* callbacks, std::uintptr_t first, std::uintptr_t second,
    std::uintptr_t third, const std::uint8_t* bytes,
    std::uint32_t byte_count) {
  RetailCallbackBridge& bridge = From(callbacks);
  std::array<std::uint8_t, 6> address{};
  if (!bridge.DecodeAddress(bytes, byte_count, address)) {
    openwow::debug::ErrorHandler::Get().Report(
        openwow::debug::ErrorSeverity::Assert, "Bad address from Creep",
        __FILE__, __LINE__);
    return;
  }
  bridge.target_->SendToCreepAddress(first, second, third, address);
}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward7Thunk(void* callbacks) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot7, 0, 0);
}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward8Thunk(void* callbacks) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot8, 0, 0);
}

std::uintptr_t CreepTendrilConnection::RetailCallbackBridge::Forward9Thunk(
    void* callbacks, std::uintptr_t first, std::uintptr_t second) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot9, first, second);
}

std::uintptr_t
CreepTendrilConnection::RetailCallbackBridge::Forward9WithKind5Thunk(
    void* callbacks, std::uintptr_t argument) {

  return Forward9Thunk(callbacks, argument, 5);
}

std::uintptr_t CreepTendrilConnection::RetailCallbackBridge::Forward11Thunk(
    void* callbacks, std::uintptr_t first, std::uintptr_t second) {
  return From(callbacks).target_->ForwardCreepAdapter(
      CreepAdapterMethod::kSlot11, first, second);
}

bool CreepTendrilConnection::RetailCallbackBridge::Forward12Thunk(
    void* callbacks, std::uintptr_t argument) {
  return From(callbacks).target_->ForwardCreepAdapter(
             CreepAdapterMethod::kSlot12, argument, 0) != 0;
}

void CreepTendrilConnection::RetailCallbackBridge::IncomingEventThunk(
    void* callbacks, void* reflected_event, std::uintptr_t context) {
  RetailCallbackBridge& bridge = From(callbacks);
  DecodedCreepPluginEvent decoded{};
  if (!reflected_event ||
      !bridge.target_->DecodeIncomingCreepEvent(reflected_event, decoded)) {
    openwow::debug::ErrorHandler::Get().Report(
        openwow::debug::ErrorSeverity::Assert, "Bad event from Creep",
        __FILE__, __LINE__);
    return;
  }
  (void)bridge.owner_->DispatchIncomingEvent(
      decoded.event_id, decoded.message_type, &decoded.payload, context);
}

}
