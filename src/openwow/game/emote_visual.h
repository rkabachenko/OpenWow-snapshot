#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class EmoteVisualType : std::uint8_t {
  OneShot = 0,
  Loop = 1,
  State = 2,
};

struct EmoteVisual {
  std::uint32_t id{0};
  ObjectGuid unitGuid;
  std::uint32_t emoteId{0};
  std::uint32_t animationId{0};
  EmoteVisualType visualType{EmoteVisualType::OneShot};
  float startTime{0.0f};
  float duration{0.0f};
  bool looping{false};
};

class EmoteVisualSystem {
 public:
  EmoteVisualSystem() = default;

  std::uint32_t PlayEmote(ObjectGuid unitGuid, std::uint32_t emoteId,
                           EmoteVisualType type, float duration = 0.0f);

  void StopEmote(std::uint32_t id);

  void StopAllEmotes(ObjectGuid unitGuid);

  [[nodiscard]] std::optional<EmoteVisual> GetActiveEmote(
      ObjectGuid unitGuid) const;

  [[nodiscard]] std::vector<EmoteVisual> GetAllActiveEmotes() const;

  [[nodiscard]] std::uint32_t GetActiveCount() const;

  [[nodiscard]] bool IsPlayingEmote(ObjectGuid unitGuid) const;

  [[nodiscard]] std::uint32_t GetEmoteAnimation(std::uint32_t emoteId) const;

  void SetEmoteAnimation(std::uint32_t emoteId, std::uint32_t animId);

  void Update(float dt);

  void Clear();

  void Reset();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, EmoteVisual> visuals_;
  std::unordered_map<std::uint32_t, std::uint32_t> emoteToAnim_;
  std::uint32_t nextId_{1};
  float elapsed_{0.0f};
};

}
