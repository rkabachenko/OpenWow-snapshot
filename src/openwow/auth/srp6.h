#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openwow::auth {

std::array<uint8_t, 20> SHA1Hash(const uint8_t* data, size_t size);
std::array<uint8_t, 20> SHA1Hash(const std::vector<uint8_t>& data);

class BigNumber {
 public:
  BigNumber();
  explicit BigNumber(uint32_t val);
  BigNumber(const uint8_t* data, size_t size);
  BigNumber(const BigNumber& other);
  BigNumber(BigNumber&& other) noexcept;
  BigNumber& operator=(const BigNumber& other);
  BigNumber& operator=(BigNumber&& other) noexcept;
  ~BigNumber();

  void SetRandom(int numBits);

  void SetBinary(const uint8_t* data, size_t size);

  [[nodiscard]] std::vector<uint8_t> AsByteArray(size_t minSize = 0) const;

  [[nodiscard]] uint32_t AsUInt32() const;
  [[nodiscard]] bool IsZero() const;
  [[nodiscard]] int GetNumBytes() const;
  [[nodiscard]] int GetBitLength() const;

  BigNumber operator+(const BigNumber& other) const;
  BigNumber operator-(const BigNumber& other) const;
  BigNumber operator*(const BigNumber& other) const;
  BigNumber operator%(const BigNumber& other) const;

  [[nodiscard]] BigNumber ModExp(const BigNumber& exp, const BigNumber& mod) const;

  [[nodiscard]] BigNumber ModSub(const BigNumber& other, const BigNumber& mod) const;

  [[nodiscard]] BigNumber ModMul(const BigNumber& other, const BigNumber& mod) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

inline constexpr std::array<uint8_t, 32> kSrpN = {
    0xB7, 0x9B, 0x3E, 0x2A, 0x87, 0x82, 0x3C, 0xAB,
    0x8F, 0x5E, 0xBF, 0xBF, 0x8E, 0xB1, 0x01, 0x08,
    0x53, 0x50, 0x06, 0x29, 0x8B, 0x5B, 0xAD, 0xBD,
    0x5B, 0x53, 0xE1, 0x89, 0x5E, 0x64, 0x4B, 0x89,
};

inline constexpr uint8_t kSrpG = 7;

inline constexpr uint8_t kSrpK = 3;

class SRP6Client {
 public:

  void Initialize(const std::string& username, const std::string& password);

  bool ProcessChallenge(
      const uint8_t* B_bytes, size_t B_size,
      uint8_t g,
      const uint8_t* N_bytes, size_t N_size,
      const uint8_t* salt, size_t salt_size);

  [[nodiscard]] std::array<uint8_t, 20> GetClientProof() const;

  [[nodiscard]] bool VerifyServerProof(const uint8_t* M2, size_t size) const;

  [[nodiscard]] std::array<uint8_t, 40> GetSessionKey() const;

  [[nodiscard]] std::vector<uint8_t> GetPublicKey() const;

  [[nodiscard]] bool IsInitialized() const { return initialized_; }
  [[nodiscard]] bool IsChallengeProcessed() const { return challenge_processed_; }

 private:
  std::string username_;
  std::string password_;

  BigNumber a_;
  BigNumber A_;

  std::array<uint8_t, 40> K_{};
  std::array<uint8_t, 20> M1_{};
  std::array<uint8_t, 20> M2_{};

  bool initialized_ = false;
  bool challenge_processed_ = false;
};

}
