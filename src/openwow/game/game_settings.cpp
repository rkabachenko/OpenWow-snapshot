
#include "openwow/game/game_settings.h"

#include "openwow/game/client_config.h"

#include <algorithm>

namespace openwow::game {

GameSettings& GameSettings::Get() {
  static GameSettings instance;
  return instance;
}

uint32_t GameSettings::GetScreenWidth() const {
  std::lock_guard lock(mutex_);
  return cache_.screen_width;
}

uint32_t GameSettings::GetScreenHeight() const {
  std::lock_guard lock(mutex_);
  return cache_.screen_height;
}

bool GameSettings::IsFullscreen() const {
  std::lock_guard lock(mutex_);
  return cache_.fullscreen;
}

bool GameSettings::IsVSync() const {
  std::lock_guard lock(mutex_);
  return cache_.vsync;
}

uint32_t GameSettings::GetMultiSampling() const {
  std::lock_guard lock(mutex_);
  return cache_.multisampling;
}

float GameSettings::GetGamma() const {
  std::lock_guard lock(mutex_);
  return cache_.gamma;
}

uint32_t GameSettings::GetGroundEffectDensity() const {
  std::lock_guard lock(mutex_);
  return cache_.ground_effect_density;
}

uint32_t GameSettings::GetEnvironmentDetail() const {
  std::lock_guard lock(mutex_);
  return cache_.environment_detail;
}

uint32_t GameSettings::GetViewDistance() const {
  std::lock_guard lock(mutex_);
  return cache_.view_distance;
}

uint32_t GameSettings::GetTextureResolution() const {
  std::lock_guard lock(mutex_);
  return cache_.texture_resolution;
}

uint32_t GameSettings::GetShadowQuality() const {
  std::lock_guard lock(mutex_);
  return cache_.shadow_quality;
}

uint32_t GameSettings::GetLiquidDetail() const {
  std::lock_guard lock(mutex_);
  return cache_.liquid_detail;
}

uint32_t GameSettings::GetSunshafts() const {
  std::lock_guard lock(mutex_);
  return cache_.sunshafts;
}

uint32_t GameSettings::GetParticleDensity() const {
  std::lock_guard lock(mutex_);
  return cache_.particle_density;
}

bool GameSettings::IsProjectedTexturesEnabled() const {
  std::lock_guard lock(mutex_);
  return cache_.projected_textures;
}

bool GameSettings::IsFullScreenGlow() const {
  std::lock_guard lock(mutex_);
  return cache_.full_screen_glow;
}

bool GameSettings::IsWeatherEnabled() const {
  std::lock_guard lock(mutex_);
  return cache_.weather_enabled;
}

float GameSettings::GetMasterVolume() const {
  std::lock_guard lock(mutex_);
  return cache_.master_volume;
}

float GameSettings::GetSFXVolume() const {
  std::lock_guard lock(mutex_);
  return cache_.sfx_volume;
}

float GameSettings::GetMusicVolume() const {
  std::lock_guard lock(mutex_);
  return cache_.music_volume;
}

float GameSettings::GetAmbienceVolume() const {
  std::lock_guard lock(mutex_);
  return cache_.ambience_volume;
}

bool GameSettings::IsSoundEnabled() const {
  std::lock_guard lock(mutex_);
  return cache_.sound_enabled;
}

bool GameSettings::IsMusicEnabled() const {
  std::lock_guard lock(mutex_);
  return cache_.music_enabled;
}

bool GameSettings::IsAutoLoot() const {
  std::lock_guard lock(mutex_);
  return cache_.auto_loot;
}

bool GameSettings::IsAutoSelfCast() const {
  std::lock_guard lock(mutex_);
  return cache_.auto_self_cast;
}

bool GameSettings::IsInstantQuestText() const {
  std::lock_guard lock(mutex_);
  return cache_.instant_quest_text;
}

bool GameSettings::IsShowTargetOfTarget() const {
  std::lock_guard lock(mutex_);
  return cache_.show_target_of_target;
}

uint32_t GameSettings::GetActionButtonUseKeyDown() const {
  std::lock_guard lock(mutex_);
  return cache_.action_button_use_key_down;
}

bool GameSettings::IsLockActionBars() const {
  std::lock_guard lock(mutex_);
  return cache_.lock_action_bars;
}

bool GameSettings::IsSecureAbilityToggle() const {
  std::lock_guard lock(mutex_);
  return cache_.secure_ability_toggle;
}

bool GameSettings::IsShowCastableBuffs() const {
  std::lock_guard lock(mutex_);
  return cache_.show_castable_buffs;
}

bool GameSettings::IsShowDispellableDebuffs() const {
  std::lock_guard lock(mutex_);
  return cache_.show_dispellable_debuffs;
}

float GameSettings::GetNameplateDistance() const {
  std::lock_guard lock(mutex_);
  return cache_.nameplate_distance;
}

bool GameSettings::IsShowEnemyNameplates() const {
  std::lock_guard lock(mutex_);
  return cache_.show_enemy_nameplates;
}

bool GameSettings::IsShowFriendlyNameplates() const {
  std::lock_guard lock(mutex_);
  return cache_.show_friendly_nameplates;
}

bool GameSettings::IsAutoQuestWatch() const {
  std::lock_guard lock(mutex_);
  return cache_.auto_quest_watch;
}

bool GameSettings::IsFloatingCombatText() const {
  std::lock_guard lock(mutex_);
  return cache_.floating_combat_text;
}

bool GameSettings::IsShowSpellAlerts() const {
  std::lock_guard lock(mutex_);
  return cache_.show_spell_alerts;
}

bool GameSettings::IsShowLossOfControl() const {
  std::lock_guard lock(mutex_);
  return cache_.show_loss_of_control;
}

float GameSettings::GetCameraDistance() const {
  std::lock_guard lock(mutex_);
  return cache_.camera_distance;
}

bool GameSettings::IsSmartPivot() const {
  std::lock_guard lock(mutex_);
  return cache_.smart_pivot;
}

bool GameSettings::IsCameraFollowing() const {
  std::lock_guard lock(mutex_);
  return cache_.camera_following;
}

bool GameSettings::IsOptimizeNetwork() const {
  std::lock_guard lock(mutex_);
  return cache_.optimize_network;
}

void GameSettings::SetScreenResolution(uint32_t width, uint32_t height) {
  std::lock_guard lock(mutex_);
  cache_.screen_width = width;
  cache_.screen_height = height;
}

void GameSettings::SetFullscreen(bool enabled) {
  std::lock_guard lock(mutex_);
  cache_.fullscreen = enabled;
}

void GameSettings::SetVSync(bool enabled) {
  std::lock_guard lock(mutex_);
  cache_.vsync = enabled;
}

void GameSettings::SetViewDistance(uint32_t level) {
  std::lock_guard lock(mutex_);
  cache_.view_distance = std::min(level, 9u);
}

void GameSettings::SetMasterVolume(float vol) {
  std::lock_guard lock(mutex_);
  cache_.master_volume = std::clamp(vol, 0.0f, 1.0f);
}

void GameSettings::SetMusicVolume(float vol) {
  std::lock_guard lock(mutex_);
  cache_.music_volume = std::clamp(vol, 0.0f, 1.0f);
}

void GameSettings::SetAutoLoot(bool enabled) {
  std::lock_guard lock(mutex_);
  cache_.auto_loot = enabled;
}

void GameSettings::SetAutoSelfCast(bool enabled) {
  std::lock_guard lock(mutex_);
  cache_.auto_self_cast = enabled;
}

void GameSettings::SetLockActionBars(bool enabled) {
  std::lock_guard lock(mutex_);
  cache_.lock_action_bars = enabled;
}

void GameSettings::SetNameplateDistance(float dist) {
  std::lock_guard lock(mutex_);
  cache_.nameplate_distance = std::clamp(dist, 0.0f, 100.0f);
}

void GameSettings::SetCameraDistance(float dist) {
  std::lock_guard lock(mutex_);
  cache_.camera_distance = std::clamp(dist, 0.0f, 50.0f);
}

void GameSettings::LoadFromCVars() {
  std::lock_guard lock(mutex_);

  auto& cfg = ClientConfig::Get();
  const auto& display = cfg.GetDisplay();
  const auto& sound = cfg.GetSound();
  const auto& net = cfg.GetNetwork();

  cache_.screen_width = display.screen_width;
  cache_.screen_height = display.screen_height;
  cache_.fullscreen = display.fullscreen;
  cache_.vsync = display.vsync;
  cache_.multisampling = display.multisampling;
  cache_.gamma = display.gamma;
  cache_.view_distance = display.view_distance;
  cache_.environment_detail = display.environment_detail;
  cache_.ground_effect_density = display.ground_effects;
  cache_.texture_resolution = display.texture_resolution;
  cache_.shadow_quality = display.shadow_quality;
  cache_.liquid_detail = display.liquid_detail;
  cache_.sunshafts = display.sunshafts;
  cache_.projected_textures = display.projected_textures;
  cache_.full_screen_glow = display.full_screen_glow;
  cache_.weather_enabled = display.weather_effects;

  cache_.master_volume = sound.master_volume;
  cache_.sfx_volume = sound.sfx_volume;
  cache_.music_volume = sound.music_volume;
  cache_.ambience_volume = sound.ambience_volume;
  cache_.sound_enabled = sound.enable_sound;
  cache_.music_enabled = sound.enable_music;

  cache_.optimize_network = net.optimize_network;

  auto parse_bool = [&](const std::string& key, bool def) -> bool {
    std::string v = cfg.Get(key);
    if (v == "1" || v == "true") return true;
    if (v == "0" || v == "false") return false;
    return def;
  };
  auto parse_float = [&](const std::string& key, float def) -> float {
    std::string v = cfg.Get(key);
    if (v.empty()) return def;
    try { return std::stof(v); } catch (...) { return def; }
  };

  cache_.auto_loot = parse_bool("autoLootDefault", cache_.auto_loot);
  cache_.auto_self_cast = parse_bool("autoSelfCast", cache_.auto_self_cast);
  cache_.instant_quest_text = parse_bool("instantQuestText", cache_.instant_quest_text);
  cache_.lock_action_bars = parse_bool("lockActionBars", cache_.lock_action_bars);
  cache_.nameplate_distance = parse_float("nameplateMaxDistance", cache_.nameplate_distance);
  cache_.camera_distance = parse_float("cameraDistanceMax", cache_.camera_distance);
  cache_.floating_combat_text = parse_bool("enableFloatingCombatText", cache_.floating_combat_text);
}

void GameSettings::SaveToCVars() {
  std::lock_guard lock(mutex_);

  auto& cfg = ClientConfig::Get();
  auto& display = cfg.GetDisplay();
  auto& sound = cfg.GetSound();
  auto& net = cfg.GetNetwork();

  display.screen_width = cache_.screen_width;
  display.screen_height = cache_.screen_height;
  display.fullscreen = cache_.fullscreen;
  display.vsync = cache_.vsync;
  display.multisampling = cache_.multisampling;
  display.gamma = cache_.gamma;
  display.view_distance = cache_.view_distance;
  display.environment_detail = cache_.environment_detail;
  display.ground_effects = cache_.ground_effect_density;
  display.texture_resolution = cache_.texture_resolution;
  display.shadow_quality = cache_.shadow_quality;
  display.liquid_detail = cache_.liquid_detail;
  display.sunshafts = cache_.sunshafts;
  display.projected_textures = cache_.projected_textures;
  display.full_screen_glow = cache_.full_screen_glow;
  display.weather_effects = cache_.weather_enabled;

  sound.master_volume = cache_.master_volume;
  sound.sfx_volume = cache_.sfx_volume;
  sound.music_volume = cache_.music_volume;
  sound.ambience_volume = cache_.ambience_volume;
  sound.enable_sound = cache_.sound_enabled;
  sound.enable_music = cache_.music_enabled;

  net.optimize_network = cache_.optimize_network;

  auto sb = [](bool v) -> std::string { return v ? "1" : "0"; };
  auto sf = [](float v) -> std::string {

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
  };

  cfg.Set("autoLootDefault", sb(cache_.auto_loot));
  cfg.Set("autoSelfCast", sb(cache_.auto_self_cast));
  cfg.Set("instantQuestText", sb(cache_.instant_quest_text));
  cfg.Set("lockActionBars", sb(cache_.lock_action_bars));
  cfg.Set("nameplateMaxDistance", sf(cache_.nameplate_distance));
  cfg.Set("cameraDistanceMax", sf(cache_.camera_distance));
  cfg.Set("enableFloatingCombatText", sb(cache_.floating_combat_text));
}

void GameSettings::Reset() {
  std::lock_guard lock(mutex_);
  cache_ = Cache{};
}

}
