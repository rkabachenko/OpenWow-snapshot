#pragma once

#include <cstdint>
#include <optional>

#include "openwow/core/storm_hash_table.h"

namespace openwow::game {

struct TalkingPlayerEntry {
  using KeyType = std::uint32_t;

  std::uint64_t guid{0};

  [[nodiscard]] KeyType GetKey() const {
    return static_cast<KeyType>(guid & 0xFFFFFFFF);
  }

  [[nodiscard]] bool MatchesGuid(std::uint64_t other) const {
    return guid == other;
  }
};

struct MutedPlayerEntry {
  using KeyType = std::uint32_t;

  std::uint64_t guid{0};

  [[nodiscard]] KeyType GetKey() const {
    return static_cast<KeyType>(guid & 0xFFFFFFFF);
  }

  [[nodiscard]] bool MatchesGuid(std::uint64_t other) const {
    return guid == other;
  }
};

class TalkingPlayerHashTable
    : public openwow::core::TSHashTable<TalkingPlayerEntry> {
 public:

  TalkingPlayerHashTable() = default;
  ~TalkingPlayerHashTable() override = default;

  [[nodiscard]] TalkingPlayerEntry* FindByGuid(std::uint64_t guid);
  [[nodiscard]] const TalkingPlayerEntry* FindByGuid(std::uint64_t guid) const;

  TalkingPlayerEntry* FindOrInsert(std::uint64_t guid);

  bool RemoveByGuid(std::uint64_t guid);
};

class MutedPlayerHashTable
    : public openwow::core::TSHashTable<MutedPlayerEntry> {
 public:
  MutedPlayerHashTable() = default;
  ~MutedPlayerHashTable() override = default;

  [[nodiscard]] MutedPlayerEntry* FindByGuid(std::uint64_t guid);
  [[nodiscard]] const MutedPlayerEntry* FindByGuid(std::uint64_t guid) const;

  void SetMuted(std::uint64_t guid, bool muted);
};

TalkingPlayerHashTable& GetTalkingPlayerHash();

MutedPlayerHashTable& GetMutedPlayerHash();

[[nodiscard]] bool VoiceChat_IsTalking(std::uint64_t guid);

[[nodiscard]] bool VoiceChat_IsMuted(std::uint64_t guid);

bool VoiceChat_RemoveTalkingPlayer(std::uint64_t guid);

void VoiceChat_AddTalkingPlayer(std::uint64_t guid);

void VoiceChat_SetMuted(std::uint64_t guid, bool muted);

[[nodiscard]] std::int32_t GetTalkingPlayerCount();

}
