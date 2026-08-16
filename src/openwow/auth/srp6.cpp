
#include "openwow/auth/srp6.h"

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace openwow::auth {

std::array<uint8_t, 20> SHA1Hash(const uint8_t* data, size_t size) {
  std::array<uint8_t, 20> out{};
  SHA1(data, size, out.data());
  return out;
}

std::array<uint8_t, 20> SHA1Hash(const std::vector<uint8_t>& data) {
  return SHA1Hash(data.data(), data.size());
}

struct BigNumber::Impl {
  BIGNUM* bn = nullptr;

  Impl() : bn(BN_new()) {}
  explicit Impl(BIGNUM* owned) : bn(owned) {}
  ~Impl() {
    if (bn) BN_free(bn);
  }

  Impl(const Impl& other) : bn(BN_dup(other.bn)) {}
  Impl& operator=(const Impl& other) {
    if (this != &other) BN_copy(bn, other.bn);
    return *this;
  }
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
};

BigNumber::BigNumber() : impl_(std::make_unique<Impl>()) {}

BigNumber::BigNumber(uint32_t val) : impl_(std::make_unique<Impl>()) {
  BN_set_word(impl_->bn, val);
}

BigNumber::BigNumber(const uint8_t* data, size_t size) : impl_(std::make_unique<Impl>()) {
  SetBinary(data, size);
}

BigNumber::BigNumber(const BigNumber& other) : impl_(std::make_unique<Impl>(*other.impl_)) {}

BigNumber::BigNumber(BigNumber&& other) noexcept = default;

BigNumber& BigNumber::operator=(const BigNumber& other) {
  if (this != &other) impl_ = std::make_unique<Impl>(*other.impl_);
  return *this;
}

BigNumber& BigNumber::operator=(BigNumber&& other) noexcept = default;

BigNumber::~BigNumber() = default;

void BigNumber::SetRandom(int numBits) {
  const int numBytes = (numBits + 7) / 8;
  std::vector<uint8_t> buf(static_cast<size_t>(numBytes));
  RAND_bytes(buf.data(), numBytes);
  BN_bin2bn(buf.data(), numBytes, impl_->bn);
}

void BigNumber::SetBinary(const uint8_t* data, size_t size) {

  std::vector<uint8_t> be(data, data + size);
  std::reverse(be.begin(), be.end());
  BN_bin2bn(be.data(), static_cast<int>(be.size()), impl_->bn);
}

std::vector<uint8_t> BigNumber::AsByteArray(size_t minSize) const {
  const auto bytes = static_cast<size_t>(BN_num_bytes(impl_->bn));
  const size_t len = std::max(minSize, bytes);
  std::vector<uint8_t> be(len, 0);
  BN_bn2bin(impl_->bn, be.data() + (len - bytes));
  std::reverse(be.begin(), be.end());
  return be;
}

uint32_t BigNumber::AsUInt32() const {
  return static_cast<uint32_t>(BN_get_word(impl_->bn));
}

bool BigNumber::IsZero() const {
  return BN_is_zero(impl_->bn) != 0;
}

int BigNumber::GetNumBytes() const {
  return BN_num_bytes(impl_->bn);
}

int BigNumber::GetBitLength() const {
  return BN_num_bits(impl_->bn);
}

BigNumber BigNumber::operator+(const BigNumber& other) const {
  BigNumber r;
  BN_add(r.impl_->bn, impl_->bn, other.impl_->bn);
  return r;
}

BigNumber BigNumber::operator-(const BigNumber& other) const {
  BigNumber r;
  BN_sub(r.impl_->bn, impl_->bn, other.impl_->bn);
  return r;
}

BigNumber BigNumber::operator*(const BigNumber& other) const {
  BigNumber r;
  BN_CTX* ctx = BN_CTX_new();
  BN_mul(r.impl_->bn, impl_->bn, other.impl_->bn, ctx);
  BN_CTX_free(ctx);
  return r;
}

BigNumber BigNumber::operator%(const BigNumber& other) const {
  BigNumber r;
  BN_CTX* ctx = BN_CTX_new();
  BN_mod(r.impl_->bn, impl_->bn, other.impl_->bn, ctx);
  BN_CTX_free(ctx);
  return r;
}

BigNumber BigNumber::ModExp(const BigNumber& exp, const BigNumber& mod) const {
  BigNumber r;
  BN_CTX* ctx = BN_CTX_new();
  BN_mod_exp(r.impl_->bn, impl_->bn, exp.impl_->bn, mod.impl_->bn, ctx);
  BN_CTX_free(ctx);
  return r;
}

BigNumber BigNumber::ModSub(const BigNumber& other, const BigNumber& mod) const {
  BigNumber r;
  BN_CTX* ctx = BN_CTX_new();
  BN_mod_sub(r.impl_->bn, impl_->bn, other.impl_->bn, mod.impl_->bn, ctx);
  BN_CTX_free(ctx);
  return r;
}

BigNumber BigNumber::ModMul(const BigNumber& other, const BigNumber& mod) const {
  BigNumber r;
  BN_CTX* ctx = BN_CTX_new();
  BN_mod_mul(r.impl_->bn, impl_->bn, other.impl_->bn, mod.impl_->bn, ctx);
  BN_CTX_free(ctx);
  return r;
}

namespace {

std::string ToUpper(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return r;
}

BigNumber BnFromSha1(const std::array<uint8_t, 20>& hash) {
  return BigNumber(hash.data(), hash.size());
}

std::array<uint8_t, 40> DeriveSessionKey(const std::vector<uint8_t>& S_le) {
  const uint8_t* ptr = S_le.data();
  size_t len = S_le.size();
  while (len > 0 && *ptr == 0) {
    ++ptr;
    --len;
  }

  if ((len & 1) != 0) {
    ++ptr;
    --len;
  }

  const size_t half = len / 2;
  std::vector<uint8_t> even(half), odd(half);
  for (size_t i = 0; i < half; ++i) {
    even[i] = ptr[2 * i];
    odd[i]  = ptr[2 * i + 1];
  }

  const auto h1 = SHA1Hash(even);
  const auto h2 = SHA1Hash(odd);
  std::array<uint8_t, 40> K{};
  for (size_t i = 0; i < 20; ++i) {
    K[2 * i]     = h1[i];
    K[2 * i + 1] = h2[i];
  }
  return K;
}

}

void SRP6Client::Initialize(const std::string& username, const std::string& password) {
  username_ = ToUpper(username);
  password_ = ToUpper(password);
  K_ = {};
  M1_ = {};
  M2_ = {};
  initialized_ = true;
  challenge_processed_ = false;
}

bool SRP6Client::ProcessChallenge(
    const uint8_t* B_bytes, size_t B_size,
    uint8_t g_val,
    const uint8_t* N_bytes, size_t N_size,
    const uint8_t* salt, size_t salt_size) {
  if (!initialized_) return false;
  if (!B_bytes || B_size != 32) return false;
  if (!N_bytes || N_size == 0 || N_size > 32) return false;
  if (!salt || salt_size != 32) return false;

  const BigNumber N(N_bytes, N_size);
  if (N.GetBitLength() < 256) return false;

  const BigNumber g(static_cast<uint32_t>(g_val));
  const BigNumber B(B_bytes, B_size);
  if (g_val == 0 || (B % N).IsZero()) return false;

  a_.SetRandom(19 * 8);

  A_ = g.ModExp(a_, N);

  const std::string creds = username_ + ':' + password_;
  const auto h_creds = SHA1Hash(
      reinterpret_cast<const uint8_t*>(creds.data()), creds.size());

  for (auto& c : password_) c = '\0';
  password_.clear();

  SHA_CTX xctx;
  SHA1_Init(&xctx);
  SHA1_Update(&xctx, salt, salt_size);
  SHA1_Update(&xctx, h_creds.data(), h_creds.size());
  std::array<uint8_t, 20> x_hash{};
  SHA1_Final(x_hash.data(), &xctx);
  const BigNumber x = BnFromSha1(x_hash);

  const auto A_le = A_.AsByteArray(N_size);
  const auto B_le = B.AsByteArray(N_size);

  SHA_CTX uctx;
  SHA1_Init(&uctx);
  SHA1_Update(&uctx, A_le.data(), A_le.size());
  SHA1_Update(&uctx, B_le.data(), B_le.size());
  std::array<uint8_t, 20> u_hash{};
  SHA1_Final(u_hash.data(), &uctx);
  const BigNumber u = BnFromSha1(u_hash);

  const BigNumber k(kSrpK);
  const BigNumber gx   = g.ModExp(x, N);
  const BigNumber kgx  = k.ModMul(gx, N);
  const BigNumber base = B.ModSub(kgx, N);
  const BigNumber ux   = u * x;
  const BigNumber exp  = a_ + ux;
  const BigNumber S    = base.ModExp(exp, N);

  const auto S_le = S.AsByteArray(N_size);
  K_ = DeriveSessionKey(S_le);

  {
    const auto N_le = N.AsByteArray(N_size);
    const auto h_N  = SHA1Hash(N_le);
    const auto h_g  = SHA1Hash(&g_val, 1);

    std::array<uint8_t, 20> xor_ng{};
    for (int i = 0; i < 20; ++i) xor_ng[i] = h_N[i] ^ h_g[i];

    const auto h_I = SHA1Hash(
        reinterpret_cast<const uint8_t*>(username_.data()), username_.size());

    SHA_CTX m1ctx;
    SHA1_Init(&m1ctx);
    SHA1_Update(&m1ctx, xor_ng.data(), 20);
    SHA1_Update(&m1ctx, h_I.data(), 20);
    SHA1_Update(&m1ctx, salt, salt_size);
    SHA1_Update(&m1ctx, A_le.data(), A_le.size());
    SHA1_Update(&m1ctx, B_le.data(), B_le.size());
    SHA1_Update(&m1ctx, K_.data(), K_.size());
    SHA1_Final(M1_.data(), &m1ctx);
  }

  {
    SHA_CTX m2ctx;
    SHA1_Init(&m2ctx);
    SHA1_Update(&m2ctx, A_le.data(), A_le.size());
    SHA1_Update(&m2ctx, M1_.data(), M1_.size());
    SHA1_Update(&m2ctx, K_.data(), K_.size());
    SHA1_Final(M2_.data(), &m2ctx);
  }

  challenge_processed_ = true;
  return true;
}

std::array<uint8_t, 20> SRP6Client::GetClientProof() const {
  return M1_;
}

bool SRP6Client::VerifyServerProof(const uint8_t* M2, size_t size) const {
  if (!challenge_processed_ || size < 20) return false;
  return std::memcmp(M2, M2_.data(), 20) == 0;
}

std::array<uint8_t, 40> SRP6Client::GetSessionKey() const {
  return K_;
}

std::vector<uint8_t> SRP6Client::GetPublicKey() const {
  return A_.AsByteArray(32);
}

}

#pragma GCC diagnostic pop
