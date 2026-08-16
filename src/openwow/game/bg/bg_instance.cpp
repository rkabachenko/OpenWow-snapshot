
#include "openwow/game/bg/bg_instance.h"

namespace openwow::game {

BgInstance::BgInstance() = default;

void BgInstance::Enter(BgType type) {

  Reset();
  type_ = type;

  switch (type_) {
    case BgType::kWarsongGulch:
      wsg_ = std::make_unique<BgWarsongGulch>();
      wsg_->Reset();
      break;
    case BgType::kArathiBasin:
      ab_ = std::make_unique<BgArathiBasin>();
      ab_->Reset();
      break;
    case BgType::kAlteracValley:
      av_ = std::make_unique<BgAlteracValley>();
      av_->Reset();
      break;
    case BgType::kEyeOfTheStorm:
      eots_ = std::make_unique<BgEyeOfTheStorm>();
      eots_->Reset();
      break;
    case BgType::kStrandOfAncients:
      sota_ = std::make_unique<BgStrandOfTheAncients>();
      sota_->Reset();
      break;
    case BgType::kIsleOfConquest:
      ioc_ = std::make_unique<BgIsleOfConquest>();
      ioc_->Reset();
      break;
    default:

      break;
  }
}

void BgInstance::Leave() {
  Reset();
}

void BgInstance::OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value) {
  switch (type_) {
    case BgType::kWarsongGulch:
      if (wsg_) wsg_->OnWorldStateUpdate(ws_id, value);
      break;
    case BgType::kArathiBasin:
      if (ab_) ab_->OnWorldStateUpdate(ws_id, value);
      break;
    case BgType::kAlteracValley:
      if (av_) av_->OnWorldStateUpdate(ws_id, value);
      break;
    case BgType::kEyeOfTheStorm:
      if (eots_) eots_->OnWorldStateUpdate(ws_id, value);
      break;
    case BgType::kStrandOfAncients:
      if (sota_) sota_->OnWorldStateUpdate(ws_id, value);
      break;
    case BgType::kIsleOfConquest:
      if (ioc_) ioc_->OnWorldStateUpdate(ws_id, value);
      break;
    default:
      break;
  }
}

void BgInstance::OnInitWorldStates(
    const std::unordered_map<std::int32_t, std::int32_t>& states) {
  for (const auto& [ws_id, value] : states) {
    OnWorldStateUpdate(ws_id, value);
  }
}

void BgInstance::Update(float dt) {
  switch (type_) {
    case BgType::kWarsongGulch:
      if (wsg_) wsg_->Update(dt);
      break;
    case BgType::kArathiBasin:
      if (ab_) ab_->Update(dt);
      break;
    case BgType::kAlteracValley:
      if (av_) av_->Update(dt);
      break;
    case BgType::kEyeOfTheStorm:
      if (eots_) eots_->Update(dt);
      break;
    case BgType::kStrandOfAncients:
      if (sota_) sota_->Update(dt);
      break;
    case BgType::kIsleOfConquest:
      if (ioc_) ioc_->Update(dt);
      break;
    default:
      break;
  }
}

bool BgInstance::IsFinished() const {
  switch (type_) {
    case BgType::kWarsongGulch:
      return wsg_ && wsg_->IsFinished();
    case BgType::kArathiBasin:
      return ab_ && ab_->IsFinished();
    case BgType::kAlteracValley:
      return av_ && av_->IsFinished();
    case BgType::kEyeOfTheStorm:
      return eots_ && eots_->IsFinished();
    case BgType::kStrandOfAncients:
      return false;
    case BgType::kIsleOfConquest:
      return ioc_ && ioc_->IsFinished();
    default:
      return false;
  }
}

BgWarsongGulch* BgInstance::GetWSG() {
  return (type_ == BgType::kWarsongGulch) ? wsg_.get() : nullptr;
}

const BgWarsongGulch* BgInstance::GetWSG() const {
  return (type_ == BgType::kWarsongGulch) ? wsg_.get() : nullptr;
}

BgArathiBasin* BgInstance::GetAB() {
  return (type_ == BgType::kArathiBasin) ? ab_.get() : nullptr;
}

const BgArathiBasin* BgInstance::GetAB() const {
  return (type_ == BgType::kArathiBasin) ? ab_.get() : nullptr;
}

BgAlteracValley* BgInstance::GetAV() {
  return (type_ == BgType::kAlteracValley) ? av_.get() : nullptr;
}

const BgAlteracValley* BgInstance::GetAV() const {
  return (type_ == BgType::kAlteracValley) ? av_.get() : nullptr;
}

BgEyeOfTheStorm* BgInstance::GetEotS() {
  return (type_ == BgType::kEyeOfTheStorm) ? eots_.get() : nullptr;
}

const BgEyeOfTheStorm* BgInstance::GetEotS() const {
  return (type_ == BgType::kEyeOfTheStorm) ? eots_.get() : nullptr;
}

BgStrandOfTheAncients* BgInstance::GetSotA() {
  return (type_ == BgType::kStrandOfAncients) ? sota_.get() : nullptr;
}

const BgStrandOfTheAncients* BgInstance::GetSotA() const {
  return (type_ == BgType::kStrandOfAncients) ? sota_.get() : nullptr;
}

BgIsleOfConquest* BgInstance::GetIoC() {
  return (type_ == BgType::kIsleOfConquest) ? ioc_.get() : nullptr;
}

const BgIsleOfConquest* BgInstance::GetIoC() const {
  return (type_ == BgType::kIsleOfConquest) ? ioc_.get() : nullptr;
}

void BgInstance::Reset() {
  type_ = BgType::kNone;
  wsg_.reset();
  ab_.reset();
  av_.reset();
  eots_.reset();
  sota_.reset();
  ioc_.reset();
}

}
