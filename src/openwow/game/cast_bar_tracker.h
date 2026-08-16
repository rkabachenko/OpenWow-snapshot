#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openwow::game {

struct CastBarInfo {
  std::uint32_t spell_id = 0;
  std::string spell_name;
  std::string icon_texture;

  bool is_casting = false;
  bool is_channeling = false;
  bool is_interruptible = true;

  std::uint32_t start_time = 0;
  std::uint32_t end_time = 0;
  std::uint32_t duration = 0;

  [[nodiscard]] float GetProgress(std::uint32_t current_time) const;
  [[nodiscard]] float GetRemainingSeconds(std::uint32_t current_time) const;
  [[nodiscard]] bool IsComplete(std::uint32_t current_time) const;

  enum class Result : std::uint8_t {
    kNone = 0,
    kSuccess,
    kInterrupted,
    kFailed,
  };
  Result result = Result::kNone;

  ObjectGuid target_guid;
  std::string target_name;
};

class CastBarTracker {
 public:
  static CastBarTracker& Get();

  void StartCast(const ObjectGuid& caster, std::uint32_t spell_id,
                 const std::string& name, std::uint32_t cast_time,
                 std::uint32_t start_time);

  void StartChannel(const ObjectGuid& caster, std::uint32_t spell_id,
                    const std::string& name, std::uint32_t duration,
                    std::uint32_t start_time);

  void InterruptCast(const ObjectGuid& caster);
  void FinishCast(const ObjectGuid& caster);
  void FailCast(const ObjectGuid& caster);

  [[nodiscard]] const CastBarInfo& GetCastInfo(
      const ObjectGuid& caster) const;
  [[nodiscard]] bool IsCasting(const ObjectGuid& caster) const;
  [[nodiscard]] bool IsChanneling(const ObjectGuid& caster) const;

  [[nodiscard]] const CastBarInfo& GetPlayerCast() const;
  [[nodiscard]] const CastBarInfo& GetTargetCast() const;
  void SetPlayerGuid(const ObjectGuid& guid);
  void SetTargetGuid(const ObjectGuid& guid);

  void Update(std::uint32_t current_time);

  void Reset();

 private:
  CastBarTracker() = default;

  std::unordered_map<std::uint64_t, CastBarInfo> casts_;
  ObjectGuid player_guid_;
  ObjectGuid target_guid_;
  CastBarInfo empty_info_;
  mutable std::mutex mutex_;
};

}
