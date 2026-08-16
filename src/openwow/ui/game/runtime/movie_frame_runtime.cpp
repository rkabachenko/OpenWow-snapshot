#include "openwow/ui/game/runtime/movie_frame_runtime.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/media/playback/movie_lifecycle.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

#include <optional>
#include <utility>

namespace openwow::ui::game::runtime {

MovieFrameRuntime::MovieFrameRuntime(Dependencies dependencies)
    : dependencies_(dependencies) {}

MovieFrameRuntime::~MovieFrameRuntime() { Shutdown(); }

bool MovieFrameRuntime::Start(const std::string& frame_name,
                               const std::string& avi_path, const int volume) {
  if (vfs_ == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "MovieFrameRuntime::Start: VFS not available");
    return false;
  }

  const std::string previous_frame = frame_name_;
  if (player_.IsPlaying() && dependencies_.playback_finished) {
    dependencies_.playback_finished();
  }
  dependencies_.audio.StopMovieAudio();
  frame_name_.clear();
  SetFramePlaying(previous_frame, false);
  if (!player_.Start(avi_path, volume, vfs_, data_directory_,
                     dependencies_.audio.GetOutputSampleRate(),
                     dependencies_.audio.GetOutputChannelCount())) {
    return false;
  }

  frame_name_ = frame_name;
  if (auto source = player_.AudioSource(); source != nullptr) {
    dependencies_.audio.PlayMovieAudio(std::move(source), player_.VolumeNormalized());
  }
  if (dependencies_.playback_started) dependencies_.playback_started(avi_path);
  return true;
}

void MovieFrameRuntime::Stop(const std::string_view frame_name) {
  if (!IsPlayingFrame(frame_name)) return;
  openwow::media::ReleaseMoviePlayback(
      [this] { dependencies_.audio.StopMovieAudio(); },
      [this] { player_.Stop(); });
  Update(0.0F);
}

bool MovieFrameRuntime::IsPlaying() const { return player_.IsPlaying(); }

bool MovieFrameRuntime::PushFrame(const std::string& frame_name) const {
  if (lua_ == nullptr || frame_name.empty()) return false;
  const auto ref = dependencies_.frames.FindLuaRef(frame_name);
  if (!ref.has_value()) return false;
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) return true;
  lua_pop(lua_, 1);
  return false;
}

void MovieFrameRuntime::SetFramePlaying(const std::string& frame_name,
                                        const bool playing) const {
  if (lua_ == nullptr || frame_name.empty()) return;
  const int top = lua_gettop(lua_);
  if (PushFrame(frame_name)) {
    lua_pushboolean(lua_, playing ? 1 : 0);
    lua_setfield(lua_, -2, "__ow_movie_playing");
  }
  lua_settop(lua_, top);
}

void MovieFrameRuntime::DispatchHandler(const std::string& frame_name,
                                        const char* handler,
                                        const std::string* text) {
  if (frame_name.empty() || lua_ == nullptr) return;
  const int top = lua_gettop(lua_);
  if (PushFrame(frame_name)) {
    const int frame_index = lua_absindex(lua_, -1);
    int argument_count = 0;
    if (text != nullptr) {
      lua_pushlstring(lua_, text->c_str(), text->size());
      argument_count = 1;
    }
    const auto invocation =
        InvokeFrameScriptHandler(lua_, frame_index, handler, argument_count);
    if (invocation.status != LUA_OK) {
      const char* error = lua_tostring(lua_, -1);
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          std::string("MovieFrameRuntime: ") + handler + " error: " +
              (error != nullptr ? error : "(null)"));
      lua_pop(lua_, 1);
    }
  }
  lua_settop(lua_, top);
}

void MovieFrameRuntime::Update(const float elapsed_seconds) {
  if (lua_ == nullptr) return;

  if (player_.IsPlaying()) {
    std::optional<double> clock_override;
    if (dependencies_.audio.IsMovieAudioPlaying()) {
      clock_override = dependencies_.audio.MovieAudioTimeSeconds();
    }
    player_.Update(static_cast<double>(elapsed_seconds), clock_override);
  }

  const auto events = player_.ConsumeEvents();
  if (events.empty()) return;

  bool subtitles_enabled = false;
  const int top = lua_gettop(lua_);
  if (PushFrame(frame_name_)) {
    lua_getfield(lua_, -1, "__ow_movie_subtitles");
    subtitles_enabled = lua_toboolean(lua_, -1) != 0;
  }
  lua_settop(lua_, top);

  for (const auto& event : events) {
    switch (event.type) {
      case openwow::media::MovieEvent::kShowSubtitle:
        if (subtitles_enabled) {
          DispatchHandler(frame_name_, "OnMovieShowSubtitle", &event.text);
        }
        break;
      case openwow::media::MovieEvent::kHideSubtitle:
        if (subtitles_enabled) {
          DispatchHandler(frame_name_, "OnMovieHideSubtitle", nullptr);
        }
        break;
      case openwow::media::MovieEvent::kFinished: {
        const std::string finished_frame = std::move(frame_name_);
        openwow::media::ReleaseMoviePlayback(
            [this] { dependencies_.audio.StopMovieAudio(); },
            [this] { player_.Stop(); });
        SetFramePlaying(finished_frame, false);
        DispatchHandler(finished_frame, "OnMovieFinished", nullptr);
        if (dependencies_.playback_finished) dependencies_.playback_finished();
        break;
      }
    }
  }
}

void MovieFrameRuntime::OnFrameDestroyed(const std::string_view frame_name) {
  if (frame_name_ != frame_name) return;
  openwow::media::ReleaseMoviePlayback(
      [this] { dependencies_.audio.StopMovieAudio(); },
      [this] {
        player_.Stop(openwow::media::MovieStopReason::kOwnerTeardown);
      });
  (void)player_.ConsumeEvents();
  frame_name_.clear();
  if (dependencies_.playback_finished) dependencies_.playback_finished();
}

void MovieFrameRuntime::Shutdown() {
  const bool was_playing = player_.IsPlaying();
  if (player_.IsPlaying()) {
    openwow::media::ReleaseMoviePlayback(
        [this] { dependencies_.audio.StopMovieAudio(); },
        [this] {
          player_.Stop(openwow::media::MovieStopReason::kOwnerTeardown);
        });
  }
  (void)player_.ConsumeEvents();
  frame_name_.clear();
  vfs_ = nullptr;
  lua_ = nullptr;
  if (was_playing && dependencies_.playback_finished) {
    dependencies_.playback_finished();
  }
}

void MovieFrameRuntime::CompleteServerMovie() {
  if (dependencies_.playback_finished) dependencies_.playback_finished();
}

}
