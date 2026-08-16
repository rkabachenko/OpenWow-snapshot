#pragma once

#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace openwow::platform {

class SingleInstanceGuard {
 public:
  SingleInstanceGuard() = default;

  SingleInstanceGuard(const SingleInstanceGuard&) = delete;
  SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
  SingleInstanceGuard(SingleInstanceGuard&&) = delete;
  SingleInstanceGuard& operator=(SingleInstanceGuard&&) = delete;

  ~SingleInstanceGuard() { Release(); }

  bool TryAcquire() {
#if defined(_WIN32)
    mutex_handle_ = ::CreateMutexA(nullptr, TRUE,
                                   "Blizzard Entertainment World of Warcraft");
    if (mutex_handle_ == nullptr) {

      return true;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
      ::CloseHandle(mutex_handle_);
      mutex_handle_ = nullptr;
      return false;
    }
    return true;

#else

    const std::string lock_path =
        "/tmp/openwow_" + std::to_string(static_cast<unsigned long>(::getuid())) + ".lock";
    lock_fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd_ < 0) {

      return true;
    }
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {

      ::close(lock_fd_);
      lock_fd_ = -1;
      return false;
    }
    return true;
#endif
  }

  void Release() {
#if defined(_WIN32)
    if (mutex_handle_ != nullptr) {
      ::ReleaseMutex(mutex_handle_);
      ::CloseHandle(mutex_handle_);
      mutex_handle_ = nullptr;
    }
#else
    if (lock_fd_ >= 0) {
      ::flock(lock_fd_, LOCK_UN);
      ::close(lock_fd_);
      lock_fd_ = -1;
    }
#endif
  }

 private:
#if defined(_WIN32)
  HANDLE mutex_handle_{nullptr};
#else
  int lock_fd_{-1};
#endif
};

}
