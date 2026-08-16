#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace openwow::game {

class WorldSession;

struct AuraQueryResult {
  std::string name;
  std::string rank;
  std::string icon;
  std::uint32_t count{0};
  std::string debuffType;
  float duration{0.0f};
  double expirationTime{0.0};
  float remainingTime{0.0f};
  std::string caster;
  ObjectGuid casterGuid;
  bool canStealOrPurge{false};
  bool shouldConsolidate{false};
  std::uint32_t spellId{0};
};

struct WeaponEnchantResult {
  bool hasMainHand{false};
  float mainHandExpiration{0.0f};
  int mainHandCharges{0};
  bool hasOffHand{false};
  float offHandExpiration{0.0f};
  int offHandCharges{0};
};

class AuraLuaBridge {
 public:
  static AuraLuaBridge& Get();

  [[nodiscard]] std::optional<AuraQueryResult> GetUnitBuff(
      const WorldSession& session, const ObjectGuid& unitGuid,
      std::uint32_t index) const;

  [[nodiscard]] std::optional<AuraQueryResult> GetUnitDebuff(
      const WorldSession& session, const ObjectGuid& unitGuid,
      std::uint32_t index) const;

  [[nodiscard]] std::optional<AuraQueryResult> GetUnitAura(
      const WorldSession& session, const ObjectGuid& unitGuid,
      std::uint32_t index,
      const std::string& filter) const;

  [[nodiscard]] std::optional<AuraQueryResult> FindUnitAura(
      const WorldSession& session, const ObjectGuid& unitGuid,
      const std::string& name,
      const std::string& rank, const std::string& filter) const;

  [[nodiscard]] std::optional<AuraQueryResult> UnitBuff(
      const WorldSession& session, const ObjectGuid& unitGuid,
      std::uint32_t index) const;

  [[nodiscard]] std::optional<AuraQueryResult> UnitDebuff(
      const WorldSession& session, const ObjectGuid& unitGuid,
      std::uint32_t index) const;

  void CancelUnitBuff(const WorldSession& session,
                      const ObjectGuid& unitGuid,
                      std::uint32_t index);

  [[nodiscard]] WeaponEnchantResult GetWeaponEnchantInfo() const;

  void SetMainHandEnchant(float expiration, int charges);
  void SetOffHandEnchant(float expiration, int charges);
  void ClearWeaponEnchants();

 private:
  AuraLuaBridge() = default;

  bool has_mh_enchant_{false};
  float mh_expiration_{0.0f};
  int mh_charges_{0};
  bool has_oh_enchant_{false};
  float oh_expiration_{0.0f};
  int oh_charges_{0};
};

}
