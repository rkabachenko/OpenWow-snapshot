#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

extern const std::uint8_t kScanDllSignatureModulus[256];

extern const std::uint8_t kScanDllSignatureExponent[4];

bool CheckExecutableSignature(const char* path);

bool CheckExecutableSignatureFromBuffer(const void* data, std::size_t size);

bool CheckExecutableSignatureFromBuffer(const void* data, std::size_t size,
                                        const void* modulus,
                                        const void* exponent);

}
