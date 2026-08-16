#ifndef openwow_client_crt_random_h
#define openwow_client_crt_random_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  OPENWOW_CLIENT_CRT_RAND_MULTIPLIER = 214013u,
  OPENWOW_CLIENT_CRT_RAND_INCREMENT = 2531011u,
  OPENWOW_CLIENT_CRT_RAND_MASK = 0x7FFFu
};

void openwow_lua_seed_client_crt_rand(uint32_t seed);
uint32_t openwow_lua_next_client_crt_rand(void);

#ifdef __cplusplus
}
#endif

#endif
