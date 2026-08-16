
#pragma once

#include <cstdint>
#include <vector>

namespace openwow::storage::persistence {

struct CProfileBuffer {
  std::uint32_t list_prev = 0;
  std::uint32_t list_next = 0;
  std::uint32_t reference_count = 0;
  std::uint32_t capacity = 0;
  std::uint32_t used_bytes = 0;
};

struct ProfileIntrusiveNode {
  ProfileIntrusiveNode* next = nullptr;
  ProfileIntrusiveNode* prev = nullptr;
};

struct ProfileHashEntry {
  std::uint32_t hash = 0;
  ProfileIntrusiveNode primary_links{};
  ProfileIntrusiveNode secondary_links{};
  const char* name = nullptr;
};

struct ProfileHashBucket {
  ProfileIntrusiveNode root{};
};

struct ProfileKeyValue final : ProfileHashEntry {
  std::uint32_t field_18 = 0;
  std::uint32_t value_count = 0;
  const char** values = nullptr;
  std::uint32_t field_24 = 0;
};

using ProfileDestroyEntryFn = void (*)(ProfileHashEntry*);

struct ProfileHashTable {
  ProfileDestroyEntryFn destroy_entry = nullptr;
  std::uint32_t entry_list_link_offset = 12;
  ProfileIntrusiveNode entry_list_root{};
  std::uint32_t rehash_probe_counter = 0;
  std::uint32_t entry_count = 0;
  std::vector<ProfileHashBucket> buckets;
  std::int32_t bucket_mask = -1;
};

struct ProfileSection final : ProfileHashEntry {
  ProfileHashTable key_values{};
};

struct CProfile {
  void* vtable = nullptr;
  std::uint32_t field_04 = 0;
  ProfileHashTable sections{};
  std::uint32_t stringBlockCount = 0;
  ProfileIntrusiveNode string_blocks{};
};

CProfileBuffer* CProfileBuffer_Create(std::uint32_t requestedSize);
CProfile* CProfile_Create();

void CProfile_Destroy(CProfile* profile);

int CProfile_GetSection(CProfile* profile, const char* section, const char* key,
                        std::uint8_t* valueOut, int valueMaxLen,
                        std::uint32_t valueIndex);

int CProfile_GetValueInt(CProfile* profile, const char* section, const char* key,
                         std::uint32_t* value, std::uint32_t index);

int CProfile_LoadFile(const char* path, CProfile* profile);
int CProfile_LoadPath(CProfile* profile, const char* path);

void* TSHashTable_Lookup(ProfileHashTable* hashTable, const char* key);

void ProfileHashTable_Reset(ProfileHashTable* ht);

void ProfileHashTable_Init(ProfileHashTable* ht,
                           ProfileDestroyEntryFn destroy_entry);

void ProfileSection_Destroy(ProfileSection* section);

}
