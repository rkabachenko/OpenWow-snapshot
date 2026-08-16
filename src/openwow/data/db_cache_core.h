#pragma once

#include <cstdint>

namespace openwow::data {

void DBCache_GetCacheDirectory(char *out);

void DBCache_UpdateEntry_NameCache(void *dest, const void *source);

void DBCache_UpdateEntry_CreatureCache(void *dest, const void *source);

void DBCache_Load_NameCache(void *dest, const void *source);

void TSExplicitList_LightArrayRoot_ResizeCapacity(void *list, unsigned int new_size);

struct CEquipmentSetRecord {
  uint32_t       field_0    = 0;
  uint32_t       _pad       = 0;
  uint64_t       guid       = 0;
  char           name[128]  = {};
  uint32_t      *items      = nullptr;
  uint32_t       itemCount  = 0;
  uint32_t       field_98   = 0;
};

struct ArenaTeamCacheRecord {
  uint32_t  team_type        = 0;
  char      team_name[96]    = {};
  uint32_t  background_color = 0;
  uint32_t  emblem_style     = 0;
  uint32_t  emblem_color     = 0;
  uint32_t  border_style     = 0;
  uint32_t  border_color     = 0;
  uint32_t  field_78         = 0;
};

static_assert(sizeof(ArenaTeamCacheRecord) == 124,
              "ArenaTeamCacheRecord must match IDA record size (124 bytes)");

}

namespace openwow::net { struct CDataStore; }

namespace openwow::data {

void CEquipmentSet_Serialize(const CEquipmentSetRecord &record,
                             openwow::net::CDataStore &store);

void ArenaTeamCacheRecord_Serialize(const ArenaTeamCacheRecord &record,
                                    openwow::net::CDataStore &store);

void DBCache_ClearAllEntries(void *cache);

void *DBCACHECALLBACK_Alloc(void *list, int insert_mode, int payload_size, char flags);

void TSExplicitList_UDBCACHEHASH_Realloc(void *list, unsigned int new_size, const char *debug_tag);

void TSExplicitList_UDBCACHEHASH_VCre_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VGam_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VIte_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VNPC_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VNam_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VGui_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VQue_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VPag_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VPet_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VCGP_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VWar_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VAre_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VDan_Realloc(void *list, unsigned int new_size);
void TSExplicitList_UDBCACHEHASH_VGam_DestroyAll(void *list);
void TSExplicitList_UDBCACHEHASH_VIte_DestroyAll(void *list);
void TSExplicitList_UDBCACHEHASH_VGui_DestroyAll(void *list);
void TSExplicitList_UDBCACHEHASH_VQue_DestroyAll(void *list);
void TSExplicitList_UDBCACHEHASH_VCGP_DestroyAll(void *list);

void TSExplicitList_UREVERSEENTRY_DestroyAll(void *list, const char *debug_tag);

void TSExplicitList_UREVERSEENTRY_VCr_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VGa_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VIt_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VNP_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VGu_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VQu_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VPa_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VPe_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VCG_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VWa_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VAr_DestroyAll(void *list);
void TSExplicitList_UREVERSEENTRY_VDa_DestroyAll(void *list);

void DBCacheReverseEntry_VCreatureStats_Destroy(void *entry);

void DBCacheReverseEntry_VCGPetition_Destroy(void *entry);

void DBCACHEHASH_VCreatureStats_Destroy(void *entry);

void DBCACHEHASH_VCreatureStats_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VGameObjectStats_Destroy(void *entry);

void DBCACHEHASH_VGameObjectStats_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VItemStats_Destroy(void *entry);

void DBCACHEHASH_VItemStats_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VGuildStats_Destroy(void *entry);

void DBCACHEHASH_VItemTextCache_Destroy(void *entry);

void DBCACHEHASH_VItemTextCache_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VPetNameCache_Destroy(void *entry);

void DBCACHEHASH_VCGPetition_Destroy(void *entry);

void DBCACHEHASH_VCGPetition_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VDanceCache_Destroy(void *entry);

void DBCACHEHASH_VDanceCache_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VWardenCachedModule_Destroy(void *entry);

void DBCACHEHASH_VWardenCachedModule_ScalarDeletingDestructor(void *entry);

void DBCACHEHASH_VArenaTeamCache_Destroy(void *entry);

void DBCACHEHASH_VArenaTeamCache_FreeNode(void *entry);

void TSHashTable_UDBCACHEHASH_VPet_Ctor(void *table);

void TSHashTable_UREVERSEENTRY_VPe_Ctor(void *table);

void TSHashTable_UDBCACHEHASH_VGui_Reset(void *table);

void TSHashTable_UDBCACHEHASH_VGui_Destroy(void *table);

void TSHashTable_UDBCACHEHASH_VGam_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VGu_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VGa_Destroy(void *table);

void TSHashTable_UDBCACHEHASH_VIte_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VIt_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VGu_Reset(void *table);

void TSHashTable_UREVERSEENTRY_VPe_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VPe_Reset(void *table);

void TSHashTable_UDBCACHEHASH_VCGP_Destroy(void *table);

void TSHashTable_UDBCACHEHASH_VCGP_Reset(void *table);

void TSHashTable_UDBCACHEHASH_VQue_Destroy(void *table);

void TSHashTable_UDBCACHEHASH_VQue_Reset(void *table);

void TSHashTable_UREVERSEENTRY_VQu_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VQu_Reset(void *table);

void TSHashTable_UREVERSEENTRY_VPa_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VCG_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VCG_Reset(void *table);

void TSHashTable_UREVERSEENTRY_VDa_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VDa_Reset(void *table);

void TSHashTable_UREVERSEENTRY_VWa_Destroy(void *table);

void TSHashTable_UREVERSEENTRY_VWa_Reset(void *table);

unsigned int TSHashTable_UDBCACHEHASH_VCGP_InitWithFourBuckets(void *table);

unsigned int TSHashTable_UDBCACHEHASH_VPet_InitWithFourBuckets(void *table);

unsigned int TSHashTable_UDBCACHEHASH_VIte_InitWithFourBuckets(void *table);

unsigned int TSHashTable_UDBCACHEHASH_VAre_InitWithFourBuckets(void *table);

unsigned int TSHashTable_UDBCACHEHASH_VDan_InitWithFourBuckets(void *table);

void DBCache_ClearPendingEntries(void *cache, std::uint32_t loaded_flag_offset);

void DBCache_Creature_ClearRecords(void *cache);
void DBCache_GameObject_ClearRecords(void *cache);
void DBCache_ItemName_ClearRecords(void *cache);
void DBCache_Item_ClearRecords(void *cache);
void DBCache_NpcText_ClearRecords(void *cache);
void DBCache_Name_ClearRecords(void *cache);
void DBCache_Guild_ClearRecords(void *cache);
void DBCache_Quest_ClearRecords(void *cache);
void DBCache_PageText_ClearRecords(void *cache);
void DBCache_PetName_ClearRecords(void *cache);
void DBCache_Petition_ClearRecords(void *cache);
void DBCache_ItemText_ClearRecords(void *cache);
void DBCache_WardenModule_ClearRecords(void *cache);

void DualLinkNode_Detach(void *entry);

void DBCache_ApplyCacheVersion_ItemName(void *cache, int version);

void DBCache_ApplyCacheVersion_Item(void *cache, int version);

void DBCache_ApplyCacheVersion_NpcText(void *cache, int version);

void DBCache_ApplyCacheVersion_Name(void *cache, int version);

void DBCache_ApplyCacheVersion_Guild(void *cache, int version);

void DBCache_ApplyCacheVersion_Quest(void *cache, int version);

void DBCache_ApplyCacheVersion_PageText(void *cache, int version);

void DBCache_ApplyCacheVersion_PetName(void *cache, int version);

void DBCache_ApplyCacheVersion_Petition(void *cache, int version);

void DBCache_ApplyCacheVersion_ItemText(void *cache, int version);

void DBCache_ApplyCacheVersion_WardenModule(void *cache, int version);

}
