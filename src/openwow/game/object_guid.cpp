
#include "openwow/game/object_guid.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace openwow::game {

std::string ObjectGuid::TypeToString(HighGuid high) {
  switch (high) {
    case HighGuid::kPlayer:        return "Player";
    case HighGuid::kItem:          return "Item";
    case HighGuid::kGameObject:    return "GameObject";
    case HighGuid::kTransport:     return "Transport";
    case HighGuid::kUnit:          return "Creature";
    case HighGuid::kPet:           return "Pet";
    case HighGuid::kVehicle:       return "Vehicle";
    case HighGuid::kDynamicObject: return "DynObject";
    case HighGuid::kCorpse:        return "Corpse";
    case HighGuid::kMoTransport:   return "MoTransport";
    case HighGuid::kInstance:      return "Instance";
    case HighGuid::kGroup:         return "Group";
  }
  return "Unknown";
}

std::string ObjectGuid::ToString() const {
  if (IsEmpty()) return "Empty";

  const std::string type_name = TypeToString(GetHigh());

  char buf[128];
  if (HasEntry()) {
    std::snprintf(buf, sizeof(buf), "%s (Entry: %u, Counter: %u)",
                  type_name.c_str(), GetEntry(), GetCounter());
  } else {
    std::snprintf(buf, sizeof(buf), "%s (Counter: %u)",
                  type_name.c_str(), GetCounter());
  }
  return buf;
}

std::string ObjectGuid::ToDetailedString() const {
  if (IsEmpty()) return "Empty (Raw=0x0000000000000000)";

  std::string type_name = TypeToString(GetHigh());

  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "Type=%s Entry=%u Counter=%u HighGuid=0x%04X Raw=0x%016" PRIX64,
                type_name.c_str(), GetEntry(), GetCounter(),
                static_cast<unsigned>(GetHigh()), raw_);
  return buf;
}

std::string ObjectGuid::ToHexString() const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%016" PRIX64, raw_);
  return buf;
}

bool ObjectGuid::IsValid() const {
  if (IsEmpty()) return false;

  switch (GetHigh()) {
    case HighGuid::kPlayer:
    case HighGuid::kItem:
    case HighGuid::kGameObject:
    case HighGuid::kTransport:
    case HighGuid::kUnit:
    case HighGuid::kPet:
    case HighGuid::kVehicle:
    case HighGuid::kDynamicObject:
    case HighGuid::kCorpse:
    case HighGuid::kMoTransport:
    case HighGuid::kInstance:
    case HighGuid::kGroup:
      break;
    default:
      return false;
  }

  if (GetCounter() == 0 && GetHigh() != HighGuid::kPlayer) {

    return false;
  }

  return true;
}

ObjectGuid ObjectGuid::FromHexString(const std::string& hex) {
  if (hex.empty()) return ObjectGuid{};

  std::string clean = hex;
  if (clean.size() >= 2 && clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X')) {
    clean = clean.substr(2);
  }

  if (clean.empty() || clean.size() > 16) return ObjectGuid{};

  for (char c : clean) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return ObjectGuid{};
    }
  }

  std::uint64_t val = 0;
  for (char c : clean) {
    val <<= 4;
    if (c >= '0' && c <= '9') val |= static_cast<std::uint64_t>(c - '0');
    else if (c >= 'a' && c <= 'f') val |= static_cast<std::uint64_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') val |= static_cast<std::uint64_t>(c - 'A' + 10);
  }
  return ObjectGuid(val);
}

void ObjectGuid::WriteRawBytes(std::uint8_t* out) const {

  std::uint64_t val = raw_;
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<std::uint8_t>(val & 0xFF);
    val >>= 8;
  }
}

ObjectGuid ObjectGuid::ReadRawBytes(const std::uint8_t* in) {
  std::uint64_t val = 0;
  for (int i = 7; i >= 0; --i) {
    val = (val << 8) | in[i];
  }
  return ObjectGuid(val);
}

}
