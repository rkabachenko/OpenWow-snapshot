#pragma once

#include "openwow/game/actions/macros/model/macro_document.h"

#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {
class AccountData;
class MacroCatalog;
}

namespace openwow::game::actions::macros::persistence {

class MacroAccountDataAdapter {
 public:
  [[nodiscard]] static std::vector<MacroDocument> Decode(
      MacroScope scope, std::string_view text);
  [[nodiscard]] static std::string Encode(
      const std::vector<MacroDocument>& macros);

  static void Load(MacroCatalog& catalog, MacroScope scope,
                   std::string_view text);
  static void LoadAll(MacroCatalog& catalog, const AccountData& account_data);
  static void SaveIfDirty(MacroCatalog& catalog, AccountData& account_data);
};

}
