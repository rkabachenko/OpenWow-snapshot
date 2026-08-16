#pragma once

#include "openwow/game/actions/bindings/model/binding_types.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace openwow::game {
class BindingProfiles;
}

namespace openwow::game::actions::bindings::adapters::persistence {

class BindingProfileStorage {
 public:
  static void LoadFile(BindingProfiles& profiles, const std::string& path);
  static void SaveFile(const BindingProfiles& profiles,
                       const std::string& path);
  [[nodiscard]] static std::string Serialize(
      const BindingProfiles& profiles,
      BindingProfileScope scope,
      std::size_t* saved_count = nullptr);
  static void ApplyText(BindingProfiles& profiles,
                        std::string_view text,
                        BindingProfileScope target_scope);
};

}
