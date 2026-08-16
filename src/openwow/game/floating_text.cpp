
#include "openwow/game/floating_text.h"

#include <algorithm>

namespace openwow::game {

std::uint32_t FloatingTextSystem::AddText(const std::string& text,
                                          FloatingTextType type,
                                          float worldX, float worldY,
                                          float ) {
  if (!enabled_) return 0;

  FloatingTextEntry e;
  e.id = next_id_++;
  e.text = text;
  e.type = type;
  e.x = worldX;
  e.y = worldY;

  bool crit = (type == FloatingTextType::CritDamage ||
               type == FloatingTextType::CritHeal);
  e.isCrit = crit;
  e.scale = crit ? 1.5f : 1.0f;
  e.maxAge = crit ? 2.0f : 1.5f;

  entries_.push_back(std::move(e));
  return entries_.back().id;
}

void FloatingTextSystem::AddDamageText(std::uint32_t amount,
                                       DamageSchool school, bool crit,
                                       float worldX, float worldY,
                                       float ) {
  if (!enabled_) return;

  FloatingTextEntry e;
  e.id = next_id_++;
  e.text = std::to_string(amount);
  e.type = crit ? FloatingTextType::CritDamage : FloatingTextType::Damage;
  e.color = GetSchoolColor(school);
  e.x = worldX;
  e.y = worldY;
  e.isCrit = crit;
  e.scale = crit ? 1.5f : 1.0f;
  e.maxAge = crit ? 2.0f : 1.5f;

  entries_.push_back(std::move(e));
}

void FloatingTextSystem::AddHealText(std::uint32_t amount, bool crit,
                                     float worldX, float worldY,
                                     float ) {
  if (!enabled_) return;

  FloatingTextEntry e;
  e.id = next_id_++;
  e.text = "+" + std::to_string(amount);
  e.type = crit ? FloatingTextType::CritHeal : FloatingTextType::Heal;
  e.color = 0xFF00FF00;
  e.x = worldX;
  e.y = worldY;
  e.isCrit = crit;
  e.scale = crit ? 1.5f : 1.0f;
  e.maxAge = crit ? 2.0f : 1.5f;

  entries_.push_back(std::move(e));
}

void FloatingTextSystem::AddMissText(MissType missType,
                                     float worldX, float worldY,
                                     float ) {
  if (!enabled_) return;

  FloatingTextEntry e;
  e.id = next_id_++;
  e.text = GetMissTypeName(missType);

  switch (missType) {
    case MissType::Miss:    e.type = FloatingTextType::Miss;   break;
    case MissType::Dodge:   e.type = FloatingTextType::Dodge;  break;
    case MissType::Parry:   e.type = FloatingTextType::Parry;  break;
    case MissType::Block:   e.type = FloatingTextType::Block;  break;
    case MissType::Resist:  e.type = FloatingTextType::Resist; break;
    case MissType::Absorb:  e.type = FloatingTextType::Absorb; break;
    case MissType::Immune:  e.type = FloatingTextType::Immune; break;
    default:                e.type = FloatingTextType::Miss;   break;
  }

  e.color = 0xFFFFFFFF;
  e.x = worldX;
  e.y = worldY;

  entries_.push_back(std::move(e));
}

void FloatingTextSystem::Update(float dt) {
  for (auto& e : entries_) {
    e.age += dt;
    e.y += e.velocityY * dt;

    constexpr float kFadeStart = 0.7f;
    constexpr float kFadeEnd = 1.0f;
    if (e.maxAge > 0.0f) {
      const float normalized_age =
          std::clamp(e.age / e.maxAge, 0.0f, 1.0f);
      const float fade = std::clamp(
          (normalized_age - kFadeStart) / (kFadeEnd - kFadeStart),
          0.0f, 1.0f);
      e.alpha = 1.0f - fade;
    }

    if (e.isCrit && e.maxAge > 0.0f) {
      const float t = std::clamp(e.age / e.maxAge, 0.0f, 1.0f);
      e.scale = 1.5f - 0.5f * t;
      if (e.scale < 1.0f) e.scale = 1.0f;
    }
  }

  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [](const FloatingTextEntry& e) {
                       return e.age >= e.maxAge;
                     }),
      entries_.end());
}

std::uint32_t FloatingTextSystem::GetSchoolColor(DamageSchool school) {
  switch (school) {
    case DamageSchool::Physical: return 0xFFFFFFFF;
    case DamageSchool::Holy:     return 0xFFFFCC00;
    case DamageSchool::Fire:     return 0xFFFF4000;
    case DamageSchool::Nature:   return 0xFF40FF40;
    case DamageSchool::Frost:    return 0xFF80C0FF;
    case DamageSchool::Shadow:   return 0xFF8040FF;
    case DamageSchool::Arcane:   return 0xFF4040FF;
    default:                     return 0xFFFFFFFF;
  }
}

const char* FloatingTextSystem::GetMissTypeName(MissType mt) {
  switch (mt) {
    case MissType::Miss:    return "MISS";
    case MissType::Dodge:   return "DODGE";
    case MissType::Parry:   return "PARRY";
    case MissType::Block:   return "BLOCK";
    case MissType::Resist:  return "RESIST";
    case MissType::Absorb:  return "ABSORB";
    case MissType::Deflect: return "DEFLECT";
    case MissType::Immune:  return "IMMUNE";
    case MissType::Evade:   return "EVADE";
    case MissType::Reflect: return "REFLECT";
    default:                return "UNKNOWN";
  }
}

void FloatingTextSystem::Clear() {
  entries_.clear();
}

void FloatingTextSystem::Reset() {
  entries_.clear();
  next_id_ = 1;
  enabled_ = true;
  mode_ = 0;
}

}
