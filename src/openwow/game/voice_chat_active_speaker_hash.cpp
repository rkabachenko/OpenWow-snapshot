
#include "openwow/game/voice_chat_active_speaker_hash.h"

#include <algorithm>

namespace openwow::game {

static TalkingPlayerHashTable s_talking_player_hash;

static MutedPlayerHashTable s_muted_player_hash;

static std::int32_t s_talking_player_count = 0;

TalkingPlayerEntry* TalkingPlayerHashTable::FindByGuid(
    const std::uint64_t guid) {
  const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);
  if (!map_.initialized()) {
    return nullptr;
  }
  for (auto& entry : entries_) {
    if (entry.GetKey() == key && entry.MatchesGuid(guid)) {
      return &entry;
    }
  }
  return nullptr;
}

const TalkingPlayerEntry* TalkingPlayerHashTable::FindByGuid(
    const std::uint64_t guid) const {
  const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);
  if (!map_.initialized()) {
    return nullptr;
  }
  for (const auto& entry : entries_) {
    if (entry.GetKey() == key && entry.MatchesGuid(guid)) {
      return &entry;
    }
  }
  return nullptr;
}

TalkingPlayerEntry* TalkingPlayerHashTable::FindOrInsert(
    const std::uint64_t guid) {
  if (auto* existing = FindByGuid(guid)) {
    return existing;
  }

  if (!map_.initialized()) {
    map_.InitWithBuckets();
  }

  const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);

  (void)map_.InsertHashedKey(key);

  entries_.emplace_back();
  auto& entry = entries_.back();
  entry.guid = guid;
  return &entry;
}

bool TalkingPlayerHashTable::RemoveByGuid(const std::uint64_t guid) {
  const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);
  if (!map_.initialized()) {
    return false;
  }

  auto it = std::find_if(
      entries_.begin(), entries_.end(),
      [key, guid](const TalkingPlayerEntry& e) {
        return e.GetKey() == key && e.MatchesGuid(guid);
      });

  if (it == entries_.end()) {
    return false;
  }

  (void)map_.EraseHashedKey(key);
  entries_.erase(it);
  return true;
}

MutedPlayerEntry* MutedPlayerHashTable::FindByGuid(
    const std::uint64_t guid) {
  const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);
  if (!map_.initialized()) {
    return nullptr;
  }
  for (auto& entry : entries_) {
    if (entry.GetKey() == key && entry.MatchesGuid(guid)) {
      return &entry;
    }
  }
  return nullptr;
}

const MutedPlayerEntry* MutedPlayerHashTable::FindByGuid(
    const std::uint64_t guid) const {
  const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);
  if (!map_.initialized()) {
    return nullptr;
  }
  for (const auto& entry : entries_) {
    if (entry.GetKey() == key && entry.MatchesGuid(guid)) {
      return &entry;
    }
  }
  return nullptr;
}

void MutedPlayerHashTable::SetMuted(const std::uint64_t guid,
                                    const bool muted) {
  auto* existing = FindByGuid(guid);

  if (muted && !existing) {

    if (!map_.initialized()) {
      map_.InitWithBuckets();
    }

    const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);

    (void)map_.InsertHashedKey(key);
    entries_.emplace_back();
    entries_.back().guid = guid;
  } else if (!muted && existing) {

    const auto key = static_cast<std::uint32_t>(guid & 0xFFFFFFFF);

    (void)map_.EraseHashedKey(key);

    auto it = std::find_if(
        entries_.begin(), entries_.end(),
        [key, guid](const MutedPlayerEntry& e) {
          return e.GetKey() == key && e.MatchesGuid(guid);
        });
    if (it != entries_.end()) {
      entries_.erase(it);
    }
  }
}

TalkingPlayerHashTable& GetTalkingPlayerHash() {
  return s_talking_player_hash;
}

MutedPlayerHashTable& GetMutedPlayerHash() {
  return s_muted_player_hash;
}

bool VoiceChat_IsTalking(const std::uint64_t guid) {
  return s_talking_player_hash.FindByGuid(guid) != nullptr;
}

bool VoiceChat_IsMuted(const std::uint64_t guid) {
  return s_muted_player_hash.FindByGuid(guid) != nullptr;
}

bool VoiceChat_RemoveTalkingPlayer(const std::uint64_t guid) {
  return s_talking_player_hash.RemoveByGuid(guid);
}

void VoiceChat_AddTalkingPlayer(const std::uint64_t guid) {
  const auto* existing = s_talking_player_hash.FindByGuid(guid);
  if (!existing) {
    s_talking_player_hash.FindOrInsert(guid);
  }
}

void VoiceChat_SetMuted(const std::uint64_t guid, const bool muted) {
  s_muted_player_hash.SetMuted(guid, muted);
}

std::int32_t GetTalkingPlayerCount() {
  return s_talking_player_count;
}

}
