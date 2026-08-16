#pragma once

#include <cstdint>

namespace openwow::game {

void EncodeCharacterComponentBc1BlockSse(const void* pixels,
                                         std::int32_t stride,
                                         void* output);

[[nodiscard]] bool Bc1SseEncoderAvailable() noexcept;

}
