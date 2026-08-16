#pragma once

#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string_view>

namespace openwow::game {

enum class MinigameType : std::uint8_t {
  TicTacToe = 1,
};

class MinigameSystem {
 public:
  static MinigameSystem& Get() {
    static MinigameSystem instance;
    return instance;
  }

  void HandleSetupPayload(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < sizeof(std::uint64_t) + sizeof(std::uint8_t)) {
      return;
    }

    std::uint64_t player_guid = 0;
    std::memcpy(&player_guid, data, sizeof(player_guid));
    const auto raw_type = data[sizeof(player_guid)];

    std::lock_guard lock(mutex_);
    if (raw_type == static_cast<std::uint8_t>(MinigameType::TicTacToe)) {
      ActiveMinigame minigame;
      minigame.player_guid = player_guid;
      minigame.type = MinigameType::TicTacToe;

      std::size_t cursor = sizeof(player_guid) + sizeof(raw_type);
      if (cursor < size) {
        minigame.setup_state = data[cursor++];
      }

      if (cursor + sizeof(minigame.context_guid_a) <= size) {
        std::memcpy(&minigame.context_guid_a, data + cursor, sizeof(minigame.context_guid_a));
        cursor += sizeof(minigame.context_guid_a);
      }

      if (cursor + sizeof(minigame.context_guid_b) <= size) {
        std::memcpy(&minigame.context_guid_b, data + cursor, sizeof(minigame.context_guid_b));
        cursor += sizeof(minigame.context_guid_b);
      }

      if (cursor + minigame.board_state.size() <= size) {
        std::memcpy(minigame.board_state.data(), data + cursor, minigame.board_state.size());
        cursor += minigame.board_state.size();
      }

      if (minigame.setup_state == 2 && cursor < size) {
        minigame.setup_state_extra = data[cursor];
      }

      active_ = minigame;
      return;
    }

    active_.reset();
  }

  void HandleStatePayload(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
      return;
    }

    std::lock_guard lock(mutex_);
    if (!active_.has_value() || active_->type != MinigameType::TicTacToe) {
      return;
    }

    const auto cell_count = (std::min)(std::size_t{9}, size);
    for (std::size_t index = 0; index < cell_count; ++index) {
      active_->board_state[index] = data[index];
    }

    if (size > cell_count) {
      active_->current_turn = data[cell_count];
    }
  }

  void Reset() {
    std::lock_guard lock(mutex_);
    active_.reset();
  }

  [[nodiscard]] std::optional<MinigameType> GetType() const {
    std::lock_guard lock(mutex_);
    if (!active_.has_value()) {
      return std::nullopt;
    }
    return active_->type;
  }

  [[nodiscard]] std::optional<std::uint64_t> GetPlayerGuid() const {
    std::lock_guard lock(mutex_);
    if (!active_.has_value()) {
      return std::nullopt;
    }
    return active_->player_guid;
  }

  [[nodiscard]] std::optional<std::string_view> GetLuaTypeName() const {
    std::lock_guard lock(mutex_);
    if (!active_.has_value()) {
      return std::nullopt;
    }

    switch (active_->type) {
    case MinigameType::TicTacToe:
      return std::string_view{"TicTacToe"};
    }

    return std::nullopt;
  }

  [[nodiscard]] std::optional<std::array<std::uint8_t, 9>> GetLuaState() const {
    std::lock_guard lock(mutex_);
    if (!active_.has_value()) {
      return std::nullopt;
    }

    std::array<std::uint8_t, 9> state{};
    for (std::size_t index = 0; index < state.size(); ++index) {
      state[index] = active_->board_state[index];
    }
    return state;
  }

  [[nodiscard]] bool TrySendMove(const int move_type, const int param) const {
    std::optional<net::wotlk::WorldPacket> packet;

    {
      std::lock_guard lock(mutex_);
      if (!active_.has_value() || active_->type != MinigameType::TicTacToe) {
        return false;
      }

      const auto param_value = static_cast<std::uint32_t>(param);
      if (param_value > 9 || active_->board_state[param_value] != 0xFF) {
        return false;
      }

      net::wotlk::WorldPacket outbound(net::wotlk::Opcode::CMSG_MINIGAME_MOVE);
      outbound.AppendU64(active_->player_guid);
      outbound.AppendU8(static_cast<std::uint8_t>(move_type));
      outbound.AppendU32(param_value);
      packet = std::move(outbound);
    }

    return net::ClientServices__SendPacket(*packet);
  }

 private:
  struct ActiveMinigame {
    std::uint64_t player_guid = 0;
    MinigameType type = MinigameType::TicTacToe;
    std::uint8_t setup_state = 0;
    std::uint64_t context_guid_a = 0;
    std::uint64_t context_guid_b = 0;
    std::uint8_t current_turn = 0;
    std::uint8_t setup_state_extra = 0;
    std::array<std::uint8_t, 64> board_state{};
  };

  MinigameSystem() = default;

  mutable std::mutex mutex_;
  std::optional<ActiveMinigame> active_;
};

}
