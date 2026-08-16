#include "openwow/game/client_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace openwow::game {

ClientConfig& ClientConfig::Get() {
  static ClientConfig instance;
  return instance;
}

ClientConfig::ClientConfig() { Reset(); }

bool ClientConfig::Load(const std::string& wtf_path) {
  std::lock_guard lock(mutex_);

  std::ifstream in(wtf_path);
  if (!in.is_open()) return false;

  config_map_.clear();

  std::string line;
  while (std::getline(in, line)) {

    auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);

    if (line.empty() || line[0] == '#' || line[0] == '-') continue;

    if (line.size() < 5) continue;

    std::string prefix = line.substr(0, 4);
    for (auto& c : prefix) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (prefix != "SET ") continue;

    std::string rest = line.substr(4);
    start = rest.find_first_not_of(" \t");
    if (start == std::string::npos) continue;
    rest = rest.substr(start);

    auto key_end = rest.find_first_of(" \t");
    if (key_end == std::string::npos) continue;

    std::string key = rest.substr(0, key_end);

    std::string val_raw = rest.substr(key_end);
    start = val_raw.find_first_not_of(" \t");
    if (start == std::string::npos) continue;
    val_raw = val_raw.substr(start);

    std::string value;
    if (val_raw.size() >= 2 && val_raw.front() == '"' && val_raw.back() == '"') {
      value = val_raw.substr(1, val_raw.size() - 2);
    } else {
      value = val_raw;
    }

    config_map_[key] = value;
  }

  SyncFromMap();
  return true;
}

bool ClientConfig::Save(const std::string& wtf_path) const {
  std::lock_guard lock(mutex_);

  SyncToMap();

  std::ofstream out(wtf_path);
  if (!out.is_open()) return false;

  for (const auto& [key, val] : config_map_) {
    out << "SET " << key << " \"" << val << "\"\n";
  }

  return out.good();
}

DisplaySettings& ClientConfig::GetDisplay() { return display_; }
const DisplaySettings& ClientConfig::GetDisplay() const { return display_; }

SoundSettings& ClientConfig::GetSound() { return sound_; }
const SoundSettings& ClientConfig::GetSound() const { return sound_; }

NetworkSettings& ClientConfig::GetNetwork() { return network_; }
const NetworkSettings& ClientConfig::GetNetwork() const { return network_; }

void ClientConfig::Set(const std::string& key, const std::string& value) {
  std::lock_guard lock(mutex_);
  config_map_[key] = value;
}

std::string ClientConfig::Get(const std::string& key,
                              const std::string& default_val) const {
  std::lock_guard lock(mutex_);
  auto it = config_map_.find(key);
  return (it != config_map_.end()) ? it->second : default_val;
}

bool ClientConfig::Has(const std::string& key) const {
  std::lock_guard lock(mutex_);
  return config_map_.find(key) != config_map_.end();
}

void ClientConfig::SetLastRealm(const std::string& realm) {
  Set("realmName", realm);
}

std::string ClientConfig::GetLastRealm() const {
  return Get("realmName");
}

void ClientConfig::SetLastCharacter(const std::string& name) {
  Set("lastCharacterIndex", name);
}

std::string ClientConfig::GetLastCharacter() const {
  return Get("lastCharacterIndex");
}

void ClientConfig::SetLocale(const std::string& locale) {
  std::lock_guard lock(mutex_);
  locale_ = locale;
  config_map_["locale"] = locale;
}

std::string ClientConfig::GetLocale() const {
  std::lock_guard lock(mutex_);
  return locale_;
}

void ClientConfig::Reset() {
  std::lock_guard lock(mutex_);
  display_ = DisplaySettings{};
  sound_   = SoundSettings{};
  network_ = NetworkSettings{};
  config_map_.clear();
  locale_ = "enUS";
}

bool ClientConfig::ParseBool(const std::string& v, bool def) {
  if (v == "1" || v == "true" || v == "yes") return true;
  if (v == "0" || v == "false" || v == "no") return false;
  return def;
}

std::uint32_t ClientConfig::ParseUInt(const std::string& v, std::uint32_t def) {
  if (v.empty()) return def;
  try {
    return static_cast<std::uint32_t>(std::stoul(v));
  } catch (...) {
    return def;
  }
}

float ClientConfig::ParseFloat(const std::string& v, float def) {
  if (v.empty()) return def;
  try {
    return std::stof(v);
  } catch (...) {
    return def;
  }
}

void ClientConfig::SyncToMap() const {
  auto s = [](std::uint32_t v) { return std::to_string(v); };
  auto sf = [](float v) {
    std::ostringstream os;
    os << v;
    return os.str();
  };
  auto sb = [](bool v) -> std::string { return v ? "1" : "0"; };

  config_map_["gxResolution"]         = s(display_.screen_width) + "x" + s(display_.screen_height);
  config_map_["gxWindow"]             = sb(!display_.fullscreen);
  config_map_["gxVSync"]              = sb(display_.vsync);
  config_map_["gxMultisample"]        = s(display_.multisampling);
  config_map_["gamma"]                = sf(display_.gamma);
  config_map_["renderScale"]          = sf(display_.render_scale);
  config_map_["farclip"]              = s(display_.view_distance);
  config_map_["environmentDetail"]    = s(display_.environment_detail);
  config_map_["groundEffectDensity"]  = s(display_.ground_effects);
  config_map_["textureFilteringMode"] = s(display_.texture_resolution);
  config_map_["terrainHighlights"]    = sb(display_.terrain_highlights);
  config_map_["weatherDensity"]       = sb(display_.weather_effects);
  config_map_["ffxDeath"]             = sb(display_.death_effects);
  config_map_["ffxGlow"]              = sb(display_.full_screen_glow);
  config_map_["projectedTextures"]    = sb(display_.projected_textures);
  config_map_["particleDensity"]      = sb(display_.particle_density);
  config_map_["shadowMode"]           = s(display_.shadow_quality);
  config_map_["liquidDetail"]         = s(display_.liquid_detail);
  config_map_["sunshafts"]            = s(display_.sunshafts);

  config_map_["MasterVolume"]       = sf(sound_.master_volume);
  config_map_["SoundVolume"]        = sf(sound_.sfx_volume);
  config_map_["MusicVolume"]        = sf(sound_.music_volume);
  config_map_["AmbienceVolume"]     = sf(sound_.ambience_volume);
  config_map_["DialogVolume"]       = sf(sound_.dialog_volume);
  config_map_["EnableSound"]        = sb(sound_.enable_sound);
  config_map_["EnableMusic"]        = sb(sound_.enable_music);
  config_map_["EnableAmbience"]     = sb(sound_.enable_ambience);
  config_map_["EnableDialog"]       = sb(sound_.enable_dialog);
  config_map_["EnableReverb"]       = sb(sound_.enable_reverb);
  config_map_["EnablePositional"]   = sb(sound_.enable_positional);
  config_map_["SoundOutputDevice"]  = s(sound_.output_device);

  config_map_["optimizeNetwork"]    = sb(network_.optimize_network);
  config_map_["useIPv6"]            = sb(network_.use_ipv6);
  config_map_["networkSmoothing"]   = s(network_.network_smoothing);

  config_map_["locale"]             = locale_;
}

void ClientConfig::SyncFromMap() {
  auto g = [&](const std::string& k) -> std::string {
    auto it = config_map_.find(k);
    return (it != config_map_.end()) ? it->second : "";
  };

  {
    const auto res = g("gxResolution");
    auto x = res.find('x');
    if (x == std::string::npos) x = res.find('X');
    if (x != std::string::npos) {
      display_.screen_width  = ParseUInt(res.substr(0, x), display_.screen_width);
      display_.screen_height = ParseUInt(res.substr(x + 1), display_.screen_height);
    }
  }
  { auto v = g("gxWindow");           if (!v.empty()) display_.fullscreen = !ParseBool(v, !display_.fullscreen); }
  { auto v = g("gxVSync");            if (!v.empty()) display_.vsync = ParseBool(v, display_.vsync); }
  { auto v = g("gxMultisample");      if (!v.empty()) display_.multisampling = ParseUInt(v, display_.multisampling); }
  { auto v = g("gamma");              if (!v.empty()) display_.gamma = ParseFloat(v, display_.gamma); }
  { auto v = g("renderScale");        if (!v.empty()) display_.render_scale = ParseFloat(v, display_.render_scale); }
  { auto v = g("farclip");            if (!v.empty()) display_.view_distance = ParseUInt(v, display_.view_distance); }
  { auto v = g("environmentDetail");  if (!v.empty()) display_.environment_detail = ParseUInt(v, display_.environment_detail); }
  { auto v = g("groundEffectDensity");if (!v.empty()) display_.ground_effects = ParseUInt(v, display_.ground_effects); }
  { auto v = g("textureFilteringMode");if (!v.empty()) display_.texture_resolution = ParseUInt(v, display_.texture_resolution); }
  { auto v = g("terrainHighlights");  if (!v.empty()) display_.terrain_highlights = ParseBool(v, display_.terrain_highlights); }
  { auto v = g("weatherDensity");     if (!v.empty()) display_.weather_effects = ParseBool(v, display_.weather_effects); }
  { auto v = g("ffxDeath");           if (!v.empty()) display_.death_effects = ParseBool(v, display_.death_effects); }
  { auto v = g("ffxGlow");            if (!v.empty()) display_.full_screen_glow = ParseBool(v, display_.full_screen_glow); }
  { auto v = g("projectedTextures");  if (!v.empty()) display_.projected_textures = ParseBool(v, display_.projected_textures); }
  { auto v = g("particleDensity");    if (!v.empty()) display_.particle_density = ParseBool(v, display_.particle_density); }
  { auto v = g("shadowMode");         if (!v.empty()) display_.shadow_quality = ParseUInt(v, display_.shadow_quality); }
  { auto v = g("liquidDetail");       if (!v.empty()) display_.liquid_detail = ParseUInt(v, display_.liquid_detail); }
  { auto v = g("sunshafts");          if (!v.empty()) display_.sunshafts = ParseUInt(v, display_.sunshafts); }

  { auto v = g("MasterVolume");      if (!v.empty()) sound_.master_volume = ParseFloat(v, sound_.master_volume); }
  { auto v = g("SoundVolume");       if (!v.empty()) sound_.sfx_volume = ParseFloat(v, sound_.sfx_volume); }
  { auto v = g("MusicVolume");       if (!v.empty()) sound_.music_volume = ParseFloat(v, sound_.music_volume); }
  { auto v = g("AmbienceVolume");    if (!v.empty()) sound_.ambience_volume = ParseFloat(v, sound_.ambience_volume); }
  { auto v = g("DialogVolume");      if (!v.empty()) sound_.dialog_volume = ParseFloat(v, sound_.dialog_volume); }
  { auto v = g("EnableSound");       if (!v.empty()) sound_.enable_sound = ParseBool(v, sound_.enable_sound); }
  { auto v = g("EnableMusic");       if (!v.empty()) sound_.enable_music = ParseBool(v, sound_.enable_music); }
  { auto v = g("EnableAmbience");    if (!v.empty()) sound_.enable_ambience = ParseBool(v, sound_.enable_ambience); }
  { auto v = g("EnableDialog");      if (!v.empty()) sound_.enable_dialog = ParseBool(v, sound_.enable_dialog); }
  { auto v = g("EnableReverb");      if (!v.empty()) sound_.enable_reverb = ParseBool(v, sound_.enable_reverb); }
  { auto v = g("EnablePositional");  if (!v.empty()) sound_.enable_positional = ParseBool(v, sound_.enable_positional); }
  { auto v = g("SoundOutputDevice"); if (!v.empty()) sound_.output_device = ParseUInt(v, sound_.output_device); }

  { auto v = g("optimizeNetwork");   if (!v.empty()) network_.optimize_network = ParseBool(v, network_.optimize_network); }
  { auto v = g("useIPv6");           if (!v.empty()) network_.use_ipv6 = ParseBool(v, network_.use_ipv6); }
  { auto v = g("networkSmoothing");  if (!v.empty()) network_.network_smoothing = ParseUInt(v, network_.network_smoothing); }

  { auto v = g("locale"); if (!v.empty()) locale_ = v; }
}

}
