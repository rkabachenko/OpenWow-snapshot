#include "openwow/game/actions/macros/model/macro_store.h"

#include <algorithm>
#include <cctype>

namespace openwow::game::actions::macros {
namespace {

bool EqualsNoCase(const std::string_view lhs,
                  const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

bool NameLess(const MacroDocument* lhs, const MacroDocument* rhs) {
  const auto limit = std::min(lhs->name.size(), rhs->name.size());
  for (std::size_t index = 0; index < limit; ++index) {
    const auto lhs_character =
        std::tolower(static_cast<unsigned char>(lhs->name[index]));
    const auto rhs_character =
        std::tolower(static_cast<unsigned char>(rhs->name[index]));
    if (lhs_character != rhs_character) {
      return lhs_character < rhs_character;
    }
  }
  return lhs->name.size() < rhs->name.size();
}

}

const MacroDocument* MacroStore::Find(const MacroId id) const {
  const auto it = documents_.find(id);
  return it != documents_.end() ? &it->second : nullptr;
}

MacroDocument* MacroStore::FindMutable(const MacroId id) {
  const auto it = documents_.find(id);
  return it != documents_.end() ? &it->second : nullptr;
}

const MacroDocument* MacroStore::FindByName(
    const std::string_view name) const {
  if (name.empty()) {
    return nullptr;
  }
  for (const auto id : lookup_order_) {
    const auto* document = Find(id);
    if (document != nullptr && EqualsNoCase(document->name, name)) {
      return document;
    }
  }
  return nullptr;
}

std::optional<MacroSlotIndex> MacroStore::FindSlot(
    const MacroId id) const {
  for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
    if (slots_[slot] && *slots_[slot] == id) {
      return MacroSlotIndex::FromZeroBased(slot);
    }
  }
  return std::nullopt;
}

std::optional<MacroDocument> MacroStore::AtSlot(
    const MacroSlotIndex slot) const {
  if (!slots_[slot.value()]) {
    return std::nullopt;
  }
  const auto* document = Find(*slots_[slot.value()]);
  return document != nullptr ? std::optional<MacroDocument>(*document)
                             : std::nullopt;
}

bool MacroStore::Update(
    const MacroId id,
    const std::function<void(MacroDocument&)>& update) {
  auto* document = FindMutable(id);
  if (document == nullptr) {
    return false;
  }
  update(*document);
  dirty_ = true;
  return true;
}

bool MacroStore::UpdateAtSlot(
    const MacroSlotIndex slot,
    const std::function<void(MacroDocument&)>& update) {
  return slots_[slot.value()]
             ? Update(*slots_[slot.value()], update)
             : false;
}

std::vector<MacroDocument> MacroStore::Snapshot(
    const MacroScope scope) const {
  std::vector<MacroDocument> result;
  for (const auto& [id, document] : documents_) {
    (void)id;
    if (document.scope == scope) {
      result.push_back(document);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const MacroDocument& lhs, const MacroDocument& rhs) {
              return lhs.id < rhs.id;
            });
  return result;
}

void MacroStore::PromoteLookup(const MacroId id) {
  RemoveLookup(id);
  lookup_order_.insert(lookup_order_.begin(), id);
}

void MacroStore::RemoveLookup(const MacroId id) {
  lookup_order_.erase(
      std::remove(lookup_order_.begin(), lookup_order_.end(), id),
      lookup_order_.end());
}

void MacroStore::RebuildSlots() {
  slots_.fill(std::nullopt);
  std::vector<const MacroDocument*> account;
  std::vector<const MacroDocument*> character;
  account.reserve(documents_.size());
  character.reserve(documents_.size());
  for (const auto& [id, document] : documents_) {
    (void)id;
    (document.scope == MacroScope::kCharacter ? character : account)
        .push_back(&document);
  }
  std::sort(account.begin(), account.end(), NameLess);
  std::sort(character.begin(), character.end(), NameLess);

  account_count_ = static_cast<std::uint32_t>(
      std::min(account.size(), kAccountSlotCount));
  character_count_ = static_cast<std::uint32_t>(
      std::min(character.size(), kCharacterSlotCount));
  for (std::size_t index = 0; index < account_count_; ++index) {
    slots_[index] = account[index]->id;
  }
  for (std::size_t index = 0; index < character_count_; ++index) {
    slots_[kCharacterSlotOffset + index] = character[index]->id;
  }
}

}
