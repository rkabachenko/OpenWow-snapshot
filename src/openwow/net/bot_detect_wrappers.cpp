#include "openwow/net/bot_detect.h"

#include "openwow/game/warden_probes.h"
#include "openwow/net/client_services.h"

#include <openssl/sha.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace openwow::net {

namespace {

constexpr std::size_t kRealmConnectionSessionKeyOffset = 1288u;

}

void *ClientServices__GetConnectionObject() {
  return ClientServices::GetConnectionObject();
}

int GetProbeValues(int *v1, int *v2, int *v3) {
  return openwow::game::GetProbeValues(v1, v2, v3);
}

void SHA1_Init(std::uint8_t *ctx) {
  ::SHA1_Init(reinterpret_cast<SHA_CTX *>(ctx));
}

void SHA1_Update(std::uint8_t *ctx, const void *data, std::uint32_t len) {
  ::SHA1_Update(reinterpret_cast<SHA_CTX *>(ctx), data, len);
}

void SHA1_Final(std::uint8_t *ctx, std::uint8_t *digest) {
  ::SHA1_Final(digest, reinterpret_cast<SHA_CTX *>(ctx));
}

char *GetSessionKey(void *connection) {
  if (connection == nullptr) {
    return nullptr;
  }

  auto *const bytes = static_cast<std::uint8_t *>(connection);
  return reinterpret_cast<char *>(bytes + kRealmConnectionSessionKeyOffset);
}

}

#pragma GCC diagnostic pop
