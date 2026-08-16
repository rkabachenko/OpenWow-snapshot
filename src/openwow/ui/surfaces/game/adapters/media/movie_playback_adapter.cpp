#include "openwow/ui/surfaces/game/adapters/media/movie_playback_adapter.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/world_session.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <cstdio>
#include <string>

namespace openwow::ui::game {
namespace {

int MovieResolutionWidth() {
  const std::string resolution = CVarSystem::Instance().GetCVar("gxResolution");
  int width = 0;
  int height = 0;
  return std::sscanf(resolution.c_str(), "%dx%d", &width, &height) >= 1 &&
                 width > 0
             ? width
             : 1024;
}

}

void MoviePlaybackAdapter::BindSession(
    openwow::game::WorldSession* session) noexcept {
  session_ = session;
}

int MoviePlaybackAdapter::SelectFileData(const std::uint32_t movie_id) const {
  const auto* dbc = session_ != nullptr ? session_->GetDbcLoader() : nullptr;
  if (dbc == nullptr || dbc->movie().LookupEntry(movie_id) == nullptr) return -1;

  const auto target_width = static_cast<std::uint32_t>(MovieResolutionWidth());
  int best_id = -1;
  std::uint32_t best_width = 0;
  for (const auto& variation : dbc->movie_variation().entries()) {
    if (variation.movie_id != movie_id) continue;
    const auto* movie_file =
        dbc->movie_file_data().LookupEntry(variation.file_data_id);
    const auto* file = dbc->file_data().LookupEntry(variation.file_data_id);
    if (movie_file == nullptr || file == nullptr) continue;

    const std::uint32_t width = movie_file->width;
    if (width == target_width) return static_cast<int>(variation.file_data_id);
    if (best_id < 0 ||
        (best_width >= target_width && width < best_width) ||
        (best_width < target_width && width > best_width &&
         width <= target_width)) {
      best_id = static_cast<int>(variation.file_data_id);
      best_width = width;
    }
  }
  return best_id;
}

void MoviePlaybackAdapter::Trigger(const std::uint32_t movie_id) {
  if (pending_ || active_) CompletePendingMovie();
  const auto* dbc = session_ != nullptr ? session_->GetDbcLoader() : nullptr;
  const auto* movie = dbc != nullptr ? dbc->movie().LookupEntry(movie_id) : nullptr;
  const int file_data_id = SelectFileData(movie_id);
  if (movie == nullptr || file_data_id < 0) return;

  const auto* file =
      dbc->file_data().LookupEntry(static_cast<std::uint32_t>(file_data_id));
  if (file == nullptr) return;
  pending_path_ = std::string(file->filepath);
  pending_path_.append(file->filename);
  if (const auto dot = pending_path_.find('.'); dot != std::string::npos) {
    pending_path_.resize(dot);
  }
  pending_ = true;

  ScriptEventDispatch::Get().FireEventArgs(
      events::PLAY_MOVIE, {pending_path_, static_cast<int>(movie->volume)});

  if (pending_) CompletePendingMovie();
}

void MoviePlaybackAdapter::PlaybackStarted(const std::string_view movie_path) {
  if (!pending_ || movie_path != pending_path_) return;
  pending_ = false;
  active_ = true;
  if (session_ != nullptr) session_->spell_visual().BeginMovie();
  openwow::game::SetCinematicInputBlocked(true);
  openwow::game::ResetInputAfterCinematicTransition();
}

void MoviePlaybackAdapter::PlaybackFinished() {
  if (pending_ || active_) CompletePendingMovie();
}

void MoviePlaybackAdapter::CompletePendingMovie() {
  const bool should_complete = pending_ || active_;
  pending_ = false;
  active_ = false;
  pending_path_.clear();
  if (!should_complete) return;

  openwow::game::SetCinematicInputBlocked(false);
  openwow::game::ResetInputAfterCinematicTransition();
  if (session_ != nullptr) session_->spell_visual().StopMovie();
  if (!openwow::net::ClientServices__SendPacket(
          openwow::net::wotlk::PacketSender::BuildCompleteMovie())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Unable to send CMSG_COMPLETE_MOVIE");
  }
}

void MoviePlaybackAdapter::Shutdown() {
  CompletePendingMovie();
  session_ = nullptr;
}

}
