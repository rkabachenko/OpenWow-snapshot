#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace openwow::game {

class GameSettings {
 public:
  static GameSettings& Get();

  uint32_t GetScreenWidth() const;
  uint32_t GetScreenHeight() const;
  bool IsFullscreen() const;
  bool IsVSync() const;
  uint32_t GetMultiSampling() const;
  float GetGamma() const;

  uint32_t GetGroundEffectDensity() const;
  uint32_t GetEnvironmentDetail() const;
  uint32_t GetViewDistance() const;
  uint32_t GetTextureResolution() const;
  uint32_t GetShadowQuality() const;
  uint32_t GetLiquidDetail() const;
  uint32_t GetSunshafts() const;
  uint32_t GetParticleDensity() const;
  bool IsProjectedTexturesEnabled() const;
  bool IsFullScreenGlow() const;
  bool IsWeatherEnabled() const;

  float GetMasterVolume() const;
  float GetSFXVolume() const;
  float GetMusicVolume() const;
  float GetAmbienceVolume() const;
  bool IsSoundEnabled() const;
  bool IsMusicEnabled() const;

  bool IsAutoLoot() const;
  bool IsAutoSelfCast() const;
  bool IsInstantQuestText() const;
  bool IsShowTargetOfTarget() const;
  uint32_t GetActionButtonUseKeyDown() const;
  bool IsLockActionBars() const;
  bool IsSecureAbilityToggle() const;
  bool IsShowCastableBuffs() const;
  bool IsShowDispellableDebuffs() const;
  float GetNameplateDistance() const;
  bool IsShowEnemyNameplates() const;
  bool IsShowFriendlyNameplates() const;
  bool IsAutoQuestWatch() const;

  bool IsFloatingCombatText() const;
  bool IsShowSpellAlerts() const;
  bool IsShowLossOfControl() const;

  float GetCameraDistance() const;
  bool IsSmartPivot() const;
  bool IsCameraFollowing() const;

  bool IsOptimizeNetwork() const;

  void SetScreenResolution(uint32_t width, uint32_t height);
  void SetFullscreen(bool enabled);
  void SetVSync(bool enabled);
  void SetViewDistance(uint32_t level);
  void SetMasterVolume(float vol);
  void SetMusicVolume(float vol);
  void SetAutoLoot(bool enabled);
  void SetAutoSelfCast(bool enabled);
  void SetLockActionBars(bool enabled);
  void SetNameplateDistance(float dist);
  void SetCameraDistance(float dist);

  void LoadFromCVars();
  void SaveToCVars();

  void Reset();

 private:
  GameSettings() = default;

  struct Cache {
    uint32_t screen_width = 1024;
    uint32_t screen_height = 768;
    bool fullscreen = false;
    bool vsync = true;
    uint32_t multisampling = 0;
    float gamma = 1.0f;

    uint32_t ground_effect_density = 3;
    uint32_t environment_detail = 3;
    uint32_t view_distance = 4;
    uint32_t texture_resolution = 2;
    uint32_t shadow_quality = 2;
    uint32_t liquid_detail = 2;
    uint32_t sunshafts = 1;
    uint32_t particle_density = 3;
    bool projected_textures = true;
    bool full_screen_glow = true;
    bool weather_enabled = true;

    float master_volume = 1.0f;
    float sfx_volume = 1.0f;
    float music_volume = 0.4f;
    float ambience_volume = 0.6f;
    bool sound_enabled = true;
    bool music_enabled = true;

    bool auto_loot = false;
    bool auto_self_cast = false;
    bool instant_quest_text = false;
    bool show_target_of_target = true;
    uint32_t action_button_use_key_down = 1;
    bool lock_action_bars = false;
    bool secure_ability_toggle = true;
    bool show_castable_buffs = false;
    bool show_dispellable_debuffs = false;
    float nameplate_distance = 60.0f;
    bool show_enemy_nameplates = true;
    bool show_friendly_nameplates = false;
    bool auto_quest_watch = true;

    bool floating_combat_text = true;
    bool show_spell_alerts = true;
    bool show_loss_of_control = true;

    float camera_distance = 15.0f;
    bool smart_pivot = false;
    bool camera_following = true;

    bool optimize_network = true;
  };

  Cache cache_;
  mutable std::mutex mutex_;
};

}
