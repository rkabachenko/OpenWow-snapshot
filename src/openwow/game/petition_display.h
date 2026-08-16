#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class PetitionDisplayType : std::uint8_t {
  GuildCharter = 0,
  ArenaTeam2v2 = 1,
  ArenaTeam3v3 = 2,
  ArenaTeam5v5 = 3,
};

struct PetitionDisplaySignature {
  ObjectGuid guid;
  std::string name;
};

struct PetitionDisplayInfo {
  PetitionDisplayType type = PetitionDisplayType::GuildCharter;
  std::uint32_t petitionId = 0;
  std::string charterName;
  ObjectGuid ownerGuid;
  std::string ownerName;
  std::uint32_t cost = 0;
  std::uint32_t requiredSignatures = 0;
  std::vector<PetitionDisplaySignature> signatures;
  bool canSign = false;
  bool isOwner = false;
};

class PetitionDisplay {
 public:

  void SetPetition(const PetitionDisplayInfo& info);

  [[nodiscard]] std::optional<PetitionDisplayInfo> GetPetition() const;

  [[nodiscard]] bool IsOpen() const;

  void Close();

  [[nodiscard]] std::size_t GetSignatureCount() const;

  [[nodiscard]] std::uint32_t GetRequiredSignatures() const;

  [[nodiscard]] bool HasEnoughSignatures() const;

  [[nodiscard]] bool CanSign() const;

  [[nodiscard]] bool HasSigned(ObjectGuid guid) const;

  [[nodiscard]] std::string GetCostText() const;

  [[nodiscard]] std::string GetProgressText() const;

  static std::uint32_t GetRequiredSignaturesForType(PetitionDisplayType type);

  static std::uint32_t GetCostForType(PetitionDisplayType type);

  void Reset();

 private:
  std::optional<PetitionDisplayInfo> petition_;
  bool open_ = false;
};

}
