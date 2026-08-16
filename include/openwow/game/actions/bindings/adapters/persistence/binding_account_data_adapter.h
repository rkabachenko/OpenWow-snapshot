#pragma once

#include "openwow/game/actions/bindings/model/binding_types.h"

#include <cstdint>
#include <string_view>

namespace openwow::game {
class AccountData;
class BindingProfiles;
enum class AccountDataType : std::uint8_t;
}

namespace openwow::game::actions::bindings::adapters::persistence {

class BindingAccountDataAdapter {
 public:
  static void LoadCached(AccountData& account_data,
                         BindingProfiles& profiles);
  static void Save(AccountData& account_data,
                   const BindingProfiles& profiles);

  static void ApplyAccountDataSlot(BindingProfiles& profiles,
                                   AccountDataType type,
                                   std::string_view data);

  [[nodiscard]] static BindingProfileLoadGeneration BeginAsynchronousLoad(
      BindingProfiles& profiles);
  [[nodiscard]] static std::uint32_t MakeLoadCookie(
      BindingProfileLoadGeneration generation,
      BindingProfileScope scope);
  static void CompleteAsynchronousLoad(BindingProfiles& profiles,
                                       std::uint32_t cookie,
                                       std::string_view data);
};

}
