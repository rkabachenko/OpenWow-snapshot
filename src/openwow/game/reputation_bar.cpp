#include "openwow/game/reputation_bar.h"

#include <algorithm>

namespace openwow::game {

static const char* const kStandingNames[] = {
    "Hated",
    "Hostile",
    "Unfriendly",
    "Neutral",
    "Friendly",
    "Honored",
    "Revered",
    "Exalted",
};

static constexpr std::uint32_t kStandingColors[] = {
    0xFFCC2222,
    0xFFFF4400,
    0xFFFF8800,
    0xFFFFFF00,
    0xFF00FF00,
    0xFF00AA00,
    0xFF2266FF,
    0xFF9933CC,
};

void ReputationBarUI::SetWatchedFaction(std::uint32_t factionId,
                                        std::string name,
                                        RepStanding standing,
                                        std::int32_t current,
                                        std::int32_t max) {
  state_.factionId = factionId;
  state_.factionName = std::move(name);
  state_.standing = standing;
  state_.current = current;
  state_.max = (max > 0) ? max : 1;
  state_.isWatched = true;
}

std::uint32_t ReputationBarUI::GetWatchedFactionId() const {
  return state_.factionId;
}

std::string ReputationBarUI::GetWatchedFactionName() const {
  return state_.factionName;
}

RepStanding ReputationBarUI::GetStanding() const { return state_.standing; }

std::string ReputationBarUI::GetStandingName() const {
  auto idx = static_cast<std::size_t>(state_.standing);
  if (idx < std::size(kStandingNames)) return kStandingNames[idx];
  return "Unknown";
}

std::uint32_t ReputationBarUI::GetStandingColor() const {
  auto idx = static_cast<std::size_t>(state_.standing);
  if (idx < std::size(kStandingColors)) return kStandingColors[idx];
  return 0xFFFFFFFF;
}

std::int32_t ReputationBarUI::GetCurrentRep() const { return state_.current; }

std::int32_t ReputationBarUI::GetMaxRep() const { return state_.max; }

float ReputationBarUI::GetProgress() const {
  if (state_.max <= 0) return 0.0f;
  float p = static_cast<float>(state_.current) /
            static_cast<float>(state_.max);
  return std::clamp(p, 0.0f, 1.0f);
}

std::string ReputationBarUI::GetProgressText() const {
  return std::to_string(state_.current) + " / " +
         std::to_string(state_.max);
}

bool ReputationBarUI::HasWatchedFaction() const { return state_.isWatched; }

void ReputationBarUI::ClearWatchedFaction() {
  state_ = RepBarState{};
  recentGain_ = 0;
  recentGainFade_ = 0.0f;
}

void ReputationBarUI::SetAtWar(bool atWar) { state_.isAtWar = atWar; }

bool ReputationBarUI::IsAtWar() const { return state_.isAtWar; }

std::int32_t ReputationBarUI::GetRecentGain() const { return recentGain_; }

void ReputationBarUI::SetRecentGain(std::int32_t amount) {
  recentGain_ = amount;
  recentGainFade_ = 1.0f;
}

float ReputationBarUI::GetRecentGainFade() const { return recentGainFade_; }

void ReputationBarUI::Update(float dt) {
  if (recentGainFade_ > 0.0f) {
    recentGainFade_ -= dt / kFadeDuration;
    if (recentGainFade_ < 0.0f) recentGainFade_ = 0.0f;
  }
}

void ReputationBarUI::Reset() {
  state_ = RepBarState{};
  recentGain_ = 0;
  recentGainFade_ = 0.0f;
}

}
