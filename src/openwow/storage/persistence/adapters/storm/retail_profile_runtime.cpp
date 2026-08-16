
#include "openwow/storage/persistence/adapters/storm/retail_profile.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/storage/persistence/profile_document.h"
#include "openwow/storage/persistence/profile_value.h"
#include "openwow/storage/persistence/adapters/storm/retail_profile_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <string_view>

namespace openwow::storage::persistence {

namespace {

constexpr char kProfileSourceFile[] = ".\\Profile.cpp";
constexpr char kProfileSectionTag[] = "AU:Section<Profile>";
constexpr char kProfileKeyValueTag[] = "AU:KEYVALUE<Profile>";
constexpr int kErrorInvalidParameter = 87;

const void* s_profileVtable = nullptr;

void CopyProfileValueText(std::uint8_t* destination, const char* source,
                          int max_length) {
  if (!destination || !source) {
    return;
  }

  if (max_length == 0x7FFFFFFF) {
    openwow::core::SStrCopy(reinterpret_cast<char*>(destination), source,
                            0x7FFFFFFFu);
    return;
  }

  if (max_length <= 0) {

    destination[0] = 0;
    return;
  }

  openwow::core::SStrCopy(reinterpret_cast<char*>(destination), source,
                          static_cast<std::size_t>(max_length));
}

void InitListHead(ProfileIntrusiveNode* node) {
  node->next = node;
  node->prev = node;
}

void LinkTail(ProfileIntrusiveNode* head, ProfileIntrusiveNode* node) {
  if (!head || !node) {
    return;
  }

  node->prev = head->prev;
  node->next = head;
  head->prev->next = node;
  head->prev = node;
}

void UnlinkNode(ProfileIntrusiveNode* node) {
  if (!node || !node->next || !node->prev) {
    return;
  }

  node->prev->next = node->next;
  node->next->prev = node->prev;
  node->next = nullptr;
  node->prev = nullptr;
}

ProfileHashEntry* ProfileEntryFromPrimaryLinks(ProfileIntrusiveNode* node) {
  if (!node) {
    return nullptr;
  }

  constexpr auto offset = offsetof(ProfileHashEntry, primary_links);
  return reinterpret_cast<ProfileHashEntry*>(
      reinterpret_cast<unsigned char*>(node) - offset);
}

const ProfileHashEntry* ProfileEntryFromPrimaryLinks(
    const ProfileIntrusiveNode* node) {
  if (!node) {
    return nullptr;
  }

  constexpr auto offset = offsetof(ProfileHashEntry, primary_links);
  return reinterpret_cast<const ProfileHashEntry*>(
      reinterpret_cast<const unsigned char*>(node) - offset);
}

ProfileHashEntry* ProfileEntryFromSecondaryLinks(ProfileIntrusiveNode* node) {
  if (!node) {
    return nullptr;
  }

  constexpr auto offset = offsetof(ProfileHashEntry, secondary_links);
  return reinterpret_cast<ProfileHashEntry*>(
      reinterpret_cast<unsigned char*>(node) - offset);
}

void InitProfileBucket(ProfileHashBucket* bucket) {
  if (!bucket) {
    return;
  }

  InitListHead(&bucket->root);
}

void DestroyHashTableStorage(ProfileHashTable* table);

void DetachBucketChain(ProfileHashBucket* bucket) {
  if (!bucket) {
    return;
  }

  ProfileIntrusiveNode* cursor = bucket->root.next;
  while (cursor != &bucket->root) {
    ProfileIntrusiveNode* next = cursor->next;
    UnlinkNode(cursor);
    cursor = next;
  }

  InitProfileBucket(bucket);
}

void SetProfileBucketStorageCount(ProfileHashTable* table,
                                  const std::size_t bucket_count) {
  if (!table) {
    return;
  }

  const std::size_t preserved_count = std::min(table->buckets.size(), bucket_count);
  for (std::size_t index = preserved_count; index < table->buckets.size(); ++index) {
    DetachBucketChain(&table->buckets[index]);
  }

  table->buckets.resize(bucket_count);
}

void ResetProfileBuckets(ProfileHashTable* table) {
  if (!table) {
    return;
  }

  for (auto& bucket : table->buckets) {
    InitProfileBucket(&bucket);
  }
}

void DestroyOwnedProfileString(const char* text) {
  if (!text) {
    return;
  }

  openwow::core::SMemFree(const_cast<char*>(text), kProfileSourceFile, -2, 0);
}

void DestroyProfileKeyValue(ProfileHashEntry* entry) {
  auto* key_value = static_cast<ProfileKeyValue*>(entry);
  if (key_value->values) {
    for (std::uint32_t index = 0; index < key_value->value_count; ++index) {
      DestroyOwnedProfileString(key_value->values[index]);
    }
  }
  DestroyOwnedProfileString(key_value->name);
  if (key_value->values) {
    openwow::core::SMemFree(
        const_cast<void*>(static_cast<const void*>(key_value->values)),
        "pad", -2, 0);
    key_value->values = nullptr;
  }

  UnlinkNode(&key_value->secondary_links);
  UnlinkNode(&key_value->primary_links);
  openwow::core::SMemFree(key_value, kProfileKeyValueTag, -2, 0);
}

void ClearHashTableEntries(ProfileHashTable* table, bool unlink_only) {
  if (!table) {
    return;
  }

  table->entry_count = 0;

  if (unlink_only) {
    ProfileIntrusiveNode* cursor = table->entry_list_root.next;
    while (cursor != &table->entry_list_root) {
      ProfileIntrusiveNode* next = cursor->next;
      UnlinkNode(cursor);
      cursor = next;
    }
    InitListHead(&table->entry_list_root);

    for (auto& bucket : table->buckets) {
      DetachBucketChain(&bucket);
    }
    return;
  }

  while (table->entry_list_root.next != &table->entry_list_root) {
    auto* entry = ProfileEntryFromSecondaryLinks(table->entry_list_root.next);
    if (table->destroy_entry) {
      table->destroy_entry(entry);
    } else {
      UnlinkNode(&entry->secondary_links);
      UnlinkNode(&entry->primary_links);
    }
  }

  for (auto& bucket : table->buckets) {
    InitProfileBucket(&bucket);
  }
  InitListHead(&table->entry_list_root);
}

ProfileHashEntry* GetFirstEntryInBucket(ProfileHashBucket& bucket) {
  if (bucket.root.next == &bucket.root) {
    return nullptr;
  }

  return ProfileEntryFromPrimaryLinks(bucket.root.next);
}

const ProfileHashEntry* GetFirstEntryInBucket(const ProfileHashBucket& bucket) {
  if (bucket.root.next == &bucket.root) {
    return nullptr;
  }

  return ProfileEntryFromPrimaryLinks(bucket.root.next);
}

ProfileHashEntry* GetNextEntryInBucket(ProfileHashBucket& bucket,
                                       ProfileHashEntry* entry) {
  if (!entry) {
    return nullptr;
  }

  ProfileIntrusiveNode* next = entry->primary_links.next;
  if (next == &bucket.root) {
    return nullptr;
  }

  return ProfileEntryFromPrimaryLinks(next);
}

const ProfileHashEntry* GetNextEntryInBucket(const ProfileHashBucket& bucket,
                                             const ProfileHashEntry* entry) {
  if (!entry) {
    return nullptr;
  }

  const ProfileIntrusiveNode* next = entry->primary_links.next;
  if (next == &bucket.root) {
    return nullptr;
  }

  return ProfileEntryFromPrimaryLinks(next);
}

void InsertProfileHashEntryIntoBucket(ProfileHashBucket* bucket,
                                      ProfileHashEntry* entry) {
  if (!bucket || !entry) {
    return;
  }

  UnlinkNode(&entry->primary_links);
  LinkTail(&bucket->root, &entry->primary_links);
}

void LinkProfileHashEntryIntoTable(ProfileHashTable* table,
                                   ProfileHashEntry* entry) {
  if (!table || !entry) {
    return;
  }

  UnlinkNode(&entry->secondary_links);
  LinkTail(&table->entry_list_root, &entry->secondary_links);
}

void DestroyProfileSectionEntry(ProfileHashEntry* entry) {
  ProfileSection_Destroy(static_cast<ProfileSection*>(entry));
}

void DestroyProfileObject(CProfile* profile) {
  if (!profile) {
    return;
  }

  if (!profile->sections.destroy_entry) {
    profile->sections.destroy_entry = &DestroyProfileSectionEntry;
  }

  ClearHashTableEntries(&profile->sections, false);
  DestroyHashTableStorage(&profile->sections);
  UnlinkNode(&profile->string_blocks);
  profile->stringBlockCount = 0;
}

void DestroyHashTableStorage(ProfileHashTable* table) {
  SetProfileBucketStorageCount(table, 0);
  table->bucket_mask = -1;
  InitListHead(&table->entry_list_root);
}

std::uint32_t HashProfileName(const std::string_view name) {
  const std::string storage(name);
  return openwow::core::SStrHashCI(storage.c_str());
}

std::size_t NextProfileBucketCount(const std::size_t current_count) {
  return current_count == 0 ? 4 : current_count * 2;
}

void InitializeProfileBuckets(ProfileHashTable* table,
                              const std::size_t bucket_count) {
  SetProfileBucketStorageCount(table, bucket_count);
  ResetProfileBuckets(table);
  table->bucket_mask = static_cast<std::int32_t>(bucket_count - 1);
}

void RehashProfileHashTable(ProfileHashTable* table,
                            const std::size_t bucket_count) {
  std::vector<ProfileHashEntry*> entries;
  entries.reserve(table->entry_count);
  for (auto& bucket : table->buckets) {
    for (ProfileHashEntry* entry = GetFirstEntryInBucket(bucket); entry;) {
      ProfileHashEntry* next = GetNextEntryInBucket(bucket, entry);
      UnlinkNode(&entry->primary_links);
      entries.push_back(entry);
      entry = next;
    }
    InitProfileBucket(&bucket);
  }

  InitializeProfileBuckets(table, bucket_count);
  for (ProfileHashEntry* entry : entries) {
    const std::size_t bucket_index =
        static_cast<std::uint32_t>(table->bucket_mask) & entry->hash;
    InsertProfileHashEntryIntoBucket(&table->buckets[bucket_index], entry);
  }
}

bool CheckGrowAndRehashProfileHashTable(ProfileHashTable* table,
                                        const std::size_t bucket_index) {
  if (!table || bucket_index >= table->buckets.size() || table->bucket_mask < 0) {
    return false;
  }

  if (static_cast<std::uint32_t>(table->bucket_mask) >= 0x1FFFu) {
    return false;
  }

  if (table->rehash_probe_counter <= 3u) {
    table->rehash_probe_counter = 0;
  } else {
    table->rehash_probe_counter -= 3u;
  }

  ProfileHashBucket& bucket = table->buckets[bucket_index];
  for (ProfileHashEntry* entry = GetFirstEntryInBucket(bucket); entry;
       entry = GetNextEntryInBucket(bucket, entry)) {
    ++table->rehash_probe_counter;
    if (table->rehash_probe_counter > 13u) {
      table->rehash_probe_counter = 0;
      RehashProfileHashTable(table, NextProfileBucketCount(table->buckets.size()));
      return true;
    }
  }

  return false;
}

bool EnsureProfileHashTableBucketStorage(ProfileHashTable* table,
                                         const std::uint32_t incoming_hash) {
  if (table->bucket_mask < 0 || table->buckets.empty()) {
    InitializeProfileBuckets(table, 4);
    return true;
  }

  const std::size_t bucket_index =
      static_cast<std::uint32_t>(table->bucket_mask) & incoming_hash;
  if (bucket_index >= table->buckets.size()) {
    return false;
  }

  CheckGrowAndRehashProfileHashTable(table, bucket_index);

  return true;
}

bool InsertProfileHashEntry(ProfileHashTable* table, ProfileHashEntry* entry) {
  if (!table || !entry) {
    return false;
  }

  if (!EnsureProfileHashTableBucketStorage(table, entry->hash)) {
    return false;
  }

  const std::size_t bucket_index =
      static_cast<std::uint32_t>(table->bucket_mask) & entry->hash;
  if (bucket_index >= table->buckets.size()) {
    return false;
  }

  InsertProfileHashEntryIntoBucket(&table->buckets[bucket_index], entry);
  LinkProfileHashEntryIntoTable(table, entry);
  ++table->entry_count;
  return true;
}

char* AllocateOwnedProfileString(const std::string_view value) {
  auto* storage = static_cast<char*>(
      openwow::core::SMemAlloc(value.size() + 1, kProfileSourceFile, -2, 0x8));
  if (!storage) {
    return nullptr;
  }

  std::memcpy(storage, value.data(), value.size());
  storage[value.size()] = '\0';
  return storage;
}

ProfileSection* CreateProfileSection(const std::string_view name) {

  void* storage =
      openwow::core::SMemAlloc(sizeof(ProfileSection), kProfileSectionTag, -2, 0x8);
  if (!storage) {
    return nullptr;
  }

  auto* section = new (storage) ProfileSection();
  section->hash = HashProfileName(name);
  section->name = AllocateOwnedProfileString(name);
  if (!section->name) {
    openwow::core::SMemFree(section, kProfileSectionTag, -2, 0);
    return nullptr;
  }

  InitListHead(&section->primary_links);
  InitListHead(&section->secondary_links);
  ProfileHashTable_Init(&section->key_values, &DestroyProfileKeyValue);
  return section;
}

ProfileKeyValue* CreateProfileKeyValue(const std::string_view name) {

  void* storage = openwow::core::SMemAlloc(sizeof(ProfileKeyValue),
                                           kProfileKeyValueTag, -2, 0x8);
  if (!storage) {
    return nullptr;
  }

  auto* key_value = new (storage) ProfileKeyValue();
  key_value->hash = HashProfileName(name);
  key_value->name = AllocateOwnedProfileString(name);
  if (!key_value->name) {
    openwow::core::SMemFree(key_value, kProfileKeyValueTag, -2, 0);
    return nullptr;
  }

  InitListHead(&key_value->primary_links);
  InitListHead(&key_value->secondary_links);
  return key_value;
}

bool AppendProfileValue(ProfileKeyValue* key_value,
                        const std::string_view value_text) {
  if (!key_value) {
    return false;
  }

  auto* value = AllocateOwnedProfileString(value_text);
  if (!value) {
    return false;
  }

  const std::size_t new_count =
      static_cast<std::size_t>(key_value->value_count) + 1;
  auto** new_values = static_cast<const char**>(
      openwow::core::SMemAlloc(sizeof(const char*) * new_count,
                               kProfileSourceFile, -2, 0x8));
  if (!new_values) {
    openwow::core::SMemFree(value, kProfileSourceFile, -2, 0);
    return false;
  }

  for (std::size_t index = 0; index < key_value->value_count; ++index) {
    new_values[index] = key_value->values[index];
  }
  new_values[new_count - 1] = value;

  if (key_value->values) {
    openwow::core::SMemFree(
        const_cast<void*>(static_cast<const void*>(key_value->values)),
        "pad", -2, 0);
  }

  key_value->values = new_values;
  key_value->value_count = static_cast<std::uint32_t>(new_count);
  return true;
}

ProfileSection* FindOrCreateProfileSection(CProfile* profile,
                                           const std::string_view name) {
  auto* existing = static_cast<ProfileSection*>(
      TSHashTable_Lookup(&profile->sections, std::string(name).c_str()));
  if (existing) {
    return existing;
  }

  auto* created = CreateProfileSection(name);
  if (!created) {
    return nullptr;
  }

  if (!InsertProfileHashEntry(&profile->sections, created)) {
    ProfileSection_Destroy(created);
    return nullptr;
  }

  return created;
}

ProfileKeyValue* FindOrCreateProfileKeyValue(ProfileSection* section,
                                             const std::string_view name) {
  auto* existing = static_cast<ProfileKeyValue*>(
      TSHashTable_Lookup(&section->key_values, std::string(name).c_str()));
  if (existing) {
    return existing;
  }

  auto* created = CreateProfileKeyValue(name);
  if (!created) {
    return nullptr;
  }

  if (!InsertProfileHashEntry(&section->key_values, created)) {
    DestroyProfileKeyValue(created);
    return nullptr;
  }

  return created;
}

bool LoadParsedProfileIntoRuntime(CProfile* profile,
                                  const ProfileDocument& document) {
  for (const ProfileAssignment& assignment : document.Assignments()) {
    auto* section =
        FindOrCreateProfileSection(profile, assignment.section.Text());
    if (!section) {
      return false;
    }

    auto* key_value =
        FindOrCreateProfileKeyValue(section, assignment.key.Text());
    if (!key_value) {
      return false;
    }

    for (const ProfileValue& value : assignment.values) {
      if (!AppendProfileValue(key_value, value.Text())) {
        return false;
      }
    }
  }

  return true;
}

}

namespace detail {

int LoadRetailProfileDocument(CProfile* profile,
                              const ProfileDocument& document) {
  return LoadParsedProfileIntoRuntime(profile, document) ? 1 : 0;
}

}

CProfileBuffer* CProfileBuffer_Create(std::uint32_t requestedSize) {
  std::size_t data_size = requestedSize;
  if (data_size <= 4) {
    data_size = 4;
  }

  auto* buf = static_cast<CProfileBuffer*>(
      openwow::core::SMemAlloc(sizeof(CProfileBuffer) + data_size,
                               kProfileSourceFile, 0x52, 0));
  if (!buf) {
    return nullptr;
  }

  buf->list_prev = 0;
  buf->list_next = 0;
  buf->reference_count = 0;
  buf->capacity = requestedSize;
  buf->used_bytes = 0;
  return buf;
}

void* TSHashTable_Lookup(ProfileHashTable* hashTable, const char* key) {
  if (!hashTable || !key || hashTable->bucket_mask < 0 ||
      hashTable->buckets.empty()) {
    return nullptr;
  }

  const std::uint32_t hash = openwow::core::SStrHashCI(key);
  ProfileHashBucket& bucket =
      hashTable->buckets[static_cast<std::uint32_t>(hashTable->bucket_mask) & hash];
  ProfileHashEntry* entry = GetFirstEntryInBucket(bucket);

  while (entry) {
    if (entry->hash == hash &&
        (entry->name == key ||
         openwow::core::SStrCmpNoCase(entry->name, key, 0x7FFFFFFFu) == 0)) {
      return entry;
    }

    entry = GetNextEntryInBucket(bucket, entry);
  }

  return nullptr;
}

void ProfileHashTable_Reset(ProfileHashTable* ht) {
  if (!ht) {
    return;
  }

  ht->rehash_probe_counter = 0;
  ClearHashTableEntries(ht, true);
  DestroyHashTableStorage(ht);
}

void ProfileHashTable_Init(ProfileHashTable* ht,
                           ProfileDestroyEntryFn destroy_entry) {
  ht->destroy_entry = destroy_entry;
  ht->entry_list_link_offset = 12;
  ht->rehash_probe_counter = 0;
  ht->entry_count = 0;
  std::vector<ProfileHashBucket>().swap(ht->buckets);
  ht->bucket_mask = -1;
  InitListHead(&ht->entry_list_root);
}

CProfile* CProfile_Create() {
  void* storage = openwow::core::SMemAlloc(sizeof(CProfile), kProfileSourceFile,
                                           0x1FE, 0);
  if (!storage) {
    return nullptr;
  }

  auto* profile = new (storage) CProfile();
  profile->vtable = const_cast<void*>(s_profileVtable);
  ProfileHashTable_Init(&profile->sections, &DestroyProfileSectionEntry);
  profile->stringBlockCount = 0;
  InitListHead(&profile->string_blocks);
  return profile;
}

void CProfile_Destroy(CProfile* profile) {
  if (!profile) {
    return;
  }

  DestroyProfileObject(profile);
  openwow::core::SMemFree(profile, "delete", -1, 0);
}

int CProfile_GetSection(CProfile* profile, const char* section, const char* key,
                        std::uint8_t* valueOut, int valueMaxLen,
                        std::uint32_t valueIndex) {
  if (!section || !key || !valueOut) {
    openwow::core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  *valueOut = 0;

  auto* section_entry =
      static_cast<ProfileSection*>(TSHashTable_Lookup(&profile->sections, section));
  if (!section_entry) {
    return 0;
  }

  auto* key_value = static_cast<ProfileKeyValue*>(
      TSHashTable_Lookup(&section_entry->key_values, key));
  if (!key_value || valueIndex >= key_value->value_count ||
      !key_value->values || !key_value->values[valueIndex]) {
    return 0;
  }

  CopyProfileValueText(valueOut, key_value->values[valueIndex], valueMaxLen);
  return 1;
}

int CProfile_GetValueInt(CProfile* profile, const char* section, const char* key,
                         std::uint32_t* value, std::uint32_t index) {
  if (!section || !key || !value) {
    openwow::core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  *value = 0;

  auto* section_entry =
      static_cast<ProfileSection*>(TSHashTable_Lookup(&profile->sections, section));
  if (!section_entry) {
    return 0;
  }

  auto* key_value = static_cast<ProfileKeyValue*>(
      TSHashTable_Lookup(&section_entry->key_values, key));
  if (!key_value || index >= key_value->value_count || !key_value->values ||
      !key_value->values[index]) {
    return 0;
  }

  *value = ProfileValueView(key_value->values[index]).AsInteger().RawValue();
  return 1;
}

void ProfileSection_Destroy(ProfileSection* section) {
  if (!section) {
    return;
  }

  DestroyOwnedProfileString(section->name);
  if (!section->key_values.destroy_entry) {
    section->key_values.destroy_entry = &DestroyProfileKeyValue;
  }

  ClearHashTableEntries(&section->key_values, false);
  DestroyHashTableStorage(&section->key_values);
  UnlinkNode(&section->secondary_links);
  UnlinkNode(&section->primary_links);
  openwow::core::SMemFree(section, kProfileSectionTag, -2, 0);
}

}
