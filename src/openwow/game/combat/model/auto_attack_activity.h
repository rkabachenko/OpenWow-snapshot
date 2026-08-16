#pragma once

#include <cstdint>

namespace openwow::game::combat {

enum class AutoAttackActivity : std::uint8_t {
  Inactive,
  Active,
};

enum class ClientControlPermission : std::uint8_t {
  Revoked,
  Granted,
};

struct AutoAttackActivityChange {
  AutoAttackActivity previous{AutoAttackActivity::Inactive};
  AutoAttackActivity current{AutoAttackActivity::Inactive};

  [[nodiscard]] bool changed() const {
    return previous != current;
  }
};

}
