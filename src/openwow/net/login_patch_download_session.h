#pragma once

#include "openwow/data/startup_filesystem_state.h"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openwow::net {

enum class LoginPatchDownloadState : std::uint32_t {
  kInProgress = 0,
  kComplete = 2,
  kFailed = 3,
  kRestartLogin = 4,
};

struct LoginPatchDownloadFailureInfo {
  std::uint32_t result_code = 0;
  std::uint64_t auxiliary_value = 0;
};

class LoginPatchDownloadSession {
 public:
  virtual ~LoginPatchDownloadSession() = default;

  [[nodiscard]] virtual LoginPatchDownloadState download_state() const = 0;
  [[nodiscard]] virtual LoginPatchDownloadFailureInfo failure_info() const = 0;
  [[nodiscard]] virtual std::uint64_t current_size() const = 0;
  [[nodiscard]] virtual double progress_ratio() const = 0;
  virtual void AbortTransfer() = 0;
  virtual void Close(bool delete_file) = 0;
  virtual void FinalizeSuccess() = 0;

  [[nodiscard]] virtual std::optional<bool> DispatchInlineChunk(
      std::span<const std::uint8_t> bytes) {
    (void)bytes;
    return std::nullopt;
  }
};

struct LoginPatchManifestTransferSnapshot {
  std::filesystem::path relative_path;
  std::vector<std::uint8_t> payload;

  std::int64_t reported_required_extent = -1;
  std::uint64_t current_extent = 0;
  bool ready = false;
  bool transfer_complete = false;
};

class LoginPatchManifestSession final : public LoginPatchDownloadSession {
 public:
  LoginPatchManifestSession() = default;

  explicit LoginPatchManifestSession(
      std::vector<LoginPatchManifestTransferSnapshot> transfers)
      : transfers_(std::move(transfers)) {}

  void ResetTransfers(std::vector<LoginPatchManifestTransferSnapshot> transfers) {
    std::lock_guard lock(mutex_);
    transfers_ = std::move(transfers);
    state_ = LoginPatchDownloadState::kInProgress;
    failure_info_ = {};
  }

  void SetTransferSnapshot(
      const std::size_t index,
      const LoginPatchManifestTransferSnapshot& snapshot) {
    std::lock_guard lock(mutex_);
    if (index >= transfers_.size()) {
      transfers_.resize(index + 1);
    }
    transfers_[index] = snapshot;
    if (state_ != LoginPatchDownloadState::kRestartLogin) {
      state_ = LoginPatchDownloadState::kInProgress;
    }
  }

  void SetFailureInfo(const LoginPatchDownloadFailureInfo failure_info) {
    std::lock_guard lock(mutex_);
    failure_info_ = failure_info;
  }

  [[nodiscard]] LoginPatchDownloadState download_state() const override {
    std::lock_guard lock(mutex_);
    if (static_cast<std::uint32_t>(state_) > 1u) {
      return state_;
    }

    bool all_finished = true;
    bool all_exact = true;
    for (const auto& transfer : transfers_) {
      const std::uint64_t required_extent = EffectiveRequiredExtent(transfer);
      if (transfer.current_extent != required_extent) {
        all_exact = false;
      }
      if (!transfer.ready) {
        all_finished = false;
      }
    }

    if (all_finished) {
      state_ = all_exact ? LoginPatchDownloadState::kComplete
                         : LoginPatchDownloadState::kFailed;
    }
    return state_;
  }

  [[nodiscard]] LoginPatchDownloadFailureInfo failure_info() const override {
    std::lock_guard lock(mutex_);
    return failure_info_;
  }

  [[nodiscard]] std::uint64_t current_size() const override {
    std::lock_guard lock(mutex_);

    std::uint64_t current_total = 0;
    for (const auto& transfer : transfers_) {
      current_total += transfer.current_extent;
    }
    return current_total;
  }

  [[nodiscard]] double progress_ratio() const override {
    std::lock_guard lock(mutex_);

    std::uint64_t current_total = 0;
    std::uint64_t required_total = 0;
    for (const auto& transfer : transfers_) {
      current_total += transfer.current_extent;
      required_total += EffectiveRequiredExtent(transfer);
    }

    if (current_total == 0 || required_total == 0) {
      return 0.0;
    }
    return static_cast<double>(current_total) /
           static_cast<double>(required_total);
  }

  void AbortTransfer() override {
    std::lock_guard lock(mutex_);
    state_ = LoginPatchDownloadState::kFailed;
    failure_info_.result_code = 3;
    failure_info_.auxiliary_value = 0;
  }

  void Close(const bool delete_file) override {
    (void)delete_file;
  }

  void FinalizeSuccess() override {
    std::lock_guard lock(mutex_);
    if (transfers_.empty()) {
      return;
    }

    const std::string& startup_root =
        openwow::data::GetCachedStartupWorkingDirectory();
    for (const auto& transfer : transfers_) {
      const std::uint64_t write_extent = transfer.current_extent;
      if (write_extent > transfer.payload.size() ||
          write_extent >
              static_cast<std::uint64_t>(
                  (std::numeric_limits<std::streamsize>::max)())) {
        MarkFinalizeFailureResultCodeOnlyLocked(1);
        return;
      }

      const std::filesystem::path target_path =
          std::filesystem::path(startup_root + "/" +
                                transfer.relative_path.generic_string());
      std::ofstream output(
          target_path,
          std::ios::binary | std::ios::out | std::ios::trunc);
      if (!output.is_open()) {
        MarkFinalizeFailureResultCodeOnlyLocked(1);
        return;
      }

      if (write_extent != 0) {
        output.write(reinterpret_cast<const char*>(transfer.payload.data()),
                     static_cast<std::streamsize>(write_extent));
      }
      if (!output) {
        MarkFinalizeFailureResultCodeOnlyLocked(1);
        return;
      }
    }
  }

 private:
  void MarkFinalizeFailureResultCodeOnlyLocked(const std::uint32_t result_code) {
    state_ = LoginPatchDownloadState::kFailed;
    failure_info_.result_code = result_code;
  }

  [[nodiscard]] static std::uint64_t EffectiveRequiredExtent(
      const LoginPatchManifestTransferSnapshot& transfer) {
    if (transfer.transfer_complete) {
      return transfer.current_extent;
    }

    if (transfer.reported_required_extent < 0) {
      return DoubledCurrentExtent(transfer.current_extent);
    }

    const auto reported_required_extent =
        static_cast<std::uint64_t>(transfer.reported_required_extent);
    if (reported_required_extent < transfer.current_extent) {
      return DoubledCurrentExtent(transfer.current_extent);
    }
    return reported_required_extent;
  }

  [[nodiscard]] static std::uint64_t DoubledCurrentExtent(
      const std::uint64_t current_extent) {
    const auto max_extent = std::numeric_limits<std::uint64_t>::max();
    if (current_extent > max_extent / 2u) {
      return max_extent;
    }
    return current_extent * 2u;
  }

  mutable std::mutex mutex_;
  mutable LoginPatchDownloadState state_ = LoginPatchDownloadState::kInProgress;
  LoginPatchDownloadFailureInfo failure_info_{};
  std::vector<LoginPatchManifestTransferSnapshot> transfers_;
};

}
