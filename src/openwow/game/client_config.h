#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openwow::game {

struct DisplaySettings {
  std::uint32_t screen_width       = 1024;
  std::uint32_t screen_height      = 768;
  bool          fullscreen         = false;
  bool          vsync              = true;
  std::uint32_t multisampling      = 0;
  float         gamma              = 1.0f;
  float         render_scale       = 1.0f;
  std::uint32_t view_distance      = 4;
  std::uint32_t environment_detail = 3;
  std::uint32_t ground_effects     = 3;
  std::uint32_t texture_resolution = 2;
  bool          terrain_highlights = true;
  bool          weather_effects    = true;
  bool          death_effects      = true;
  bool          full_screen_glow   = true;
  bool          projected_textures = true;
  bool          particle_density   = true;
  std::uint32_t shadow_quality     = 2;
  std::uint32_t liquid_detail      = 2;
  std::uint32_t sunshafts          = 1;
};

struct SoundSettings {
  float         master_volume      = 1.0f;
  float         sfx_volume         = 1.0f;
  float         music_volume       = 0.4f;
  float         ambience_volume    = 0.6f;
  float         dialog_volume      = 1.0f;
  bool          enable_sound       = true;
  bool          enable_music       = true;
  bool          enable_ambience    = true;
  bool          enable_dialog      = true;
  bool          enable_reverb      = true;
  bool          enable_positional  = true;
  std::uint32_t output_device      = 0;
};

struct NetworkSettings {
  bool          optimize_network   = true;
  bool          use_ipv6           = false;
  std::uint32_t network_smoothing  = 100;
};

class ClientConfig {
 public:
  static ClientConfig& Get();

  bool Load(const std::string& wtf_path);

  bool Save(const std::string& wtf_path) const;

  DisplaySettings&       GetDisplay();
  const DisplaySettings& GetDisplay() const;

  SoundSettings&         GetSound();
  const SoundSettings&   GetSound() const;

  NetworkSettings&       GetNetwork();
  const NetworkSettings& GetNetwork() const;

  void        Set(const std::string& key, const std::string& value);
  std::string Get(const std::string& key, const std::string& default_val = "") const;
  bool        Has(const std::string& key) const;

  void        SetLastRealm(const std::string& realm_name);
  std::string GetLastRealm() const;
  void        SetLastCharacter(const std::string& char_name);
  std::string GetLastCharacter() const;

  void        SetLocale(const std::string& locale);
  std::string GetLocale() const;

  void Reset();

 private:
  ClientConfig();

  void SyncToMap() const;

  void SyncFromMap();

  static bool ParseBool(const std::string& v, bool def);
  static std::uint32_t ParseUInt(const std::string& v, std::uint32_t def);
  static float ParseFloat(const std::string& v, float def);

  DisplaySettings display_;
  SoundSettings   sound_;
  NetworkSettings network_;
  mutable std::unordered_map<std::string, std::string> config_map_;
  std::string     locale_ = "enUS";
  mutable std::mutex mutex_;
};

}
