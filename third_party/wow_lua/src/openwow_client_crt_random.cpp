#include "openwow_client_crt_random.h"

#include <atomic>

namespace {

std::atomic<uint32_t> g_client_crt_rand_state{1u};

}

extern "C" void openwow_lua_seed_client_crt_rand(const uint32_t seed) {
  g_client_crt_rand_state.store(seed, std::memory_order_relaxed);
}

extern "C" uint32_t openwow_lua_next_client_crt_rand(void) {
  uint32_t previous =
      g_client_crt_rand_state.load(std::memory_order_relaxed);
  uint32_t next;
  do {
    next = OPENWOW_CLIENT_CRT_RAND_MULTIPLIER * previous +
           OPENWOW_CLIENT_CRT_RAND_INCREMENT;
  } while (!g_client_crt_rand_state.compare_exchange_weak(
      previous, next, std::memory_order_relaxed, std::memory_order_relaxed));
  return (next >> 16u) & OPENWOW_CLIENT_CRT_RAND_MASK;
}
