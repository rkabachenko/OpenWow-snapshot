#pragma once

#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/codecs/audio_decoder.h"
#include "openwow/audio/codecs/mp3/mp3_decoder.h"
#include "openwow/audio/playback/movie_audio_source.h"
#include "openwow/audio/resources/sound_data.h"
#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/adapters/sdl/music_pcm_stream.h"
#include "openwow/runtime/scheduling/jthread_compat.h"
#include "openwow/runtime/scheduling/thread_pool_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>

namespace openwow::audio {

[[nodiscard]] inline float EvaluateFadeScale(const MusicFade& fade) {
  if (!fade.active) {
    return 1.0f;
  }

  const float t = fade.duration_ms > 0.0f
                      ? std::clamp(fade.elapsed_ms / fade.duration_ms, 0.0f, 1.0f)
                      : 1.0f;
  return fade.start_volume + (fade.end_volume - fade.start_volume) * t;
}

inline std::shared_ptr<MusicPcmStream> StopSfxChannel(AudioChannel& channel, MusicFade& fade,
                                                      const float fade_out_ms) {
  if (!channel.active) {
    fade = {};
    auto retired_stream = std::move(channel.pcm_stream);
    if (retired_stream != nullptr) {
      retired_stream->RequestStop();
    }
    return retired_stream;
  }

  if (fade_out_ms > 0.0f) {
    const float start_scale =
        fade.active ? std::clamp(EvaluateFadeScale(fade), 0.0f, 1.0f) : 1.0f;
    if (start_scale > 0.0f) {
      fade.active = true;
      fade.fading_in = false;
      fade.duration_ms = fade_out_ms;
      fade.elapsed_ms = 0.0f;
      fade.start_volume = start_scale;
      fade.end_volume = 0.0f;
      return nullptr;
    }
  }

  auto retired_stream = std::move(channel.pcm_stream);
  if (retired_stream != nullptr) {
    retired_stream->RequestStop();
  }
  channel.active = false;
  channel.data = {};
  channel.position = 0;
  fade = {};
  return retired_stream;
}

inline void BeginMusicFadeIn(MusicFade& fade, const float fade_in_ms) {
  if (fade_in_ms <= 0.0F) {
    fade = {};
    return;
  }

  fade.active = true;
  fade.fading_in = true;
  fade.duration_ms = fade_in_ms;
  fade.elapsed_ms = 0.0F;
  fade.start_volume = 0.0F;
  fade.end_volume = 1.0F;
}

[[nodiscard]] inline bool HasMp3Extension(const std::string_view path) {
  constexpr std::string_view extension = ".mp3";
  if (path.size() < extension.size()) {
    return false;
  }
  const std::string_view suffix = path.substr(path.size() - extension.size());
  return std::equal(suffix.begin(), suffix.end(), extension.begin(), extension.end(),
                    [](const char lhs, const char rhs) {
                      const auto lower = [](const unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                      };
                      return lower(static_cast<unsigned char>(lhs)) ==
                             lower(static_cast<unsigned char>(rhs));
                    });
}

}
