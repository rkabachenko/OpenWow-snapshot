#pragma once

#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kObjectCompactSlotCount = 3;

inline constexpr std::uint32_t kItemCompactSlotEnd = 50;

inline constexpr std::uint32_t kContainerCompactSlotEnd = 122;

inline constexpr std::uint32_t kCorpseCompactSlotEnd = 6;

inline constexpr std::uint32_t kGameObjectCompactSlotEnd = 7;

inline constexpr std::uint32_t kUnitCompactSlotEnd = 126;

inline constexpr std::uint32_t kPlayerVisibleCompactSlotEnd = 299;

inline constexpr std::uint32_t kPlayerActiveCompactSlotEnd = 1169;

inline constexpr std::uint32_t kObjectFieldCount = 6;
inline constexpr std::uint32_t kItemFieldCount   = 64;
inline constexpr std::uint32_t kUnitFieldCount   = 148;

inline constexpr std::uint32_t kObjectFieldBytes = kObjectFieldCount * 4;
inline constexpr std::uint32_t kItemFieldBytes   = kItemFieldCount * 4;
inline constexpr std::uint32_t kUnitFieldBytes   = kUnitFieldCount * 4;

struct CFieldSpanBase {
  std::uintptr_t descriptor_begin{0};

  std::uintptr_t descriptor_end{0};

  void Init(std::uintptr_t begin, std::uintptr_t end);
};

struct CFieldSpanObjectDerived : CFieldSpanBase {
  std::uintptr_t type_section_descriptor_begin{0};

  std::uintptr_t type_section_compact_begin{0};

  void Init(std::uintptr_t begin, std::uintptr_t end);
};

struct CFieldSpanItem : CFieldSpanBase {
  std::uintptr_t item_section_descriptor_begin{0};

  std::uintptr_t item_section_compact_begin{0};

  void Init(std::uintptr_t begin, std::uintptr_t end);
};

struct CFieldSpanContainer : CFieldSpanItem {
  std::uintptr_t container_section_descriptor_begin{0};

  std::uintptr_t container_section_compact_begin{0};

  void Init(std::uintptr_t begin, std::uintptr_t end);
};

struct CFieldSpanPlayer : CFieldSpanObjectDerived {
  std::uintptr_t player_section_descriptor_begin{0};

  std::uintptr_t player_section_compact_begin{0};

  void Init(std::uintptr_t begin, std::uintptr_t end);
};

}
