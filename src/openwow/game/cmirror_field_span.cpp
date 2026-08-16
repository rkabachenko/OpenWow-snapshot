
#include "openwow/game/cmirror_field_span.h"

namespace openwow::game {

void CFieldSpanBase::Init(std::uintptr_t begin, std::uintptr_t end) {
  descriptor_begin = begin;
  descriptor_end = end;
}

void CFieldSpanObjectDerived::Init(std::uintptr_t begin,
                                   std::uintptr_t end) {
  CFieldSpanBase::Init(begin, end);

  type_section_descriptor_begin = begin + kObjectFieldBytes;
  type_section_compact_begin = end + kObjectCompactSlotCount * 4;
}

void CFieldSpanItem::Init(std::uintptr_t begin, std::uintptr_t end) {
  CFieldSpanBase::Init(begin, end);

  item_section_descriptor_begin = begin + kObjectFieldBytes;
  item_section_compact_begin = end + kObjectCompactSlotCount * 4;
}

void CFieldSpanContainer::Init(std::uintptr_t begin,
                                std::uintptr_t end) {
  CFieldSpanItem::Init(begin, end);

  container_section_descriptor_begin = begin + kItemFieldBytes;
  container_section_compact_begin = end + kItemCompactSlotEnd * 4;
}

void CFieldSpanPlayer::Init(std::uintptr_t begin,
                             std::uintptr_t end) {
  CFieldSpanObjectDerived::Init(begin, end);

  player_section_descriptor_begin = begin + kUnitFieldBytes;
  player_section_compact_begin = end + kUnitCompactSlotEnd * 4;
}

}
