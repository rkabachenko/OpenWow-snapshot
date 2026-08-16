#pragma once

#include "openwow/net/protocol/rc4_cipher.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::net::wotlk {

class AuthCrypt {
 public:

  void Init(const std::uint8_t session_key[40]);

  void Init(const std::uint8_t session_key[40],
            std::span<const std::uint8_t, 32> redirect_challenge);

  void EncryptSend(std::uint8_t* data, std::size_t len);

  void DecryptRecv(std::uint8_t* data, std::size_t len);

 [[nodiscard]] bool IsInitialized() const { return initialized_; }

 private:
  void InitKeys(const std::array<std::uint8_t, 20>& send_key,
                const std::array<std::uint8_t, 20>& receive_key);

  openwow::net::RC4State send_cipher_{};
  openwow::net::RC4State recv_cipher_{};
  bool initialized_{false};
};

}
