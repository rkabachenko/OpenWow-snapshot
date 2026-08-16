#include "openwow/net/login_inline_download_cache.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <system_error>
#include <vector>

namespace openwow::net {
namespace {

std::uint32_t TruncateToLow32(const std::uint64_t value) {
  return static_cast<std::uint32_t>(value);
}

std::streamoff RetailLow32Offset(const std::uint64_t value) {
  return static_cast<std::streamoff>(TruncateToLow32(value));
}

std::uint64_t RetailLow32ExtentWithExtra(const std::uint64_t value,
                                         const std::uint32_t extra) {
  return static_cast<std::uint32_t>(TruncateToLow32(value) + extra);
}

std::array<std::uint8_t, LoginInlineDownloadCache::kDigestSize>
ComputeFileDigestLow32Range(std::fstream& file,
                            const std::uint64_t range_start,
                            const std::uint64_t range_end) {
  constexpr std::size_t kReadChunkSize = 0x8000;

  openwow::core::MD5Context md5{};
  openwow::core::MD5_Init(&md5);

  std::vector<char> buffer(kReadChunkSize);
  const auto start_low = static_cast<std::uint32_t>(range_start);
  const auto end_low = static_cast<std::uint32_t>(range_end);
  std::uint32_t remaining = 0;
  if (start_low < end_low) {
    remaining = end_low - start_low;
  }

  file.clear();
  file.seekg(static_cast<std::streamoff>(start_low), std::ios::beg);
  while (remaining != 0) {
    const std::size_t read_now = static_cast<std::size_t>(
        std::min<std::uint32_t>(remaining,
                                static_cast<std::uint32_t>(buffer.size())));
    file.read(buffer.data(), static_cast<std::streamsize>(read_now));
    if (file.gcount() != static_cast<std::streamsize>(read_now)) {
      return {};
    }
    openwow::core::MD5_Update(&md5, buffer.data(), read_now);
    remaining -= read_now;
  }

  std::array<std::uint8_t, LoginInlineDownloadCache::kDigestSize> digest{};
  openwow::core::MD5_Final(&md5, digest.data());
  return digest;
}

}

LoginInlineDownloadCache::~LoginInlineDownloadCache() {
  Close(false);
}

LoginInlineDownloadCache::StartResult LoginInlineDownloadCache::Begin(
    const std::filesystem::path& target_path,
    const std::span<const std::uint8_t, kDigestSize> expected_digest,
    const std::uint64_t expected_size) {
  std::lock_guard lock(mutex_);

  CloseFileLocked();
  target_path_ = target_path;
  std::copy(expected_digest.begin(),
            expected_digest.end(),
            expected_digest_.begin());
  current_size_ = 0;
  expected_size_ = expected_size;
  bytes_since_trailer_flush_ = 0;
  result_code_ = 0;
  auxiliary_value_ = 0;
  state_ = State::kInProgress;
  uses_partial_file_ = false;

  StartResult result{};

  const auto partial_path = PartialPath();
  if (std::filesystem::is_regular_file(partial_path)) {
    if (OpenExistingFile(partial_path)) {
      std::error_code ec;
      const std::uint64_t partial_file_size =
          std::filesystem::file_size(partial_path, ec);
      if (!ec) {
        if (partial_file_size == expected_size_) {
          if (ValidateFileDigestLocked(expected_size_)) {
            current_size_ = expected_size_;
            uses_partial_file_ = true;
            state_ = State::kComplete;
            result.disposition = StartDisposition::kAlreadyComplete;
            result.resume_offset = current_size_;
            result.uses_partial_file = true;
            return result;
          }
        } else if (partial_file_size == expected_size_ + sizeof(PartialTrailer)) {
          PartialTrailer trailer{};
          if (ReadPartialTrailerLocked(&trailer) &&
              trailer.magic == kPartialTrailerMagic &&
              trailer.reserved == 0 &&
              trailer.digest == expected_digest_ &&
              trailer.expected_size == expected_size_) {
            current_size_ = trailer.current_size;
            uses_partial_file_ = true;
            if (current_size_ == expected_size_) {
              if (ValidateFileDigestLocked(current_size_)) {
                state_ = State::kComplete;
                result.disposition = StartDisposition::kAlreadyComplete;
                result.resume_offset = current_size_;
                result.uses_partial_file = true;
                return result;
              }
            } else if (current_size_ < expected_size_) {
              result.disposition = StartDisposition::kTransferRequired;
              result.resume_offset = current_size_;
              result.uses_partial_file = true;
              return result;
            }
          }
        }
      }
      CloseFileLocked();
    }
  }

  if (std::filesystem::is_regular_file(target_path_)) {
    std::error_code ec;
    const std::uint64_t target_file_size =
        std::filesystem::file_size(target_path_, ec);
    if (OpenExistingFile(target_path_) &&
        !ec &&
        target_file_size == expected_size_ &&
        ValidateFileDigestLocked(expected_size_)) {
      current_size_ = expected_size_;
      state_ = State::kComplete;
      CloseFileLocked();
      result.disposition = StartDisposition::kAlreadyComplete;
      result.resume_offset = current_size_;
      result.uses_partial_file = false;
      return result;
    }
    CloseFileLocked();
  }

  uses_partial_file_ = true;
  current_size_ = 0;
  if (!CreateSizedFile(
          partial_path,
          RetailLow32ExtentWithExtra(
              expected_size_,
              static_cast<std::uint32_t>(sizeof(PartialTrailer))))) {
    result.disposition = StartDisposition::kFailed;
    return result;
  }

  result.disposition = StartDisposition::kTransferRequired;
  result.resume_offset = 0;
  result.uses_partial_file = true;
  return result;
}

bool LoginInlineDownloadCache::AppendChunk(
    const std::span<const std::uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) {
    state_ = State::kFailed;
    return false;
  }
  if (!bytes.empty()) {
    file_.clear();
    file_.seekp(RetailLow32Offset(current_size_), std::ios::beg);
    file_.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!file_) {
      MarkFailedLocked(1);
      return false;
    }
  }

  current_size_ += bytes.size();
  bytes_since_trailer_flush_ += static_cast<std::uint32_t>(bytes.size());
  if (current_size_ > expected_size_) {
    MarkFailedLocked(1);
    return false;
  }

  if (bytes_since_trailer_flush_ >= kChunkFlushThreshold ||
      current_size_ == expected_size_) {
    if (!WriteTrailerLocked()) {
      MarkFailedLocked(1);
      return false;
    }
    bytes_since_trailer_flush_ = 0;
  }

  if (current_size_ == expected_size_) {
    state_ = ValidateFileDigestLocked(current_size_)
                 ? State::kComplete
                 : State::kFailed;
  }

  return state_ != State::kFailed;
}

void LoginInlineDownloadCache::AbortTransfer() {
  std::lock_guard lock(mutex_);
  state_ = State::kFailed;
  result_code_ = 3;
  if (file_.is_open() && !WriteTrailerLocked()) {
    MarkFailedLocked(1);
  }
  CloseFileLocked();
}

void LoginInlineDownloadCache::Close(const bool delete_file) {
  std::lock_guard lock(mutex_);
  CloseFileLocked();
  if (!delete_file) {
    return;
  }

  std::error_code ec;
  std::filesystem::remove(uses_partial_file_ ? PartialPath() : target_path_, ec);
}

void LoginInlineDownloadCache::FinalizeSuccess() {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) {
    return;
  }

  const auto finalize_path = uses_partial_file_ ? PartialPath() : target_path_;
  CloseFileLocked();

  std::error_code ec;
  std::filesystem::resize_file(finalize_path,
                               static_cast<std::uint64_t>(
                                   TruncateToLow32(expected_size_)),
                               ec);
  if (ec) {
    MarkFailedLocked(1);
    return;
  }

  if (uses_partial_file_) {
    if (std::filesystem::is_regular_file(target_path_)) {
      std::filesystem::remove(target_path_, ec);
      ec.clear();
    }
    std::filesystem::rename(finalize_path, target_path_, ec);
    if (ec) {
      MarkFailedLocked(1);
      return;
    }
  }
}

LoginInlineDownloadCache::State LoginInlineDownloadCache::state() const {
  std::lock_guard lock(mutex_);
  return state_;
}

LoginPatchDownloadState LoginInlineDownloadCache::download_state() const {
  switch (state()) {
    case State::kComplete:
      return LoginPatchDownloadState::kComplete;
    case State::kFailed:
      return LoginPatchDownloadState::kFailed;
    case State::kInProgress:
      return LoginPatchDownloadState::kInProgress;
  }
  return LoginPatchDownloadState::kFailed;
}

LoginPatchDownloadFailureInfo LoginInlineDownloadCache::failure_info() const {
  std::lock_guard lock(mutex_);
  return LoginPatchDownloadFailureInfo{
      .result_code = result_code_,
      .auxiliary_value = auxiliary_value_,
  };
}

std::uint32_t LoginInlineDownloadCache::result_code() const {
  std::lock_guard lock(mutex_);
  return result_code_;
}

std::uint64_t LoginInlineDownloadCache::current_size() const {
  std::lock_guard lock(mutex_);
  return current_size_;
}

std::uint64_t LoginInlineDownloadCache::expected_size() const {
  std::lock_guard lock(mutex_);
  return expected_size_;
}

bool LoginInlineDownloadCache::uses_partial_file() const {
  std::lock_guard lock(mutex_);
  return uses_partial_file_;
}

double LoginInlineDownloadCache::progress_ratio() const {
  std::lock_guard lock(mutex_);
  if (expected_size_ == 0) {
    return 0.0;
  }
  return static_cast<double>(current_size_) /
         static_cast<double>(expected_size_);
}

std::filesystem::path LoginInlineDownloadCache::PartialPath() const {
  auto partial_path = target_path_;
  partial_path += ".partial";
  return partial_path;
}

bool LoginInlineDownloadCache::OpenExistingFile(
    const std::filesystem::path& path) {
  file_.clear();
  file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
  return file_.is_open();
}

bool LoginInlineDownloadCache::CreateSizedFile(
    const std::filesystem::path& path,
    const std::uint64_t size) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    ec.clear();
  }

  {
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
    if (!create.is_open()) {
      state_ = State::kFailed;
      result_code_ = 1;
      return false;
    }
  }

  std::filesystem::resize_file(path, size, ec);
  if (ec) {
    state_ = State::kFailed;
    result_code_ = 2;
    auxiliary_value_ =
        ((expected_size_ + sizeof(PartialTrailer)) >> 20) + 1;
    return false;
  }

  if (!OpenExistingFile(path)) {
    state_ = State::kFailed;
    result_code_ = 1;
    return false;
  }

  return true;
}

bool LoginInlineDownloadCache::WriteTrailerLocked() {
  if (!file_.is_open()) {
    return false;
  }

  PartialTrailer trailer{};
  trailer.digest = expected_digest_;
  trailer.current_size = current_size_;
  trailer.expected_size = expected_size_;

  file_.clear();
  file_.seekp(RetailLow32Offset(expected_size_), std::ios::beg);
  file_.write(reinterpret_cast<const char*>(&trailer),
              static_cast<std::streamsize>(sizeof(trailer)));
  file_.flush();
  return static_cast<bool>(file_);
}

bool LoginInlineDownloadCache::ValidateFileDigestLocked(
    const std::uint64_t size) {
  if (!file_.is_open()) {
    return false;
  }
  const auto digest = ComputeFileDigestLow32Range(file_, 0, size);
  return digest == expected_digest_;
}

bool LoginInlineDownloadCache::ReadPartialTrailerLocked(
    PartialTrailer* trailer) {
  if (!file_.is_open() || trailer == nullptr) {
    return false;
  }

  std::error_code ec;
  const auto file_size = std::filesystem::file_size(PartialPath(), ec);
  if (ec || file_size < sizeof(PartialTrailer)) {
    return false;
  }

  file_.clear();
  file_.seekg(static_cast<std::streamoff>(file_size - sizeof(PartialTrailer)),
              std::ios::beg);
  file_.read(reinterpret_cast<char*>(trailer),
             static_cast<std::streamsize>(sizeof(*trailer)));
  return file_.gcount() == static_cast<std::streamsize>(sizeof(*trailer));
}

void LoginInlineDownloadCache::CloseFileLocked() {
  file_.clear();
  if (!file_.is_open()) {
    return;
  }
  file_.flush();
  file_.close();
  file_.clear();
}

void LoginInlineDownloadCache::MarkFailedLocked(
    const std::uint32_t result_code) {
  state_ = State::kFailed;
  result_code_ = result_code;
}

bool DispatchInlineDownloadChunk(LoginInlineDownloadCache* const download,
                                 const std::span<const std::uint8_t> bytes) {
  if (download == nullptr) {
    return false;
  }
  return download->AppendChunk(bytes);
}

std::optional<bool> LoginInlineDownloadCache::DispatchInlineChunk(
    const std::span<const std::uint8_t> bytes) {
  return AppendChunk(bytes);
}

LoginInlineDownloadBootstrapResult BeginNamedInlineDownload(
    const std::filesystem::path& client_root,
    const std::filesystem::path& relative_target_path,
    const std::span<const std::uint8_t, LoginInlineDownloadCache::kDigestSize>
        expected_digest,
    const std::uint64_t expected_size,
    std::unique_ptr<LoginInlineDownloadCache>* const active_download) {
  LoginInlineDownloadBootstrapResult result{};
  result.target_path = client_root / relative_target_path;

  std::error_code ec;
  if (result.target_path.has_parent_path()) {
    std::filesystem::create_directories(result.target_path.parent_path(), ec);
  }

  if (active_download == nullptr) {
    return result;
  }

  active_download->reset(new (std::nothrow) LoginInlineDownloadCache());
  if (!*active_download) {
    return result;
  }

  result.has_active_download = true;
  result.start = (*active_download)
                     ->Begin(result.target_path, expected_digest, expected_size);
  return result;
}

}
