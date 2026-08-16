#pragma once

#include "openwow/data/async_file_read.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openwow::game::vehicle_runtime_layout {

inline constexpr std::size_t kVehicleEntryPointerOffset = 12u;
inline constexpr std::size_t kVehicleTransformMatrixOffset = 16u;
inline constexpr std::size_t kVehiclePassengerListOffset = 368u;
inline constexpr std::size_t kVehiclePassengerGuidHolderOffset = 40u;

template <typename T>
[[nodiscard]] T LoadField(const void* const base, const std::size_t offset) {
  T value{};
  if (base == nullptr) {
    return value;
  }

  std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(T));
  return value;
}

template <typename T>
void StoreField(void* const base, const std::size_t offset, const T& value) {
  if (base == nullptr) {
    return;
  }

  std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof(T));
}

inline void StoreVehicleEntryPointerField(
    void* const vehicle,
    const openwow::data::dbc::VehicleEntry* const vehicle_entry) {
  const std::uint32_t token =
      openwow::data::AsyncFileRead_EncodePointerToken(vehicle_entry);
  StoreField(vehicle, kVehicleEntryPointerOffset, token);
}

[[nodiscard]] inline const openwow::data::dbc::VehicleEntry*
DecodeTokenVehicleEntry(const std::uint32_t token) {
  if (token == 0u) {
    return nullptr;
  }

  const auto* const token_entry =
      static_cast<const openwow::data::dbc::VehicleEntry*>(
          openwow::data::AsyncFileRead_ResolvePointerToken(token));
  return reinterpret_cast<std::uintptr_t>(token_entry) != token ? token_entry
                                                                : nullptr;
}

[[nodiscard]] inline const openwow::data::dbc::VehicleEntry*
ResolveVehicleEntryPointerField(const void* const vehicle_data) {
  const std::uint32_t token =
      LoadField<std::uint32_t>(vehicle_data, kVehicleEntryPointerOffset);
  return DecodeTokenVehicleEntry(token);
}

}
