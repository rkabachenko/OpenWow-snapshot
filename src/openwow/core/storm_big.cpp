
#include "storm_big.h"

#include <openssl/bn.h>

#include <algorithm>
#include <memory>
#include <new>
#include <vector>

namespace openwow::core {

namespace {

void InitializeBigNumContext(BigNumContext* ctx) {
    std::memset(ctx->header, 0, sizeof(ctx->header));
    BigNum_Init(&ctx->num);
    std::memset(ctx->trailer, 0, sizeof(ctx->trailer));
}

}

BigNumContext* BigNum_Create() {
    auto* ctx = new (std::nothrow) BigNumContext();
    if (!ctx) {
        return nullptr;
    }

    InitializeBigNumContext(ctx);
    return ctx;
}

void BigNum_Destroy(BigNumContext* ctx) {
    delete ctx;
}

void BigNum_Load(BigNum* bn, const uint8_t* data, uint32_t size) {
    if (!bn) return;
    std::memset(bn->limbs, 0, sizeof(bn->limbs));
    bn->count = 0;

    if (!data || size == 0) return;

    const uint32_t bounded_size =
        std::min<uint32_t>(size, kBigNumLimbs * sizeof(uint32_t));
    const uint32_t limb_count = (bounded_size + 3u) / 4u;

    for (uint32_t i = 0; i < bounded_size; ++i) {
        bn->limbs[i / 4] |= static_cast<uint32_t>(data[i]) << (8 * (i & 3));
    }
    bn->count = limb_count;
}

void BigNum_Extract(const BigNum* bn, uint8_t* out, uint32_t max_size,
                    uint32_t* out_size) {
    if (!bn || (!out && max_size != 0)) {
        if (out_size) *out_size = 0;
        return;
    }
    uint32_t byte_count = 0;
    const uint32_t active_limb_count =
        std::min<uint32_t>(BigNum_ActiveLimbCount(*bn), kBigNumLimbs);
    if (active_limb_count != 0) {
        byte_count = (active_limb_count - 1) * 4;
        uint32_t top_limb = bn->limbs[active_limb_count - 1];
        while (top_limb != 0) {
            ++byte_count;
            top_limb >>= 8;
        }
    }

    uint32_t copy_size = std::min(byte_count, max_size);
    for (uint32_t i = 0; i < copy_size; ++i) {
        out[i] = static_cast<uint8_t>(bn->limbs[i / 4] >> (8 * (i & 3)));
    }

    if (out_size) *out_size = copy_size;
}

int BigNum_Compare(const BigNum* lhs, const BigNum* rhs) {
    if (!lhs || !rhs) {
        return lhs == rhs ? 0 : (lhs ? 1 : -1);
    }

    int result = 0;
    for (std::uint32_t lhs_index = 0, rhs_index = 0;
         lhs_index < lhs->count || rhs_index < rhs->count;
         ++lhs_index, ++rhs_index) {
        const std::uint32_t lhs_limb =
            lhs_index < lhs->count ? lhs->limbs[lhs_index] : 0;
        const std::uint32_t rhs_limb =
            rhs_index < rhs->count ? rhs->limbs[rhs_index] : 0;

        if (lhs_limb != rhs_limb) {
            result = lhs_limb > rhs_limb ? 1 : -1;
        }
    }
    return result;
}

uint32_t BigNum_ExtractPadded(const BigNum* bn, uint8_t* out,
                              uint32_t desired_size) {
    if (!bn || (!out && desired_size != 0)) {
        return 0;
    }
    uint32_t actual_size = 0;
    BigNum_Extract(bn, out, desired_size, &actual_size);
    if (actual_size < desired_size) {
        std::memset(out + actual_size, 0, desired_size - actual_size);
    }
    return actual_size;
}

namespace {

struct BigNumDeleter {
    void operator()(BIGNUM* value) const {
        BN_free(value);
    }
};

struct BigNumCtxDeleter {
    void operator()(BN_CTX* value) const {
        BN_CTX_free(value);
    }
};

using UniqueBigNum = std::unique_ptr<BIGNUM, BigNumDeleter>;
using UniqueBigNumCtx = std::unique_ptr<BN_CTX, BigNumCtxDeleter>;

UniqueBigNum BigNumToOpenSsl(const BigNum& value) {
    const auto byte_count = static_cast<size_t>(
                                std::min<uint32_t>(
                                    BigNum_ActiveLimbCount(value),
                                    kBigNumLimbs)) *
                            sizeof(uint32_t);
    if (byte_count == 0) {
        return UniqueBigNum(BN_new());
    }

    std::vector<uint8_t> big_endian(byte_count);
    for (size_t index = 0; index < byte_count; ++index) {
        const auto limb_index = index / sizeof(uint32_t);
        const auto limb_shift = 8U * static_cast<unsigned int>(index & 3U);
        big_endian[byte_count - 1 - index] = static_cast<uint8_t>(
            value.limbs[limb_index] >> limb_shift);
    }
    return UniqueBigNum(
        BN_bin2bn(big_endian.data(), static_cast<int>(big_endian.size()), nullptr));
}

void OpenSslToBigNum(const BIGNUM* value, BigNum* out) {
    const auto byte_count = static_cast<size_t>(BN_num_bytes(value));
    if (byte_count == 0) {
        BigNum_Init(out);
        return;
    }

    std::vector<uint8_t> big_endian(byte_count);
    BN_bn2bin(value, big_endian.data());
    std::reverse(big_endian.begin(), big_endian.end());
    BigNum_Load(out, big_endian.data(), static_cast<uint32_t>(big_endian.size()));
}

}

void BigNum_ModExp(BigNum* result, const BigNum* base,
                   const BigNum* exp, const BigNum* modulus) {
    if (!result) {
        return;
    }
    BigNum_Init(result);
    if (!base || !exp || !modulus) {
        return;
    }

    auto bn_base = BigNumToOpenSsl(*base);
    auto bn_exp = BigNumToOpenSsl(*exp);
    auto bn_modulus = BigNumToOpenSsl(*modulus);
    UniqueBigNum bn_result(BN_new());
    UniqueBigNumCtx bn_ctx(BN_CTX_new());
    if (!bn_base || !bn_exp || !bn_modulus || !bn_result || !bn_ctx) {
        return;
    }

    if (BN_mod_exp(bn_result.get(), bn_base.get(), bn_exp.get(), bn_modulus.get(),
                   bn_ctx.get()) != 1) {
        return;
    }

    OpenSslToBigNum(bn_result.get(), result);
}

void SSignature_LoadKeyPair(BigNum* modulus, const uint8_t* mod_data,
                            uint32_t mod_size, BigNum* exponent,
                            const uint8_t* exp_data, uint32_t exp_size) {
    BigNum_Load(modulus, mod_data, mod_size);
    BigNum_Load(exponent, exp_data, exp_size);
}

void SSignature_RSAVerify(const BigNum* modulus, const BigNum* exponent,
                          uint8_t* signature, uint32_t sig_size) {
    if (!modulus || !exponent || (!signature && sig_size != 0) ||
        sig_size > kBigNumLimbs * sizeof(uint32_t)) {
        return;
    }
    BigNum encoded{};
    BigNum decoded{};
    BigNum_Load(&encoded, signature, sig_size);
    BigNum_ModExp(&decoded, &encoded, exponent, modulus);
    BigNum_ExtractPadded(&decoded, signature, sig_size);
}

}
