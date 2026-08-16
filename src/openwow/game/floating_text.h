#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openwow/game/combat_log.h"

namespace openwow::game {

enum class FloatingTextType : std::uint8_t {
  Damage,
  CritDamage,
  Heal,
  CritHeal,
  Miss,
  Dodge,
  Parry,
  Block,
  Resist,
  Absorb,
  Immune,
  Energize,
  Honor,
  Reputation,
  ComboPoint,
  Experience,
};

struct FloatingTextEntry {
  std::uint32_t id{0};
  std::string text;
  FloatingTextType type{FloatingTextType::Damage};
  std::uint32_t color{0xFFFFFFFF};
  float x{0}, y{0};
  float velocityY{-60.0f};
  float scale{1.0f};
  float alpha{1.0f};
  float age{0};
  float maxAge{1.5f};
  bool isCrit{false};
};

class FloatingTextSystem {
 public:

  std::uint32_t AddText(const std::string& text, FloatingTextType type,
                        float worldX, float worldY, float worldZ);

  void AddDamageText(std::uint32_t amount, DamageSchool school, bool crit,
                     float worldX, float worldY, float worldZ);

  void AddHealText(std::uint32_t amount, bool crit,
                   float worldX, float worldY, float worldZ);

  void AddMissText(MissType missType,
                   float worldX, float worldY, float worldZ);

  void Update(float dt);

  [[nodiscard]] const std::vector<FloatingTextEntry>& GetActiveTexts() const {
    return entries_;
  }

  [[nodiscard]] std::uint32_t GetActiveCount() const {
    return static_cast<std::uint32_t>(entries_.size());
  }

  void SetEnabled(bool e) { enabled_ = e; }
  [[nodiscard]] bool IsEnabled() const { return enabled_; }

  void SetMode(std::uint32_t mode) { mode_ = mode; }
  [[nodiscard]] std::uint32_t GetMode() const { return mode_; }

  [[nodiscard]] static std::uint32_t GetSchoolColor(DamageSchool school);

  [[nodiscard]] static const char* GetMissTypeName(MissType mt);

  void Clear();
  void Reset();

 private:
  std::vector<FloatingTextEntry> entries_;
  std::uint32_t next_id_{1};
  bool enabled_{true};
  std::uint32_t mode_{0};
};

}
