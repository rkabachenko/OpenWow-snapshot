#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::data {

struct alignas(4) QuestCacheRecord {
  std::uint32_t header[18];
  float header_float_48;
  std::uint32_t header_tail[2];
  std::uint32_t reward_item_id[4];
  std::uint32_t reward_item_count[4];
  std::uint32_t char_title_id;
  std::uint32_t reward_choice_item_id[5];
  std::uint32_t reward_choice_item_count[6];
  std::uint32_t source_item_count;
  float point_x;
  float point_y;
  std::uint32_t point_map_id;
  char title[512];
  char details[3000];
  char objectives[3000];
  char end_text[512];
  std::uint32_t objective_creature_or_go_id[4];
  std::uint32_t objective_creature_or_go_count[4];
  std::uint32_t objective_spell_cast[6];
  std::uint32_t objective_display_id[6];
  std::uint32_t objective_item_id[4];
  std::uint32_t objective_item_count[4];
  char objective_text[4][256];
  std::uint32_t post_objective[4];
  char offer_reward_text[2048];
  std::uint32_t reward_faction[15];
  std::uint32_t complete_emote;
};

static_assert(sizeof(QuestCacheRecord) == 10468);
static_assert(offsetof(QuestCacheRecord, header) == 0x00);
static_assert(offsetof(QuestCacheRecord, header_float_48) == 0x48);
static_assert(offsetof(QuestCacheRecord, header_tail) == 0x4C);
static_assert(offsetof(QuestCacheRecord, reward_item_id) == 0x54);
static_assert(offsetof(QuestCacheRecord, reward_item_count) == 0x64);
static_assert(offsetof(QuestCacheRecord, char_title_id) == 0x74);
static_assert(offsetof(QuestCacheRecord, reward_choice_item_id) == 0x78);
static_assert(offsetof(QuestCacheRecord, reward_choice_item_count) == 0x8C);
static_assert(offsetof(QuestCacheRecord, source_item_count) == 0xA4);
static_assert(offsetof(QuestCacheRecord, point_x) == 0xA8);
static_assert(offsetof(QuestCacheRecord, point_y) == 0xAC);
static_assert(offsetof(QuestCacheRecord, point_map_id) == 0xB0);
static_assert(offsetof(QuestCacheRecord, title) == 0xB4);
static_assert(offsetof(QuestCacheRecord, details) == 0x2B4);
static_assert(offsetof(QuestCacheRecord, objectives) == 0xE6C);
static_assert(offsetof(QuestCacheRecord, end_text) == 0x1A24);
static_assert(offsetof(QuestCacheRecord, objective_creature_or_go_id) == 0x1C24);
static_assert(offsetof(QuestCacheRecord, objective_creature_or_go_count) == 0x1C34);
static_assert(offsetof(QuestCacheRecord, objective_spell_cast) == 0x1C44);
static_assert(offsetof(QuestCacheRecord, objective_display_id) == 0x1C5C);
static_assert(offsetof(QuestCacheRecord, objective_item_id) == 0x1C74);
static_assert(offsetof(QuestCacheRecord, objective_item_count) == 0x1C84);
static_assert(offsetof(QuestCacheRecord, objective_text) == 0x1C94);
static_assert(offsetof(QuestCacheRecord, post_objective) == 0x2094);
static_assert(offsetof(QuestCacheRecord, offer_reward_text) == 0x20A4);
static_assert(offsetof(QuestCacheRecord, reward_faction) == 0x28A4);
static_assert(offsetof(QuestCacheRecord, complete_emote) == 0x28E0);

QuestCacheRecord* QuestCacheRecord_CopyFrom(QuestCacheRecord* destination,
                                            const QuestCacheRecord* source);

}
