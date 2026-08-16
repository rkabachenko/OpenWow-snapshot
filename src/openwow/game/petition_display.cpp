
#include "openwow/game/petition_display.h"

#include <algorithm>
#include <sstream>

namespace openwow::game {

void PetitionDisplay::SetPetition(const PetitionDisplayInfo& info) {
  petition_ = info;
  open_ = true;
}

std::optional<PetitionDisplayInfo> PetitionDisplay::GetPetition() const {
  return petition_;
}

bool PetitionDisplay::IsOpen() const { return open_; }

void PetitionDisplay::Close() { open_ = false; }

std::size_t PetitionDisplay::GetSignatureCount() const {
  if (!petition_) return 0;
  return petition_->signatures.size();
}

std::uint32_t PetitionDisplay::GetRequiredSignatures() const {
  if (!petition_) return 0;
  return petition_->requiredSignatures;
}

bool PetitionDisplay::HasEnoughSignatures() const {
  if (!petition_) return false;
  return petition_->signatures.size() >= petition_->requiredSignatures;
}

bool PetitionDisplay::CanSign() const {
  if (!petition_) return false;
  return petition_->canSign;
}

bool PetitionDisplay::HasSigned(ObjectGuid guid) const {
  if (!petition_) return false;
  return std::any_of(
      petition_->signatures.begin(), petition_->signatures.end(),
      [&](const PetitionDisplaySignature& sig) {
        return sig.guid.GetRawValue() == guid.GetRawValue();
      });
}

std::string PetitionDisplay::GetCostText() const {
  if (!petition_) return "0c";
  std::uint32_t total = petition_->cost;
  std::uint32_t gold = total / 10000;
  std::uint32_t silver = (total % 10000) / 100;
  std::uint32_t copper = total % 100;
  std::ostringstream oss;
  if (gold > 0) oss << gold << "g ";
  if (silver > 0 || gold > 0) oss << silver << "s ";
  oss << copper << "c";
  return oss.str();
}

std::string PetitionDisplay::GetProgressText() const {
  if (!petition_) return "0/0 Signatures";
  std::ostringstream oss;
  oss << petition_->signatures.size() << "/" << petition_->requiredSignatures
      << " Signatures";
  return oss.str();
}

std::uint32_t PetitionDisplay::GetRequiredSignaturesForType(
    PetitionDisplayType type) {
  switch (type) {
    case PetitionDisplayType::GuildCharter:
      return 9;
    case PetitionDisplayType::ArenaTeam2v2:
      return 2;
    case PetitionDisplayType::ArenaTeam3v3:
      return 3;
    case PetitionDisplayType::ArenaTeam5v5:
      return 4;
  }
  return 0;
}

std::uint32_t PetitionDisplay::GetCostForType(PetitionDisplayType type) {
  switch (type) {
    case PetitionDisplayType::GuildCharter:
      return 100000;
    case PetitionDisplayType::ArenaTeam2v2:
    case PetitionDisplayType::ArenaTeam3v3:
    case PetitionDisplayType::ArenaTeam5v5:
      return 8000;
  }
  return 0;
}

void PetitionDisplay::Reset() {
  petition_.reset();
  open_ = false;
}

}
