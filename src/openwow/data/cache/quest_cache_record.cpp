#include "openwow/data/cache/quest_cache_record.h"

#include "openwow/core/storm_string.h"

#include <cstring>

namespace openwow::data {

QuestCacheRecord* QuestCacheRecord_CopyFrom(QuestCacheRecord* destination,
                                            const QuestCacheRecord* source) {
  std::memcpy(destination->header, source->header, sizeof(destination->header));
  destination->header_float_48 = source->header_float_48;
  destination->header_tail[0] = source->header_tail[0];
  destination->header_tail[1] = source->header_tail[1];

  for (int index = 0; index < 4; ++index) {
    destination->post_objective[index] = source->post_objective[index];
  }
  destination->complete_emote = source->complete_emote;

  for (int index = 0; index < 15; ++index) {
    destination->reward_faction[index] = source->reward_faction[index];
  }
  for (int index = 0; index < 4; ++index) {
    destination->reward_item_id[index] = source->reward_item_id[index];
    destination->reward_item_count[index] = source->reward_item_count[index];
  }
  destination->char_title_id = source->char_title_id;

  for (int index = 0; index < 5; ++index) {
    destination->reward_choice_item_id[index] =
        source->reward_choice_item_id[index];
  }
  for (int index = 0; index < 6; ++index) {
    destination->reward_choice_item_count[index] =
        source->reward_choice_item_count[index];
  }

  destination->source_item_count = source->source_item_count;
  destination->point_x = source->point_x;
  destination->point_y = source->point_y;
  destination->point_map_id = source->point_map_id;

  openwow::core::SStrCopy(destination->title, source->title,
                          sizeof(destination->title));
  openwow::core::SStrCopy(destination->details, source->details,
                          sizeof(destination->details));
  openwow::core::SStrCopy(destination->objectives, source->objectives,
                          sizeof(destination->objectives));
  openwow::core::SStrCopy(destination->end_text, source->end_text,
                          sizeof(destination->end_text));
  openwow::core::SStrCopy(destination->offer_reward_text,
                          source->offer_reward_text,
                          sizeof(destination->offer_reward_text));

  for (int index = 0; index < 4; ++index) {
    destination->objective_creature_or_go_id[index] =
        source->objective_creature_or_go_id[index];
    destination->objective_creature_or_go_count[index] =
        source->objective_creature_or_go_count[index];
    destination->objective_item_id[index] = source->objective_item_id[index];
    destination->objective_item_count[index] =
        source->objective_item_count[index];
    openwow::core::SStrCopy(destination->objective_text[index],
                            source->objective_text[index],
                            sizeof(destination->objective_text[index]));
  }

  for (int index = 0; index < 6; ++index) {
    destination->objective_spell_cast[index] =
        source->objective_spell_cast[index];
    destination->objective_display_id[index] =
        source->objective_display_id[index];
  }

  return destination;
}

}
