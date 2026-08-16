#include "openwow/net/wotlk/protocol/auth_crypt.h"
#include "openwow/net/wotlk/protocol/world_header_crypto.h"

namespace openwow::net::wotlk {

static constexpr std::size_t kRC4DropBytes = 1024;

void AuthCrypt::Init(const std::uint8_t session_key[40]) {
  const auto keys = DeriveWorldHeaderKeys(session_key, 40u);
  InitKeys(keys.send, keys.receive);
}

void AuthCrypt::Init(
    const std::uint8_t session_key[40],
    const std::span<const std::uint8_t, 32> redirect_challenge) {
  const auto keys =
      DeriveWorldHeaderKeys(session_key, 40u, redirect_challenge);
  InitKeys(keys.send, keys.receive);
}

void AuthCrypt::InitKeys(
    const std::array<std::uint8_t, 20>& send_key,
    const std::array<std::uint8_t, 20>& receive_key) {
  send_cipher_.Init(send_key.data(), send_key.size());
  recv_cipher_.Init(receive_key.data(), receive_key.size());

  send_cipher_.Drop(kRC4DropBytes);
  recv_cipher_.Drop(kRC4DropBytes);

  initialized_ = true;
}

void AuthCrypt::EncryptSend(std::uint8_t* data, std::size_t len) {
  if (!initialized_) return;
  send_cipher_.Process(data, len);
}

void AuthCrypt::DecryptRecv(std::uint8_t* data, std::size_t len) {
  if (!initialized_) return;
  recv_cipher_.Process(data, len);
}

}
