#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "openwow/network/serialization/packed_guid_codec.h"

namespace openwow::game {

enum class HighGuid : std::uint16_t {
  kPlayer        = 0x0000,
  kItem          = 0x4000,
  kGameObject    = 0xF110,
  kTransport     = 0xF120,
  kUnit          = 0xF130,
  kPet           = 0xF140,
  kVehicle       = 0xF150,
  kDynamicObject = 0xF100,
  kCorpse        = 0xF101,
  kMoTransport   = 0x1FC0,
  kInstance      = 0x1F40,
  kGroup         = 0x1F50,
};

class ObjectGuid {
 public:
  constexpr ObjectGuid() = default;
  constexpr explicit ObjectGuid(std::uint64_t raw) : raw_(raw) {}

  static constexpr ObjectGuid Create(HighGuid high, std::uint32_t entry,
                                     std::uint32_t counter) {
    return ObjectGuid(
        (static_cast<std::uint64_t>(high) << 48) |
        (static_cast<std::uint64_t>(entry & 0x00FFFFFF) << 24) |
        static_cast<std::uint64_t>(counter & 0x00FFFFFF));
  }

  static constexpr ObjectGuid CreateGlobal(HighGuid high,
                                           std::uint32_t counter) {
    return ObjectGuid(
        (static_cast<std::uint64_t>(high) << 48) |
        static_cast<std::uint64_t>(counter));
  }

  static constexpr ObjectGuid FromHalves(std::uint32_t low,
                                         std::uint32_t high) {
    return ObjectGuid(static_cast<std::uint64_t>(low) |
                      (static_cast<std::uint64_t>(high) << 32));
  }

  [[nodiscard]] constexpr std::uint32_t GetLowPart() const {
    return static_cast<std::uint32_t>(raw_ & 0xFFFFFFFF);
  }
  [[nodiscard]] constexpr std::uint32_t GetHighPart() const {
    return static_cast<std::uint32_t>(raw_ >> 32);
  }

  [[nodiscard]] constexpr std::uint64_t GetRawValue() const { return raw_; }
  [[nodiscard]] constexpr bool IsEmpty() const { return raw_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const { return raw_ != 0; }

  [[nodiscard]] constexpr bool IsGuildCacheKey() const {
    return (GetHighPart() & 0xFFF00000u) == 0x1FF00000u;
  }

  [[nodiscard]] constexpr HighGuid GetHigh() const {
    return static_cast<HighGuid>(
        static_cast<std::uint16_t>((raw_ >> 48) & 0xFFFF));
  }

  [[nodiscard]] constexpr bool HasEntry() const {
    switch (GetHigh()) {
      case HighGuid::kGameObject:
      case HighGuid::kTransport:
      case HighGuid::kUnit:
      case HighGuid::kPet:
      case HighGuid::kVehicle:
        return true;
      default:
        return false;
    }
  }

  [[nodiscard]] constexpr std::uint32_t GetEntry() const {
    return HasEntry()
               ? static_cast<std::uint32_t>((raw_ >> 24) & 0x00FFFFFF)
               : 0;
  }

  [[nodiscard]] constexpr std::uint32_t GetCounter() const {
    return HasEntry()
               ? static_cast<std::uint32_t>(raw_ & 0x00FFFFFF)
               : static_cast<std::uint32_t>(raw_ & 0xFFFFFFFF);
  }

  [[nodiscard]] constexpr bool IsPlayer() const {
    return GetHigh() == HighGuid::kPlayer && !IsEmpty();
  }
  [[nodiscard]] constexpr bool IsCreature() const {
    return GetHigh() == HighGuid::kUnit;
  }
  [[nodiscard]] constexpr bool IsPet() const {
    return GetHigh() == HighGuid::kPet;
  }
  [[nodiscard]] constexpr bool IsVehicle() const {
    return GetHigh() == HighGuid::kVehicle;
  }
  [[nodiscard]] constexpr bool IsCreatureOrPetOrVehicle() const {
    return IsCreature() || IsPet() || IsVehicle();
  }
  [[nodiscard]] constexpr bool IsAnyTypeCreature() const {
    return IsCreatureOrPetOrVehicle();
  }
  [[nodiscard]] constexpr bool IsItem() const {
    return GetHigh() == HighGuid::kItem;
  }
  [[nodiscard]] constexpr bool IsGameObject() const {
    return GetHigh() == HighGuid::kGameObject;
  }
  [[nodiscard]] constexpr bool IsDynamicObject() const {
    return GetHigh() == HighGuid::kDynamicObject;
  }
  [[nodiscard]] constexpr bool IsCorpse() const {
    return GetHigh() == HighGuid::kCorpse;
  }
  [[nodiscard]] constexpr bool IsTransport() const {
    return GetHigh() == HighGuid::kTransport;
  }
  [[nodiscard]] constexpr bool IsMoTransport() const {
    return GetHigh() == HighGuid::kMoTransport;
  }
  [[nodiscard]] constexpr bool IsAnyTypeGameObject() const {
    return IsGameObject() || IsTransport() || IsMoTransport();
  }
  [[nodiscard]] constexpr bool IsInstance() const {
    return GetHigh() == HighGuid::kInstance;
  }
  [[nodiscard]] constexpr bool IsGroup() const {
    return GetHigh() == HighGuid::kGroup;
  }

  constexpr auto operator<=>(const ObjectGuid&) const = default;
  constexpr bool operator==(const ObjectGuid&) const = default;

  struct Hash {
    std::size_t operator()(const ObjectGuid& g) const noexcept {
      return std::hash<std::uint64_t>{}(g.raw_);
    }
  };

  [[nodiscard]] std::vector<std::uint8_t> Pack() const {
    const auto encoded = openwow::net::EncodePackedGuid(raw_);
    return {encoded.view().begin(), encoded.view().end()};
  }

  void PackInto(std::vector<std::uint8_t>& out) const {
    const auto encoded = openwow::net::EncodePackedGuid(raw_);
    out.insert(out.end(), encoded.view().begin(), encoded.view().end());
  }

  static std::size_t Unpack(const std::uint8_t* data, std::size_t len,
                            ObjectGuid& out) {
    const auto decoded = openwow::net::DecodePackedGuid(data, len);
    if (!decoded) return 0;
    out = ObjectGuid(decoded.value);
    return decoded.bytes_consumed;
  }

  [[nodiscard]] std::string ToString() const;

  [[nodiscard]] std::string ToDetailedString() const;

  [[nodiscard]] static std::string TypeToString(HighGuid high);

  [[nodiscard]] bool IsValid() const;

  [[nodiscard]] constexpr bool IsGlobalType() const { return !HasEntry(); }

  [[nodiscard]] static ObjectGuid FromHexString(const std::string& hex);

  [[nodiscard]] std::string ToHexString() const;

  void WriteRawBytes(std::uint8_t* out) const;

  static ObjectGuid ReadRawBytes(const std::uint8_t* in);

 private:
  std::uint64_t raw_{0};
};

}

template <>
struct std::hash<openwow::game::ObjectGuid> {
  std::size_t operator()(const openwow::game::ObjectGuid& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.GetRawValue());
  }
};
