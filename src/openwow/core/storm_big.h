
#pragma once

#include <cstdint>
#include <cstring>

namespace openwow::core {

static constexpr int kBigNumLimbs = 80;

struct BigNum {
    uint32_t limbs[kBigNumLimbs] = {};
    uint32_t count = 0;
};

struct BigNumContext {
    uint32_t header[5] = {};
    BigNum num;
    uint32_t trailer[4] = {};
};

static_assert(sizeof(BigNumContext) == 360);

inline void BigNum_Init(BigNum* bn) {
    std::memset(bn->limbs, 0, sizeof(bn->limbs));
    bn->count = 0;
}

inline uint32_t BigNum_ActiveLimbCount(const BigNum& bn) {
    return bn.count;
}

BigNumContext* BigNum_Create();

void BigNum_Destroy(BigNumContext* ctx);

void BigNum_Load(BigNum* bn, const uint8_t* data, uint32_t size);

void BigNum_Extract(const BigNum* bn, uint8_t* out, uint32_t max_size,
                    uint32_t* out_size);

int BigNum_Compare(const BigNum* lhs, const BigNum* rhs);

uint32_t BigNum_ExtractPadded(const BigNum* bn, uint8_t* out,
                              uint32_t desired_size);

void BigNum_ModExp(BigNum* result, const BigNum* base,
                   const BigNum* exp, const BigNum* modulus);

void SSignature_LoadKeyPair(BigNum* modulus, const uint8_t* mod_data,
                            uint32_t mod_size, BigNum* exponent,
                            const uint8_t* exp_data, uint32_t exp_size);

void SSignature_RSAVerify(const BigNum* modulus, const BigNum* exponent,
                          uint8_t* signature, uint32_t sig_size);

}
