
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace openwow::game {

class TutorialSystem {
 public:
  static constexpr int kMaxFlaggedTutorials = 60;
  static constexpr std::size_t kTutorialFlagWordCount = 8;
  using NamedSoundPlaybackCallback = std::function<void(std::string_view)>;

  static TutorialSystem& Instance();

  [[nodiscard]] int GetNextCompletedTutorial(int tutorial_id) const;

  [[nodiscard]] int GetPrevCompletedTutorial(int tutorial_id) const;

  void SaveFlaggedTutorials();

  void LoadFlaggedTutorials();

  void InitializeFromServerFlags(std::span<const std::uint32_t> flags);

  void ClearTutorials();

  void ResetTutorials();

  [[nodiscard]] bool CanResetTutorials() const;

  void TriggerTutorial(std::uint32_t tutorial_index);

  void FlagTutorial(std::uint32_t tutorial_index);

  [[nodiscard]] bool IsTutorialSeen(std::uint32_t index) const;

  [[nodiscard]] bool IsTutorialCompleted(std::uint32_t index) const;

  void MarkTutorialSeen(std::uint32_t index);

  std::array<int, kMaxFlaggedTutorials>& flagged_tutorials() {
    return flagged_tutorials_;
  }

  std::array<std::uint32_t, kTutorialFlagWordCount>& seen_bits() {
    return seen_bits_;
  }

  std::array<std::uint32_t, kTutorialFlagWordCount>& completed_bits() {
    return completed_bits_;
  }
  [[nodiscard]] const std::array<std::uint32_t, kTutorialFlagWordCount>&
  completed_bits() const {
    return completed_bits_;
  }

  void SetSeenBitsInitialized(bool v) { seen_bits_initialized_ = v; }
  void SetCompletedBitsInitialized(bool v) { completed_bits_initialized_ = v; }
  [[nodiscard]] bool IsSeenBitsInitialized() const {
    return seen_bits_initialized_;
  }
  [[nodiscard]] bool IsCompletedBitsInitialized() const {
    return completed_bits_initialized_;
  }

  void SetNamedSoundPlaybackCallbackForTests(NamedSoundPlaybackCallback callback);
  void ResetNamedSoundPlaybackCallbackForTests();

  void Reset();

 private:
  TutorialSystem() = default;

  void QueueFlaggedTutorialId(std::uint32_t tutorial_id);
  void PlayNamedTutorialSound(std::string_view sound_name);

  std::array<int, kMaxFlaggedTutorials> flagged_tutorials_{};

  bool seen_bits_initialized_{false};
  std::array<std::uint32_t, kTutorialFlagWordCount> seen_bits_{};

  bool completed_bits_initialized_{false};
  std::array<std::uint32_t, kTutorialFlagWordCount> completed_bits_{};

  NamedSoundPlaybackCallback named_sound_playback_callback_;
};

}
