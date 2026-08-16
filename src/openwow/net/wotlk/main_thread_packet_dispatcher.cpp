#include "openwow/net/wotlk/main_thread_packet_dispatcher.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <algorithm>
#include <utility>

namespace openwow::net::wotlk {

MainThreadPacketDispatcher::MainThreadPacketDispatcher()
    : owner_thread_(std::this_thread::get_id()) {}

MainThreadPacketDispatcher::Registration::~Registration() {
  Reset();
}

MainThreadPacketDispatcher::Registration::Registration(
    Registration&& other) noexcept
    : dispatcher_(std::exchange(other.dispatcher_, nullptr)),
      opcode_(other.opcode_),
      generation_(other.generation_),
      feature_(std::move(other.feature_)) {}

MainThreadPacketDispatcher::Registration&
MainThreadPacketDispatcher::Registration::operator=(
    Registration&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  Reset();
  dispatcher_ = std::exchange(other.dispatcher_, nullptr);
  opcode_ = other.opcode_;
  generation_ = other.generation_;
  feature_ = std::move(other.feature_);
  return *this;
}

void MainThreadPacketDispatcher::Registration::Reset() noexcept {
  if (dispatcher_ == nullptr) {
    return;
  }
  dispatcher_->Unregister(opcode_, generation_, feature_);
  dispatcher_ = nullptr;
}

MainThreadPacketDispatcher::Registration
MainThreadPacketDispatcher::Register(const Opcode opcode,
                                     const char* feature,
                                     Handler handler) {
  const auto index = OpcodeValue(opcode);
  if (index >= slots_.size() || !handler) {
    return {};
  }

  auto& slot = slots_[index];
  const auto generation = next_generation_++;
  std::string owned_feature = feature != nullptr ? feature : "";
  slot = {
      .handler = std::move(handler),
      .feature = owned_feature,
      .generation = generation,
  };
  return Registration(*this, index, generation, std::move(owned_feature));
}

bool MainThreadPacketDispatcher::Dispatch(const WorldPacket& packet) const {
  if (std::this_thread::get_id() != owner_thread_) {
    diagnostics::Log(diagnostics::LogLevel::kError,
                     "World packet dispatch attempted off the owning thread");
    return false;
  }

  const auto index = OpcodeValue(packet.GetOpcode());
  if (index >= slots_.size()) {
    return false;
  }
  const auto& slot = slots_[index];
  return slot.handler ? slot.handler(packet) : false;
}

bool MainThreadPacketDispatcher::HasHandler(const Opcode opcode) const noexcept {
  const auto index = OpcodeValue(opcode);
  return index < slots_.size() && static_cast<bool>(slots_[index].handler);
}

std::size_t MainThreadPacketDispatcher::RegisteredHandlerCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      slots_.begin(), slots_.end(),
      [](const Slot& slot) { return static_cast<bool>(slot.handler); }));
}

void MainThreadPacketDispatcher::Unregister(
    const std::uint16_t opcode,
    const std::uint64_t generation,
    const std::string_view feature) noexcept {
  if (opcode >= slots_.size()) {
    return;
  }
  auto& slot = slots_[opcode];
  if (slot.generation == generation && slot.feature == feature) {
    slot = {};
  }
}

}
