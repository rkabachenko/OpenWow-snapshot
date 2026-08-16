#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

enum class DeathDisplayState : std::uint8_t {
  Alive            = 0,
  Dead             = 1,
  Ghost            = 2,
  ReleasePending   = 3,
  ResurrectPending = 4,
};

inline constexpr std::uint8_t kDeathDisplayStateCount = 5;

enum class ResurrectOfferType : std::uint8_t {
  SoulStone        = 0,
  RebirthDruid     = 1,
  SpiritHealer     = 2,
  EngineeringCable = 3,
  GuildPerk        = 4,
  Other            = 5,
};

inline constexpr std::uint8_t kResurrectOfferTypeCount = 6;

struct ResurrectOfferInfo {
  std::uint64_t casterGuid{0};
  std::string casterName;
  ResurrectOfferType type{ResurrectOfferType::Other};
  std::uint8_t healthPercent{0};
  std::uint8_t manaPercent{0};
  bool hasSickness{false};
};

struct GraveyardDisplayInfo {
  std::uint32_t graveyardId{0};
  std::string name;
  std::uint32_t mapId{0};
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float distance{0.0f};
  bool isFaction{false};
};

inline constexpr float kBGAutoReleaseTimeSec = 6.0f;
inline constexpr float kDurabilityLossPerDeath = 0.10f;

class DeathDisplay {
 public:

  void Die();

  void ReleaseSpirit();

  [[nodiscard]] DeathDisplayState GetState() const;

  void SetResurrectOffer(const ResurrectOfferInfo& offer);

  void AcceptResurrect();

  void DeclineResurrect();

  [[nodiscard]] bool HasResurrectOffer() const;

  [[nodiscard]] std::optional<ResurrectOfferInfo> GetResurrectOffer() const;

  void SetNearestGraveyard(const GraveyardDisplayInfo& info);

  [[nodiscard]] std::optional<GraveyardDisplayInfo> GetNearestGraveyard() const;

  [[nodiscard]] float GetTimeSinceDeath() const;

  void Update(float dt);

  [[nodiscard]] bool CanAutoRelease() const;
  void SetInBattleground(bool inBG);

  [[nodiscard]] static std::string GetResSicknessWarning();

  void Revive();

  [[nodiscard]] std::uint32_t GetRepairCostOnRevive() const;
  void SetRepairCost(std::uint32_t copper);

  [[nodiscard]] bool IsGhost() const;

 private:
  DeathDisplayState state_{DeathDisplayState::Alive};
  std::optional<ResurrectOfferInfo> resOffer_;
  std::optional<GraveyardDisplayInfo> nearestGraveyard_;
  float deathTimer_{0.0f};
  bool inBattleground_{false};
  std::uint32_t repairCost_{0};
};

}
