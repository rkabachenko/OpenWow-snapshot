#pragma once

#include <utility>

namespace openwow::media {

template <typename DetachAudio, typename ReleasePlayer>
void ReleaseMoviePlayback(DetachAudio&& detach_audio,
                          ReleasePlayer&& release_player) {
  std::forward<DetachAudio>(detach_audio)();
  std::forward<ReleasePlayer>(release_player)();
}

}
