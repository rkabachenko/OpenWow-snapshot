#pragma once

#include <cstdint>
#include <functional>

namespace openwow::data {

namespace db_cache_entry_size {

inline constexpr uint32_t kCreatureStats = 164;

inline constexpr uint32_t kGameObjectStats = 216;

inline constexpr uint32_t kItemName = 64;

inline constexpr uint32_t kItemStats = 572;

inline constexpr uint32_t kNPCText = 376;

inline constexpr uint32_t kNameCache = 408;

inline constexpr uint32_t kGuildStats = 820;

inline constexpr uint32_t kQuestCache = 10524;

inline constexpr uint32_t kPageTextCache = 68;

inline constexpr uint32_t kPetNameCache = 148;

inline constexpr uint32_t kCGPetition = 5120;

inline constexpr uint32_t kItemTextCache = 8072;

inline constexpr uint32_t kWardenCachedModule = 88;

inline constexpr uint32_t kArenaTeamCache = 180;

inline constexpr uint32_t kDanceCache = 208;

inline constexpr uint32_t kReverseEntry = 28;
}

inline constexpr uint32_t kDBCacheCallbackNodeSize = 32;

namespace db_cache_offsets {

inline constexpr uint32_t kCreature_FlagByte = 153;
inline constexpr uint32_t kCreature_CbListIdx = 37;
inline constexpr uint32_t kCreature_CbHeadIdx = 35;

inline constexpr uint32_t kGameObject_FlagByte = 205;
inline constexpr uint32_t kGameObject_CbListIdx = 50;
inline constexpr uint32_t kGameObject_CbHeadIdx = 48;

inline constexpr uint32_t kItem_FlagByte = 561;
inline constexpr uint32_t kItem_CbListIdx = 139;
inline constexpr uint32_t kItem_CbHeadIdx = 137;

inline constexpr uint32_t kName_FlagByte = 393;
inline constexpr uint32_t kName_CbListIdx = 97;
inline constexpr uint32_t kName_CbHeadIdx = 95;
inline constexpr uint32_t kName_DirtyByte = 392;

inline constexpr uint32_t kGuild_FlagByte = 809;
inline constexpr uint32_t kGuild_CbListIdx = 201;
inline constexpr uint32_t kGuild_CbHeadIdx = 199;

inline constexpr uint32_t kInvalidate_FlagByte = 137;
inline constexpr uint32_t kInvalidate_CbListIdx = 33;
inline constexpr uint32_t kInvalidate_CbHeadIdx = 31;

inline constexpr uint32_t kCGPetition_FlagByte = 5109;
inline constexpr uint32_t kCGPetition_CbListIdx = 1276;
inline constexpr uint32_t kCGPetition_LoadedByte = 5092;
inline constexpr uint32_t kCGPetition_DeferredByte = 5110;
inline constexpr uint32_t kCGPetition_EntryIdIdx = 1272;
}

namespace db_cache_wdb_record_sizes {
inline constexpr uint32_t kCreatureCache = 108;

inline constexpr uint32_t kGameObjectCache = 160;

inline constexpr uint32_t kItemNameCache = 8;

inline constexpr uint32_t kItemStatsCache = 516;

inline constexpr uint32_t kNPCTextCache = 320;

inline constexpr uint32_t kGuildStatsCache = 764;

inline constexpr uint32_t kQuestCache = 10468;

inline constexpr uint32_t kPageTextCache = 12;

inline constexpr uint32_t kPetNameCache = 92;

}

namespace db_cache_wdb_versions {
inline constexpr uint32_t kCreatureCache = 1;

inline constexpr uint32_t kGameObjectCache = 1;

inline constexpr uint32_t kItemNameCache = 1;

inline constexpr uint32_t kItemStatsCache = 5;

inline constexpr uint32_t kNPCTextCache = 1;

inline constexpr uint32_t kGuildStatsCache = 1;

inline constexpr uint32_t kQuestCache = 3;

inline constexpr uint32_t kPageTextCache = 1;

inline constexpr uint32_t kPetNameCache = 1;

}

namespace db_cache_ctor_rec_sizes {
inline constexpr uint32_t kCreatureStats = 156;

inline constexpr uint32_t kGameObjectStats = 208;

inline constexpr uint32_t kItemName = 56;

inline constexpr uint32_t kItemStats = 564;

inline constexpr uint32_t kGuildStats = 812;
inline constexpr uint32_t kPetName = 140;

}

namespace db_cache_lookup_offsets {

inline constexpr uint32_t kCreature_LoadedByte = 136;
inline constexpr uint32_t kCreature_CbIdx = 37;
inline constexpr uint32_t kCreature_IdIdx = 33;

inline constexpr uint32_t kGameObject_LoadedByte = 188;
inline constexpr uint32_t kGameObject_CbIdx = 50;
inline constexpr uint32_t kGameObject_IdIdx = 46;

inline constexpr uint32_t kItem_LoadedByte = 544;
inline constexpr uint32_t kItem_CbIdx = 139;
inline constexpr uint32_t kItem_IdIdx = 135;

inline constexpr uint32_t kNPCText_LoadedByte = 348;
inline constexpr uint32_t kNPCText_CbIdx = 90;
inline constexpr uint32_t kNPCText_IdIdx = 86;

inline constexpr uint32_t kQuest_LoadedByte = 10496;
inline constexpr uint32_t kQuest_CbIdx = 2627;
inline constexpr uint32_t kQuest_IdIdx = 2623;

inline constexpr uint32_t kCGPetition_LookupLoadedByte = 5092;
inline constexpr uint32_t kCGPetition_LookupCbIdx = 1276;
inline constexpr uint32_t kCGPetition_LookupIdIdx = 1272;
}

namespace db_cache_update_offsets {
inline constexpr uint32_t kCreature_UpdatingByte = 137;
inline constexpr uint32_t kCreature_LoadedByte = 120;
inline constexpr uint32_t kCreature_EntryIdIdx = 29;
inline constexpr uint32_t kCreature_CbHeadIdx = 33;
inline constexpr uint32_t kCreature_ReverseIdx = 31;
inline constexpr uint32_t kCreature_DeferredByte = 138;
}

namespace db_cache_ctor_link_offsets {
inline constexpr uint32_t kItemText = 8060;
}

}
