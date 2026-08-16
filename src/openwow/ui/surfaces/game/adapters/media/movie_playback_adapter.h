#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game {

class MoviePlaybackAdapter final {
 public:
  void BindSession(openwow::game::WorldSession* session) noexcept;
  void Trigger(std::uint32_t movie_id);
  void PlaybackStarted(std::string_view movie_path);
  void PlaybackFinished();
  void Shutdown();

  [[nodiscard]] bool IsActive() const noexcept { return active_; }

 private:
  [[nodiscard]] int SelectFileData(std::uint32_t movie_id) const;
  void CompletePendingMovie();

  openwow::game::WorldSession* session_{nullptr};
  std::string pending_path_;
  bool pending_{false};
  bool active_{false};
};

}
