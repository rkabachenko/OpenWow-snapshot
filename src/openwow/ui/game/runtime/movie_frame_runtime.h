#pragma once

#include "openwow/media/playback/movie_player.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::audio {
class SoundRuntime;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::game::runtime {

class FrameStore;

class MovieFrameRuntime final {
 public:
  struct Dependencies {
    FrameStore& frames;
    openwow::audio::SoundRuntime& audio;
    std::function<void(std::string_view)> playback_started;
    std::function<void()> playback_finished;
  };

  explicit MovieFrameRuntime(Dependencies dependencies);
  ~MovieFrameRuntime();
  MovieFrameRuntime(const MovieFrameRuntime&) = delete;
  MovieFrameRuntime& operator=(const MovieFrameRuntime&) = delete;

  void BindLuaState(lua_State* lua) noexcept { lua_ = lua; }
  void BindVirtualFileSystem(
      const openwow::vfs::VirtualFileSystem* vfs) noexcept {
    vfs_ = vfs;
  }
  void SetDataDirectory(const std::filesystem::path& directory) {
    data_directory_ = directory;
  }

  bool Start(const std::string& frame_name, const std::string& avi_path,
             int volume);
  void Stop(std::string_view frame_name);
  void Update(float elapsed_seconds);
  void OnFrameDestroyed(std::string_view frame_name);
  void Shutdown();
  void CompleteServerMovie();

  [[nodiscard]] bool IsPlaying() const;
  [[nodiscard]] std::string_view ActiveFrameName() const noexcept {
    return frame_name_;
  }
  [[nodiscard]] bool IsPlayingFrame(
      std::string_view frame_name) const noexcept {
    return player_.IsPlaying() && frame_name_ == frame_name;
  }
  [[nodiscard]] openwow::media::MoviePlayer& player() noexcept {
    return player_;
  }
  [[nodiscard]] const openwow::media::MoviePlayer& player() const noexcept {
    return player_;
  }

 private:
  [[nodiscard]] bool PushFrame(const std::string& frame_name) const;
  void SetFramePlaying(const std::string& frame_name, bool playing) const;
  void DispatchHandler(const std::string& frame_name, const char* handler,
                       const std::string* text);

  Dependencies dependencies_;
  lua_State* lua_{nullptr};
  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  openwow::media::MoviePlayer player_;
  std::string frame_name_;
  std::filesystem::path data_directory_;
};

}
