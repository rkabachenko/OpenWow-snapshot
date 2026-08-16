
#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::game {

enum class CharServiceType : std::uint8_t {
  NameChange    = 0,
  FactionChange = 1,
  RaceChange    = 2,
  RealmTransfer = 3,
  Customize     = 4,
  Recustomize   = 5,
};

enum class CharServiceState : std::uint8_t {
  None       = 0,
  Pending    = 1,
  InProgress = 2,
  Completed  = 3,
  Failed     = 4,
};

struct CharServiceRequest {
  CharServiceType serviceType{CharServiceType::NameChange};
  ObjectGuid      characterGuid;
  std::string     characterName;
  std::string     newName;
  std::uint32_t   newFaction{0};
  std::uint32_t   newRace{0};
  std::uint32_t   targetRealm{0};
  CharServiceState state{CharServiceState::None};
  std::string     errorMessage;
};

class CharacterServices {
 public:
  CharacterServices() = default;

  bool RequestService(CharServiceType type, ObjectGuid charGuid,
                      const std::string& charName);

  void SetNewName(const std::string& name);

  void SetNewFaction(std::uint32_t faction);

  void SetNewRace(std::uint32_t race);

  void SetTargetRealm(std::uint32_t realm);

  [[nodiscard]] std::optional<CharServiceRequest> GetCurrentRequest() const;

  [[nodiscard]] bool HasPendingRequest() const;

  [[nodiscard]] CharServiceState GetState() const;

  void SetState(CharServiceState state, const std::string& errorMsg = "");

  void CancelRequest();

  [[nodiscard]] bool IsNameChangeAvailable(ObjectGuid guid) const;
  void SetNameChangeAvailable(ObjectGuid guid, bool available);

  [[nodiscard]] bool IsFactionChangeAvailable() const;
  void SetFactionChangeAvailable(bool available);

  [[nodiscard]] bool IsRealmTransferAvailable() const;
  void SetRealmTransferAvailable(bool available);

  [[nodiscard]] static std::string GetServiceName(CharServiceType type);

  void Reset();

 private:
  std::optional<CharServiceRequest> current_request_;
  std::unordered_map<std::uint64_t, bool> name_change_available_;
  bool faction_change_available_{false};
  bool realm_transfer_available_{false};
};

}
