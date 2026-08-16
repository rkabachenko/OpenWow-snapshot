#pragma once

#include "openwow/foundation/hashing/retail_adler_seed.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::ui::glue::detail {

enum class CharacterCustomizationSlot : std::uint8_t {
  Skin = 0,
  Face = 1,
  HairStyle = 2,
  HairColor = 3,
  FacialHair = 4,
};

enum class CharacterCustomizationRandomizationOrder : std::uint8_t {
  SetupModel = 0,
  ScriptRandomize = 1,
};

struct CharacterCustomizationState {
  int race_id = 0;
  int sex_id = 0;
  int class_id = 0;
  int skin = 0;
  int skin_selection_anchor = 0;
  int face = 0;
  int hair_style = 0;
  int hair_color = 0;
  int facial_hair = 0;
};

inline bool CharacterCreateFilterMatchesFlags(const std::uint32_t flags, const int filter_mode) {
  switch (filter_mode) {
  case 0:
    if ((flags & 0x1u) == 0u) {
      return false;
    }
    return (flags & 0xCu) == 0u;
  case 1:
    return (flags & 0x1u) != 0u && (flags & 0x14u) != 0u && (flags & 0x8u) == 0u;
  case 2:
    if ((flags & 0x3u) == 0u) {
      return false;
    }
    return (flags & 0xCu) == 0u;
  case 3:
    if ((flags & 0x3u) == 0u || (flags & 0x14u) == 0u) {
      return false;
    }
    return (flags & 0x8u) == 0u;
  case 4:
    return true;
  case 5:
    return (flags & 0xCu) == 0u;
  case 6:
    if ((flags & 0x14u) == 0u) {
      return false;
    }
    return (flags & 0x8u) == 0u;
  default:
    return false;
  }
}

inline int CharacterCreateFilterModeForClass(const int mode, const int class_id) {
  switch (mode) {
  case 1:
    return (class_id == 6) ? 3 : 2;
  case 2:
    return 4;
  case 3:
    return (class_id == 6) ? 6 : 5;
  default:
    return class_id == 6 ? 1 : 0;
  }
}

template <typename EntryRange>
inline const openwow::data::dbc::CharSectionsEntry *FindCharacterCustomizationRow(
    const EntryRange &entries, const CharacterCustomizationState &state,
    const std::uint32_t base_section, const int type_value, const int variation_value,
    const int mode = 0);

template <typename EntryRange>
inline int GetCharacterCustomizationTypeSlotCount(const EntryRange &entries,
                                                  const CharacterCustomizationState &state,
                                                  const std::uint32_t base_section);

template <typename EntryRange>
inline int GetCharacterCustomizationVariationSlotCount(const EntryRange &entries,
                                                       const CharacterCustomizationState &state,
                                                       const std::uint32_t base_section,
                                                       const int type_value);

template <typename EntryRange>
inline const openwow::data::dbc::CharSectionsEntry *GetUnfilteredCharacterCustomizationRow(
    const EntryRange &entries, const CharacterCustomizationState &state,
    const std::uint32_t base_section, const int type_value, const int variation_value,
    bool *out_found = nullptr);

template <typename EntryRange>
inline std::vector<int>
CollectCharacterCustomizationCandidates(const EntryRange &entries,
                                        const CharacterCustomizationState &state,
                                        const CharacterCustomizationSlot slot,
                                        const int mode = 0) {
  std::vector<int> values;
  values.reserve(16);

  switch (slot) {
  case CharacterCustomizationSlot::Skin: {
    const int slot_count = GetCharacterCustomizationVariationSlotCount(entries, state, 0u, 0);
    for (int variation = 0; variation < slot_count; ++variation) {
      if (FindCharacterCustomizationRow(entries, state, 0u, 0, variation, mode) != nullptr) {
        values.push_back(variation);
      }
    }
    break;
  }
  case CharacterCustomizationSlot::Face: {
    const int slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 1u);
    for (int type_value = 0; type_value < slot_count; ++type_value) {
      if (FindCharacterCustomizationRow(entries, state, 1u, type_value, state.skin, mode) !=
          nullptr) {
        values.push_back(type_value);
      }
    }
    break;
  }
  case CharacterCustomizationSlot::HairStyle: {
    const int slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 3u);
    for (int type_value = 0; type_value < slot_count; ++type_value) {
      if (FindCharacterCustomizationRow(entries, state, 3u, type_value, state.hair_color, mode) !=
          nullptr) {
        values.push_back(type_value);
      }
    }
    break;
  }
  case CharacterCustomizationSlot::HairColor: {
    const int slot_count =
        GetCharacterCustomizationVariationSlotCount(entries, state, 3u, state.hair_style);
    for (int variation = 0; variation < slot_count; ++variation) {
      if (FindCharacterCustomizationRow(entries, state, 3u, state.hair_style, variation, mode) !=
          nullptr) {
        values.push_back(variation);
      }
    }
    break;
  }
  case CharacterCustomizationSlot::FacialHair: {
    const int slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 2u);
    for (int type_value = 0; type_value < slot_count; ++type_value) {
      if (FindCharacterCustomizationRow(entries, state, 2u, type_value, state.hair_color, mode) !=
          nullptr) {
        values.push_back(type_value);
      }
    }
    break;
  }
  }

  return values;
}

template <typename EntryRange>
inline const openwow::data::dbc::CharSectionsEntry *
FindCharacterCustomizationRow(const EntryRange &entries, const CharacterCustomizationState &state,
                              const std::uint32_t base_section, const int type_value,
                              const int variation_value, const int mode) {
  const auto *entry = GetUnfilteredCharacterCustomizationRow(entries, state, base_section,
                                                             type_value, variation_value);
  if (entry == nullptr) {
    return nullptr;
  }
  const int filter_mode = CharacterCreateFilterModeForClass(mode, state.class_id);
  if (!CharacterCreateFilterMatchesFlags(entry->flags, filter_mode)) {
    return nullptr;
  }
  return entry;
}

template <typename EntryRange>
inline const openwow::data::dbc::CharSectionsEntry *GetUnfilteredCharacterCustomizationRow(
    const EntryRange &entries, const CharacterCustomizationState &state,
    const std::uint32_t base_section, const int type_value, const int variation_value,
    bool *out_found) {
  if (out_found != nullptr) {
    *out_found = false;
  }

  if (base_section > 4u || type_value < 0 || variation_value < 0) {
    return nullptr;
  }

  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, base_section);
  if (type_slot_count <= 0 || type_value >= type_slot_count) {
    return nullptr;
  }

  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, base_section, type_value);
  if (variation_slot_count <= 0 || variation_value >= variation_slot_count) {
    return nullptr;
  }

  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    const auto &entry = *it;
    if (static_cast<int>(entry.race_id) != state.race_id ||
        static_cast<int>(entry.sex_id) != state.sex_id || entry.base_section != base_section ||
        static_cast<int>(entry.type) != type_value ||
        static_cast<int>(entry.variation) != variation_value) {
      continue;
    }
    if (out_found != nullptr) {
      *out_found = true;
    }
    return &entry;
  }

  return nullptr;
}

template <typename EntryRange>
inline const openwow::data::dbc::CharSectionsEntry *FindFirstCharacterCustomizationRowForType(
    const EntryRange &entries, const CharacterCustomizationState &state,
    const std::uint32_t base_section, const int type_value, const int mode = 0) {
  const int filter_mode = CharacterCreateFilterModeForClass(mode, state.class_id);
  const openwow::data::dbc::CharSectionsEntry *best = nullptr;
  for (const auto &entry : entries) {
    if (static_cast<int>(entry.race_id) != state.race_id ||
        static_cast<int>(entry.sex_id) != state.sex_id || entry.base_section != base_section ||
        static_cast<int>(entry.type) != type_value ||
        !CharacterCreateFilterMatchesFlags(entry.flags, filter_mode)) {
      continue;
    }
    if (best == nullptr || entry.variation < best->variation) {
      best = &entry;
    }
  }
  return best;
}

template <typename EntryRange>
inline int GetCharacterCustomizationTypeSlotCount(const EntryRange &entries,
                                                  const CharacterCustomizationState &state,
                                                  const std::uint32_t base_section) {

  int max_value = -1;
  for (const auto &entry : entries) {
    if (static_cast<int>(entry.race_id) != state.race_id ||
        static_cast<int>(entry.sex_id) != state.sex_id || entry.base_section != base_section) {
      continue;
    }
    max_value = std::max(max_value, static_cast<int>(entry.type));
  }
  return max_value + 1;
}

template <typename EntryRange>
inline int GetCharacterCustomizationVariationSlotCount(const EntryRange &entries,
                                                       const CharacterCustomizationState &state,
                                                       const std::uint32_t base_section,
                                                       const int type_value) {
  int max_value = -1;
  for (const auto &entry : entries) {
    if (static_cast<int>(entry.race_id) != state.race_id ||
        static_cast<int>(entry.sex_id) != state.sex_id || entry.base_section != base_section ||
        static_cast<int>(entry.type) != type_value) {
      continue;
    }
    max_value = std::max(max_value, static_cast<int>(entry.variation));
  }
  return max_value + 1;
}

template <typename EntryRange>
inline int GetCharacterCustomizationMaxTypeValue(const EntryRange &entries,
                                                 const CharacterCustomizationState &state,
                                                 const std::uint32_t base_section) {
  return GetCharacterCustomizationTypeSlotCount(entries, state, base_section) - 1;
}

template <typename EntryRange>
inline int GetCharacterCustomizationMaxVariationValue(const EntryRange &entries,
                                                      const CharacterCustomizationState &state,
                                                      const std::uint32_t base_section,
                                                      const int type_value) {
  return GetCharacterCustomizationVariationSlotCount(entries, state, base_section, type_value) - 1;
}

template <typename EntryRange>
inline bool HasUnfilteredCharacterCustomizationRow(const EntryRange &entries,
                                                   const CharacterCustomizationState &state,
                                                   const std::uint32_t base_section,
                                                   const int type_value,
                                                   const int variation_value) {
  return GetUnfilteredCharacterCustomizationRow(
             entries, state, base_section, type_value, variation_value) != nullptr;
}

inline int WrapCustomizationValue(const int value, const int delta, const int max_value) {
  if (max_value < 0) {
    return value;
  }
  if (delta > 0) {
    return value >= max_value ? 0 : value + 1;
  }
  return value <= 0 ? max_value : value - 1;
}

template <typename EntryRange>
inline bool IsSkinSelectionCompatible(const EntryRange &entries,
                                      const CharacterCustomizationState &state, const int skin,
                                      const int mode = 0) {
  if (FindCharacterCustomizationRow(entries, state, 0u, 0, skin, mode) == nullptr ||
      FindCharacterCustomizationRow(entries, state, 1u, state.face, skin, mode) == nullptr) {
    return false;
  }

  if (mode == 2) {
    return true;
  }

  return FindCharacterCustomizationRow(entries, state, 4u, 0, skin, mode) != nullptr;
}

template <typename EntryRange>
inline bool CycleSkinCustomizationSelection(CharacterCustomizationState &state,
                                            const EntryRange &entries, const int delta,
                                            const int mode = 0) {
  const int skin_slot_count = GetCharacterCustomizationVariationSlotCount(entries, state, 0u, 0);
  if (skin_slot_count <= 0) {
    return false;
  }

  const int start = state.skin;
  int candidate = start;
  while (true) {
    candidate = WrapCustomizationValue(candidate, delta, skin_slot_count - 1);
    if (candidate == start) {
      break;
    }
    if (!IsSkinSelectionCompatible(entries, state, candidate, mode)) {
      continue;
    }
    state.skin = candidate;
    state.skin_selection_anchor = candidate;
    return true;
  }

  return false;
}

template <typename EntryRange>
inline bool IsFaceSelectionCompatible(const EntryRange &entries,
                                      const CharacterCustomizationState &state, const int face,
                                      const int skin, const int mode = 0) {
  if (FindCharacterCustomizationRow(entries, state, 1u, face, skin, mode) == nullptr ||
      FindCharacterCustomizationRow(entries, state, 0u, 0, skin, mode) == nullptr) {
    return false;
  }

  if (mode == 2) {
    return true;
  }

  return FindCharacterCustomizationRow(entries, state, 4u, 0, skin, mode) != nullptr;
}

template <typename EntryRange>
inline int FindCompatibleFaceSkin(CharacterCustomizationState state, const EntryRange &entries,
                                  const int face, const int mode = 0) {
  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, 1u, face);
  if (variation_slot_count <= 0) {
    return -1;
  }

  const int anchor = std::max(state.skin_selection_anchor, 0);
  for (int ordinal = 0; ordinal < variation_slot_count; ++ordinal) {
    const int candidate_skin = (anchor + ordinal) % variation_slot_count;
    if (IsFaceSelectionCompatible(entries, state, face, candidate_skin, mode)) {
      return candidate_skin;
    }
  }

  return -1;
}

template <typename EntryRange>
inline bool CycleHairColorCustomizationSelection(CharacterCustomizationState &state,
                                                 const EntryRange &entries, const int delta,
                                                 const int mode = 0) {
  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, 3u, state.hair_style);
  if (variation_slot_count <= 0) {
    return false;
  }

  const int start = state.hair_color;
  int candidate = start;
  while (true) {
    candidate = WrapCustomizationValue(candidate, delta, variation_slot_count - 1);
    if (candidate == start) {
      break;
    }
    if (FindCharacterCustomizationRow(entries, state, 3u, state.hair_style, candidate, mode) ==
        nullptr) {
      continue;
    }
    state.hair_color = candidate;
    return true;
  }

  return false;
}

template <typename EntryRange>
inline bool CycleFaceCustomizationSelection(CharacterCustomizationState &state,
                                            const EntryRange &entries, const int delta,
                                            const int mode = 0) {
  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 1u);
  if (type_slot_count <= 0) {
    return false;
  }

  const int start = state.face;
  int candidate = start;
  while (true) {
    candidate = WrapCustomizationValue(candidate, delta, type_slot_count - 1);
    if (candidate == start) {
      break;
    }
    const int compatible_skin = FindCompatibleFaceSkin(state, entries, candidate, mode);
    if (compatible_skin < 0) {
      continue;
    }

    if (IsFaceSelectionCompatible(entries, state, candidate, state.skin, mode)) {
      state.face = candidate;
    } else {
      state.skin = compatible_skin;
      state.face = candidate;
    }
    return true;
  }

  return false;
}

template <typename FacialHairStyleRange>
inline int CountCharacterCreateFacialHairStyles(const FacialHairStyleRange &styles,
                                                const CharacterCustomizationState &state);

template <typename EntryRange, typename FacialHairStyleRange>
inline bool CycleHairStyleCustomizationSelection(CharacterCustomizationState &state,
                                                 const EntryRange &entries,
                                                 const FacialHairStyleRange &facial_hair_styles,
                                                 const int delta, const int mode = 0) {
  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 3u);
  if (type_slot_count <= 0) {
    return false;
  }

  const int start = state.hair_style;
  int candidate = start;
  while (true) {
    candidate = WrapCustomizationValue(candidate, delta, type_slot_count - 1);
    if (candidate == start) {
      break;
    }
    if (GetCharacterCustomizationVariationSlotCount(entries, state, 3u, candidate) <= 0) {
      continue;
    }
    const auto *first =
        FindFirstCharacterCustomizationRowForType(entries, state, 3u, candidate, mode);
    if (first == nullptr) {
      continue;
    }
    if (FindCharacterCustomizationRow(entries, state, 3u, candidate, state.hair_color, mode) !=
        nullptr) {
      state.hair_style = candidate;
    } else {
      state.hair_color = static_cast<int>(first->variation);
      state.hair_style = candidate;

      const int filter_mode = CharacterCreateFilterModeForClass(mode, state.class_id);
      const int new_facial =
          ResolveCharacterCreateFacialHairByOrdinal(entries, facial_hair_styles, state, 0,
                                                    filter_mode);
      if (new_facial >= 0) {
        state.facial_hair = new_facial;
      }
    }
    return true;
  }

  return false;
}

template <typename FacialHairStyleRange>
inline int CountCharacterCreateFacialHairStyles(const FacialHairStyleRange &styles,
                                                const CharacterCustomizationState &state) {

  int count = 0;
  for (const auto &entry : styles) {
    if (static_cast<int>(entry.race_id) != state.race_id ||
        static_cast<int>(entry.sex_id) != state.sex_id) {
      continue;
    }
    ++count;
  }
  return count;
}

template <typename EntryRange, typename FacialHairStyleRange>
inline bool CycleFacialHairCustomizationSelection(CharacterCustomizationState &state,
                                                  const EntryRange &entries,
                                                  const FacialHairStyleRange &styles,
                                                  const int delta, const int mode = 0) {
  if (GetUnfilteredCharacterCustomizationRow(
          entries, state, 2u, state.facial_hair, state.hair_color) == nullptr) {
    const int facial_style_count = CountCharacterCreateFacialHairStyles(styles, state);
    if (facial_style_count <= 0) {
      return false;
    }
    const int candidate = WrapCustomizationValue(state.facial_hair, delta, facial_style_count - 1);
    if (candidate == state.facial_hair) {
      return false;
    }
    state.facial_hair = candidate;
    return true;
  }

  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 2u);
  if (type_slot_count <= 0) {
    return false;
  }

  const int start = state.facial_hair;
  int candidate = start;
  while (true) {
    candidate = WrapCustomizationValue(candidate, delta, type_slot_count - 1);
    if (candidate == start) {
      break;
    }
    if (GetCharacterCustomizationVariationSlotCount(entries, state, 2u, candidate) <= 0) {
      continue;
    }
    const auto *first =
        FindFirstCharacterCustomizationRowForType(entries, state, 2u, candidate, mode);
    if (first == nullptr) {
      continue;
    }
    if (FindCharacterCustomizationRow(entries, state, 2u, candidate, state.hair_color, mode) !=
        nullptr) {
      state.facial_hair = candidate;
    } else {
      state.hair_color = static_cast<int>(first->variation);
      state.facial_hair = candidate;
    }
    return true;
  }

  return false;
}

template <typename EntryRange, typename FacialHairStyleRange, typename SelectOrdinalFn>
inline void RandomizeCharacterCustomizationWithDbc(
    CharacterCustomizationState &state, const EntryRange &entries,
    const FacialHairStyleRange &facial_hair_styles, SelectOrdinalFn &&select_ordinal,
    const CharacterCustomizationRandomizationOrder order, const int mode = 0) {
  const auto randomize_slot = [&entries, &select_ordinal,
                               mode](CharacterCustomizationState &current,
                                     const CharacterCustomizationSlot slot,
                                     const int fallback_value) -> int {
    const auto candidates = CollectCharacterCustomizationCandidates(entries, current, slot, mode);
    if (candidates.empty()) {
      return fallback_value;
    }
    return candidates[select_ordinal(candidates.size())];
  };

  state.skin = randomize_slot(state, CharacterCustomizationSlot::Skin, state.skin);
  switch (order) {
  case CharacterCustomizationRandomizationOrder::SetupModel:
    state.hair_style =
        randomize_slot(state, CharacterCustomizationSlot::HairStyle, state.hair_style);
    state.hair_color =
        randomize_slot(state, CharacterCustomizationSlot::HairColor, state.hair_color);
    state.face = randomize_slot(state, CharacterCustomizationSlot::Face, state.face);
    break;
  case CharacterCustomizationRandomizationOrder::ScriptRandomize:
    state.face = randomize_slot(state, CharacterCustomizationSlot::Face, state.face);
    state.hair_style =
        randomize_slot(state, CharacterCustomizationSlot::HairStyle, state.hair_style);
    state.hair_color =
        randomize_slot(state, CharacterCustomizationSlot::HairColor, state.hair_color);
    break;
  }

  const auto facial_candidates = CollectCharacterCustomizationCandidates(
      entries, state, CharacterCustomizationSlot::FacialHair, mode);
  if (!facial_candidates.empty()) {
    state.facial_hair = facial_candidates[select_ordinal(facial_candidates.size())];
  } else {
    const int facial_style_count = CountCharacterCreateFacialHairStyles(facial_hair_styles, state);
    if (facial_style_count > 0) {
      state.facial_hair =
          static_cast<int>(select_ordinal(static_cast<std::size_t>(facial_style_count)));
    }
  }

  state.skin_selection_anchor = state.skin;
}

inline int CharacterCreateSelectorFromSelectionMode(const int filter_mode) {
  switch (filter_mode) {
  case 2:
  case 3:
    return 1;
  case 4:
    return 2;
  case 5:
  case 6:
    return 3;
  default:
    return 0;
  }
}

template <typename EntryRange>
inline bool IsCharacterCreateSkinSelectionValid(const EntryRange &entries,
                                                const CharacterCustomizationState &state,
                                                const int mode = 0) {
  if (state.skin < 0) {
    return false;
  }
  const int slot_count = GetCharacterCustomizationVariationSlotCount(entries, state, 0u, 0);
  if (slot_count <= 0 || state.skin >= slot_count) {
    return false;
  }
  return FindCharacterCustomizationRow(entries, state, 0u, 0, state.skin, mode) != nullptr;
}

template <typename EntryRange>
inline int CountValidCharacterCreateSkinSelections(const EntryRange &entries,
                                                   const CharacterCustomizationState &state,
                                                   const int mode = 0) {
  const int slot_count = GetCharacterCustomizationVariationSlotCount(entries, state, 0u, 0);
  int count = 0;
  for (int variation = 0; variation < slot_count; ++variation) {
    if (FindCharacterCustomizationRow(entries, state, 0u, 0, variation, mode) != nullptr) {
      ++count;
    }
  }
  return count;
}

template <typename EntryRange>
inline int ResolveCharacterCreateSkinByOrdinal(const EntryRange &entries,
                                               const CharacterCustomizationState &state,
                                               const int ordinal,
                                               const int mode = 0) {
  if (ordinal < 0) {
    return -1;
  }
  const int slot_count = GetCharacterCustomizationVariationSlotCount(entries, state, 0u, 0);
  int visible_ordinal = 0;
  for (int variation = 0; variation < slot_count; ++variation) {
    if (FindCharacterCustomizationRow(entries, state, 0u, 0, variation, mode) == nullptr) {
      continue;
    }
    if (visible_ordinal == ordinal) {
      return variation;
    }
    ++visible_ordinal;
  }
  return -1;
}

template <typename EntryRange>
inline bool IsCharacterCreateFaceSelectionValid(const EntryRange &entries,
                                                const CharacterCustomizationState &state,
                                                const int mode = 0) {
  if (state.face < 0) {
    return false;
  }
  const int slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 1u);
  if (slot_count <= 0 || state.face >= slot_count) {
    return false;
  }
  return FindCharacterCustomizationRow(entries, state, 1u, state.face, state.skin, mode) !=
         nullptr;
}

template <typename EntryRange>
inline int CountValidCharacterCreateFaces(const EntryRange &entries,
                                          const CharacterCustomizationState &state,
                                          const int mode = 0) {
  const int slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 1u);
  int count = 0;
  for (int type_value = 0; type_value < slot_count; ++type_value) {
    if (FindCharacterCustomizationRow(entries, state, 1u, type_value, state.skin, mode) !=
        nullptr) {
      ++count;
    }
  }
  return count;
}

template <typename EntryRange>
inline int ResolveCharacterCreateFaceByOrdinal(const EntryRange &entries,
                                               const CharacterCustomizationState &state,
                                               const int ordinal,
                                               const int mode = 0) {
  if (ordinal < 0) {
    return -1;
  }
  const int slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 1u);
  int visible_ordinal = 0;
  for (int type_value = 0; type_value < slot_count; ++type_value) {
    if (FindCharacterCustomizationRow(entries, state, 1u, type_value, state.skin, mode) ==
        nullptr) {
      continue;
    }
    if (visible_ordinal == ordinal) {
      return type_value;
    }
    ++visible_ordinal;
  }
  return -1;
}

template <typename EntryRange>
inline bool IsCharacterCreateHairStyleColorSelectionValid(const EntryRange &entries,
                                                          const CharacterCustomizationState &state,
                                                          const int mode = 0) {
  if (state.hair_style < 0 || state.hair_color < 0) {
    return false;
  }
  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 3u);
  if (type_slot_count <= 0 || state.hair_style >= type_slot_count) {
    return false;
  }
  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, 3u, state.hair_style);
  if (variation_slot_count <= 0 || state.hair_color >= variation_slot_count) {
    return false;
  }
  return FindCharacterCustomizationRow(entries, state, 3u, state.hair_style, state.hair_color,
                                       mode) != nullptr;
}

template <typename EntryRange>
inline int CountValidCharacterCreateHairStylesForColor(const EntryRange &entries,
                                                       const CharacterCustomizationState &state,
                                                       const int hair_color,
                                                       const int mode = 0) {
  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 3u);
  int count = 0;
  for (int type_value = 0; type_value < type_slot_count; ++type_value) {
    if (FindCharacterCustomizationRow(entries, state, 3u, type_value, hair_color, mode) !=
        nullptr) {
      ++count;
    }
  }
  return count;
}

template <typename EntryRange>
inline int ResolveCharacterCreateHairStyleByOrdinal(const EntryRange &entries,
                                                    const CharacterCustomizationState &state,
                                                    const int hair_color, const int ordinal,
                                                    const int mode = 0) {
  if (ordinal < 0) {
    return -1;
  }
  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 3u);
  int visible_ordinal = 0;
  for (int type_value = 0; type_value < type_slot_count; ++type_value) {
    if (FindCharacterCustomizationRow(entries, state, 3u, type_value, hair_color, mode) ==
        nullptr) {
      continue;
    }
    if (visible_ordinal == ordinal) {
      return type_value;
    }
    ++visible_ordinal;
  }
  return -1;
}

template <typename EntryRange>
inline int CountValidCharacterCreateHairColorsForHairStyle(const EntryRange &entries,
                                                           const CharacterCustomizationState &state,
                                                           const int hair_style,
                                                           const int mode = 0) {
  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, 3u, hair_style);
  int count = 0;
  for (int variation = 0; variation < variation_slot_count; ++variation) {
    if (FindCharacterCustomizationRow(entries, state, 3u, hair_style, variation, mode) !=
        nullptr) {
      ++count;
    }
  }
  return count;
}

template <typename EntryRange>
inline int ResolveCharacterCreateHairColorByOrdinal(const EntryRange &entries,
                                                    const CharacterCustomizationState &state,
                                                    const int hair_style, const int ordinal,
                                                    const int mode = 0) {
  if (ordinal < 0) {
    return -1;
  }
  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, 3u, hair_style);
  int visible_ordinal = 0;
  for (int variation = 0; variation < variation_slot_count; ++variation) {
    if (FindCharacterCustomizationRow(entries, state, 3u, hair_style, variation, mode) ==
        nullptr) {
      continue;
    }
    if (visible_ordinal == ordinal) {
      return variation;
    }
    ++visible_ordinal;
  }
  return -1;
}

template <typename EntryRange, typename FacialHairStyleRange>
inline bool IsCharacterCreateFacialHairSelectionValid(const EntryRange &entries,
                                                      const FacialHairStyleRange &styles,
                                                      const CharacterCustomizationState &state,
                                                      const int mode = 0) {
  const int facial_style_count = CountCharacterCreateFacialHairStyles(styles, state);

  if (state.facial_hair < 0 || state.facial_hair > facial_style_count) {
    return false;
  }

  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 2u);
  if (type_slot_count <= 0 || state.facial_hair >= type_slot_count || state.hair_color < 0) {
    return true;
  }

  const int variation_slot_count =
      GetCharacterCustomizationVariationSlotCount(entries, state, 2u, state.facial_hair);
  if (variation_slot_count <= 0 || state.hair_color >= variation_slot_count) {
    return true;
  }

  return FindCharacterCustomizationRow(entries, state, 2u, state.facial_hair, state.hair_color,
                                       mode) != nullptr;
}

template <typename EntryRange, typename FacialHairStyleRange>
inline int CountValidCharacterCreateFacialHairSelections(const EntryRange &entries,
                                                         const FacialHairStyleRange &styles,
                                                         const CharacterCustomizationState &state,
                                                         const int mode = 0) {
  const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 2u);
  if (type_slot_count <= 0) {
    return CountCharacterCreateFacialHairStyles(styles, state);
  }

  int count = 0;
  for (int type_value = 0; type_value < type_slot_count; ++type_value) {
    if (FindCharacterCustomizationRow(entries, state, 2u, type_value, state.hair_color, mode) !=
        nullptr) {
      ++count;
    }
  }
  return count;
}

template <typename EntryRange, typename FacialHairStyleRange>
inline int ResolveCharacterCreateFacialHairByOrdinal(const EntryRange &entries,
                                                     const FacialHairStyleRange &styles,
                                                     const CharacterCustomizationState &state,
                                                     const int ordinal,
                                                     const int filter_mode) {
  if (ordinal < 0) {
    return -1;
  }

  const int mode = CharacterCreateSelectorFromSelectionMode(filter_mode);
  if (GetCharacterCustomizationTypeSlotCount(entries, state, 2u) > 0) {
    const int count =
        CountValidCharacterCreateFacialHairSelections(entries, styles, state, mode);
    if (ordinal >= count) {
      return -1;
    }
    int visible_ordinal = 0;
    const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 2u);
    for (int type_value = 0; type_value < type_slot_count; ++type_value) {
      if (FindCharacterCustomizationRow(entries, state, 2u, type_value, state.hair_color, mode) ==
          nullptr) {
        continue;
      }
      if (visible_ordinal == ordinal) {
        return type_value;
      }
      ++visible_ordinal;
    }
    return -1;
  }

  const int facial_style_count = CountCharacterCreateFacialHairStyles(styles, state);
  if (ordinal < facial_style_count) {
    return ordinal;
  }
  return -1;
}

template <typename EntryRange, typename FacialHairStyleRange>
inline bool NormalizeCharacterCustomizationForInit(
    CharacterCustomizationState &state, const EntryRange &entries,
    const FacialHairStyleRange &facial_hair_styles, const int mode = 0) {
  bool changed = false;

  if (!IsCharacterCreateSkinSelectionValid(entries, state, mode)) {
    const int count = CountValidCharacterCreateSkinSelections(entries, state, mode);
    const int next_skin =
        count <= 0 ? 0 : ResolveCharacterCreateSkinByOrdinal(entries, state, state.skin % count, mode);
    changed |= state.skin != next_skin;
    state.skin = next_skin;
  }

  if (!IsCharacterCreateFaceSelectionValid(entries, state, mode)) {
    const int count = CountValidCharacterCreateFaces(entries, state, mode);
    const int next_face =
        count <= 0 ? 0 : ResolveCharacterCreateFaceByOrdinal(entries, state, state.face % count, mode);
    changed |= state.face != next_face;
    state.face = next_face;
  }

  if (!IsCharacterCreateHairStyleColorSelectionValid(entries, state, mode)) {
    const int count =
        CountValidCharacterCreateHairStylesForColor(entries, state, state.hair_color, mode);
    if (count > 0) {
      const int next_hair_style = ResolveCharacterCreateHairStyleByOrdinal(
          entries, state, state.hair_color, state.hair_style % count, mode);
      changed |= state.hair_style != next_hair_style;
      state.hair_style = next_hair_style;
    } else {
      const int type_slot_count = GetCharacterCustomizationTypeSlotCount(entries, state, 3u);
      std::vector<int> hair_color_counts(
          static_cast<std::size_t>(std::max(type_slot_count, 0)), 0);
      int valid_hair_style_count = 0;
      for (int type_value = 0; type_value < type_slot_count; ++type_value) {
        hair_color_counts[static_cast<std::size_t>(type_value)] =
            CountValidCharacterCreateHairColorsForHairStyle(entries, state, type_value, mode);
        if (hair_color_counts[static_cast<std::size_t>(type_value)] > 0) {
          ++valid_hair_style_count;
        }
      }

      if (valid_hair_style_count <= 0) {
        changed |= state.hair_color != 0 || state.hair_style != 0;
        state.hair_color = 0;
        state.hair_style = 0;
      } else {
        int ordinal = state.hair_style % valid_hair_style_count;
        int next_hair_style = 0;
        for (int type_value = 0; type_value < type_slot_count; ++type_value) {
          if (hair_color_counts[static_cast<std::size_t>(type_value)] <= 0) {
            continue;
          }
          if (ordinal == 0) {
            next_hair_style = type_value;
            break;
          }
          --ordinal;
        }

        const int next_hair_color = ResolveCharacterCreateHairColorByOrdinal(
            entries, state, next_hair_style,
            state.hair_color % hair_color_counts[static_cast<std::size_t>(next_hair_style)], mode);
        changed |= state.hair_style != next_hair_style;
        state.hair_style = next_hair_style;
        changed |= state.hair_color != next_hair_color;
        state.hair_color = next_hair_color;
      }
    }
  }

  if (!IsCharacterCreateFacialHairSelectionValid(entries, facial_hair_styles, state, mode)) {
    const int filter_mode = CharacterCreateFilterModeForClass(mode, state.class_id);
    const int count =
        CountValidCharacterCreateFacialHairSelections(entries, facial_hair_styles, state, mode);
    const int next_facial_hair =
        count <= 0 ? 0
                   : ResolveCharacterCreateFacialHairByOrdinal(
                         entries, facial_hair_styles, state, state.facial_hair % count,
                         filter_mode);
    changed |= state.facial_hair != next_facial_hair;
    state.facial_hair = next_facial_hair;
  }

  state.skin_selection_anchor = state.skin;
  return changed;
}

class LegacyAdlerRandom {
public:
  explicit LegacyAdlerRandom(
      openwow::foundation::hashing::AdlerSeedState& shared_state)
      : state_(shared_state) {}

  [[nodiscard]] std::uint32_t Next() {
    return openwow::foundation::hashing::AdvanceAdlerSeed(state_);
  }

  [[nodiscard]] std::uint32_t SelectOrdinal(const std::size_t count) {
    if (count == 0u) {
      return 0u;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(Next())) >> 32);
  }

private:
  openwow::foundation::hashing::AdlerSeedState& state_;
};

}
