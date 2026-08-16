#pragma once

#include <string>
#include <utility>

namespace openwow::ui::glue {

class GlueHost {
 public:
  virtual ~GlueHost() = default;

  virtual void PrepareMoviePlayback() {}

  virtual void PlayGlueMusic(const std::string& track) = 0;
  virtual void StopGlueMusic() = 0;

  virtual void PlayGlueAmbience(const std::string& track, double fade_seconds) = 0;
  virtual void StopGlueAmbience() = 0;

  virtual void PlayCreditsMusic(const std::string& track) = 0;
  virtual void StopCreditsMusic() = 0;

  virtual void OpenUrl(const std::string& url) = 0;

  virtual void SetCursorVisible(bool visible) = 0;

  virtual std::pair<double, double> GetCursorPositionDdc(int viewport_width,
                                                         int viewport_height) const = 0;
  virtual bool IsShiftKeyDown() const = 0;
};

}
