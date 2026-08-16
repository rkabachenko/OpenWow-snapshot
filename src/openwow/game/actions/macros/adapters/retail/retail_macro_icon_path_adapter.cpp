#include "openwow/game/actions/macros/adapters/retail/retail_macro_icon_path_adapter.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/world_session.h"

#include <cctype>
#include <string_view>

namespace openwow::game::actions::macros::adapters::retail {
namespace {

constexpr std::string_view kDefaultIcon = "INV_Misc_QuestionMark";
constexpr std::string_view kIconDirectory = "Interface\\Icons\\";

std::string InterfaceIconPath(const std::string_view icon) {
  std::string path(kIconDirectory);
  path.append(icon);
  return path;
}

bool EqualsNoCase(const std::string_view lhs,
                  const std::string_view rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

std::string SpellIconPath(const openwow::data::dbc::DbcLoader& dbc,
                          const std::uint32_t icon_id) {
  const auto* icon = icon_id != 0
                         ? dbc.spell_icon().LookupEntry(icon_id)
                         : nullptr;
  return icon != nullptr &&
                 !std::string_view(icon->icon_path).empty()
             ? std::string(icon->icon_path)
             : std::string{};
}

const CGUnit_C* SpellIconUnit(const MacroDocument& macro,
                              const WorldSession& session) {
  if (!macro.resolved_spell_from_pet_book) {
    return session.objects().GetLocalPlayerTyped();
  }
  const auto& pets = session.pet().pet_guids();
  return pets.empty() || pets.front() == 0
             ? nullptr
             : session.objects().GetUnit(ObjectGuid(pets.front()));
}

bool HasActiveAura(const openwow::data::dbc::SpellEntry& spell,
                   const CGUnit_C& unit) {
  for (const auto& aura : unit.Auras().All()) {
    if (aura.spell_id == spell.id && (aura.flags & 0x10u) != 0u) {
      return true;
    }
  }
  return false;
}

std::string Resolve(const MacroDocument& macro,
                    const WorldSession& session,
                    const ItemDefinitions& item_definitions) {
  if (!EqualsNoCase(macro.icon_name, kDefaultIcon)) {
    return InterfaceIconPath(macro.icon_name);
  }
  const auto* dbc = session.GetDbcLoader();
  if (dbc != nullptr && macro.resolved_item_id != 0) {
    if (const auto* item = item_definitions.GetItem(macro.resolved_item_id);
        item != nullptr && item->display_id != 0) {
      return ResolveItemInventoryIconTexturePath(dbc, item->display_id);
    }
  }
  if (dbc != nullptr && macro.resolved_spell_id > 0) {
    const auto* spell = dbc->spell().LookupEntry(
        static_cast<std::uint32_t>(macro.resolved_spell_id));
    if (spell != nullptr) {
      auto icon_id = spell->spell_icon_id;
      if (spell->active_icon_id != 0) {
        if (const auto* unit = SpellIconUnit(macro, session);
            unit != nullptr && HasActiveAura(*spell, *unit)) {
          icon_id = spell->active_icon_id;
        }
      }
      if (auto path = SpellIconPath(*dbc, icon_id); !path.empty()) {
        return path;
      }
    }
  }
  return InterfaceIconPath(kDefaultIcon);
}

}

MacroIconPathResolver MakeRetailMacroIconPathResolver(
    const WorldSession& session, const ItemDefinitions& item_definitions) {
  return [&session, &item_definitions](const MacroDocument& macro) {
    return Resolve(macro, session, item_definitions);
  };
}

}
