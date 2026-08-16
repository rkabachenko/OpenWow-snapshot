#pragma once

#include "openwow/network/protocol/wotlk/opcodes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace openwow::net::wotlk {

struct WorldPacket;

class MainThreadPacketDispatcher final {
 public:
  using Handler = std::function<bool(const WorldPacket&)>;

  class Registration final {
   public:
    Registration() = default;
    ~Registration();

    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;
    Registration(Registration&& other) noexcept;
    Registration& operator=(Registration&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept {
      return dispatcher_ != nullptr;
    }
    void Reset() noexcept;

   private:
    friend class MainThreadPacketDispatcher;

    Registration(MainThreadPacketDispatcher& dispatcher,
                  std::uint16_t opcode,
                  std::uint64_t generation,
                  std::string feature) noexcept
        : dispatcher_(&dispatcher),
          opcode_(opcode),
          generation_(generation),
          feature_(std::move(feature)) {}

    MainThreadPacketDispatcher* dispatcher_{nullptr};
    std::uint16_t opcode_{0};
    std::uint64_t generation_{0};
    std::string feature_;
  };

  MainThreadPacketDispatcher();
  ~MainThreadPacketDispatcher() = default;

  MainThreadPacketDispatcher(const MainThreadPacketDispatcher&) = delete;
  MainThreadPacketDispatcher& operator=(const MainThreadPacketDispatcher&) =
      delete;

  [[nodiscard]] Registration Register(Opcode opcode,
                                      const char* feature,
                                      Handler handler);
  [[nodiscard]] bool Dispatch(const WorldPacket& packet) const;
  [[nodiscard]] bool HasHandler(Opcode opcode) const noexcept;
  [[nodiscard]] std::size_t RegisteredHandlerCount() const noexcept;

 private:
  struct Slot {
    Handler handler;
    std::string feature;
    std::uint64_t generation{0};
  };

  void Unregister(std::uint16_t opcode,
                  std::uint64_t generation,
                  std::string_view feature) noexcept;

  std::array<Slot, kNumOpcodes> slots_{};
  std::thread::id owner_thread_;
  std::uint64_t next_generation_{1};
};

}
