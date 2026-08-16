
#include "storm_file_io.h"

#include "openwow/platform/adapters/win32/win32_compat.h"
#include "storm_error.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <thread>
#include <sys/types.h>
#if defined(_WIN32)

#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace openwow::core {

namespace {

bool SeekFileStream(std::FILE* const file, const std::int64_t offset,
                    const int origin) {
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), origin) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), origin) == 0;
#endif
}

bool TellFileStream(std::FILE* const file, std::int64_t* const out_offset) {
    if (out_offset == nullptr) {
        return false;
    }

#if defined(_WIN32)
    const auto position = _ftelli64(file);
#else
    const auto position = ftello(file);
#endif
    if (position < 0) {
        return false;
    }

    *out_offset = static_cast<std::int64_t>(position);
    return true;
}

bool GetFileDescriptor(std::FILE* const file, int* const out_fd) {
    if (out_fd == nullptr) {
        return false;
    }

    errno = 0;
#if defined(_WIN32)
    const int fd = _fileno(file);
#else
    const int fd = fileno(file);
#endif
    if (fd < 0) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }

    *out_fd = fd;
    return true;
}

void SleepAfterRetriableFileIoFailure() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

#if defined(_WIN32)
bool FlushNativeFileBuffers(std::FILE* const file) {
    int fd = -1;
    if (!GetFileDescriptor(file, &fd)) {
        return false;
    }

    errno = 0;
    const auto native_handle = _get_osfhandle(fd);
    if (native_handle == -1) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }

    if (::FlushFileBuffers(reinterpret_cast<HANDLE>(native_handle)) == FALSE) {
        openwow::platform::SetPlatformLastError(::GetLastError());
        return false;
    }

    return true;
}
#else
bool FlushNativeFileBuffers(std::FILE* const file) {
    int fd = -1;
    if (!GetFileDescriptor(file, &fd)) {
        return false;
    }

    errno = 0;
    if (fsync(fd) != 0) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }

    return true;
}
#endif

constexpr std::int64_t kFileTimeTickNanoseconds = 100LL;
constexpr std::int64_t kFileTimeTicksPerSecond = 10'000'000LL;
constexpr std::int64_t kGameEpochFileTimeTicks = 0x01BF53EB256D4000LL;
constexpr std::int64_t kUnixEpochFileTimeOffsetTicks = 116444736000000000LL;

std::int64_t FloorDivideSigned(const std::int64_t dividend,
                               const std::int64_t divisor) {
    const auto quotient = dividend / divisor;
    const auto remainder = dividend % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

std::uint64_t GameTimeNsSince2000ToFileTimeTicks(
    const std::int64_t time_ns_since_2000) {
    const auto ticks_since_2000 =
        FloorDivideSigned(time_ns_since_2000, kFileTimeTickNanoseconds);
    return static_cast<std::uint64_t>(ticks_since_2000 + kGameEpochFileTimeTicks);
}

#if !defined(_WIN32)
timespec FileTimeTicksToPosixTimespec(const std::uint64_t file_time_ticks) {
    const auto unix_ticks =
        static_cast<std::int64_t>(file_time_ticks) - kUnixEpochFileTimeOffsetTicks;
    const auto seconds =
        FloorDivideSigned(unix_ticks, kFileTimeTicksPerSecond);
    const auto tick_remainder =
        unix_ticks - seconds * kFileTimeTicksPerSecond;

    timespec result{};
    result.tv_sec = static_cast<time_t>(seconds);
    result.tv_nsec =
        static_cast<long>(tick_remainder * kFileTimeTickNanoseconds);
    return result;
}
#endif

std::uint64_t AlignUpToMask(const std::uint64_t value, const std::uint32_t mask) {
    if (mask == 0) {
        return value;
    }

    return ~static_cast<std::uint64_t>(mask) & (static_cast<std::uint64_t>(mask) + value);
}

}

StormFileIO::~StormFileIO() {
    Close();
}

bool StormFileIO::Open(const char* path, const char* mode) {
    Close();
    file_ = std::fopen(path, mode);
    if (!file_) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }
    current_pos_ = 0;

    if (!SeekFileStream(file_, 0, SEEK_END) || !TellFileStream(file_, &current_pos_)) {
        Close();
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }
    cached_size_ = static_cast<uint64_t>(current_pos_);
    if (!SeekFileStream(file_, 0, SEEK_SET)) {
        Close();
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }
    current_pos_ = 0;
    return true;
}

void StormFileIO::Close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    current_pos_ = -1;
}

bool StormFileIO::Read(void* buffer, int64_t offset, uint32_t bytes_to_read,
                        uint32_t* bytes_read) {
    uint32_t local_read = 0;
    uint32_t* out = bytes_read ? bytes_read : &local_read;
    uint32_t min_bytes = bytes_read ? 1 : bytes_to_read;
    *out = 0;

    if (!file_) return false;

    for (int attempt = 0; attempt < 3; ++attempt) {

        if (current_pos_ != offset) {
            if (!SeekFileStream(file_, offset, SEEK_SET)) {
                current_pos_ = -1;
                continue;
            }
            current_pos_ = offset;
        }

        size_t read = std::fread(buffer, 1, bytes_to_read, file_);
        *out = static_cast<uint32_t>(read);
        current_pos_ = offset + read;

        if (read > 0) {
            return *out >= min_bytes;
        }

        current_pos_ = -1;
        if (attempt < 2) {
            SleepAfterRetriableFileIoFailure();
        }
    }

    return false;
}

bool StormFileIO::ReadAllowShort(void* buffer, int64_t offset, uint32_t bytes_to_read,
                                 uint32_t* bytes_read) {
    if (bytes_read == nullptr) {
        return false;
    }

    *bytes_read = 0;
    if (!file_) {
        return false;
    }

    if (bytes_to_read == 0) {
        return true;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (current_pos_ != offset) {
            if (!SeekFileStream(file_, offset, SEEK_SET)) {
                current_pos_ = -1;
                openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
                continue;
            }
            current_pos_ = offset;
        }

        errno = 0;
        const size_t read = std::fread(buffer, 1, bytes_to_read, file_);
        *bytes_read = static_cast<uint32_t>(read);
        current_pos_ = offset + static_cast<int64_t>(read);

        if (read != 0) {
            return true;
        }

        if (std::feof(file_)) {
            clearerr(file_);
            return true;
        }

        if (std::ferror(file_)) {
            clearerr(file_);
        }

        current_pos_ = -1;
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        if (attempt < 2) {
            SleepAfterRetriableFileIoFailure();
        }
    }

    return false;
}

bool StormFileIO::Write(const void* buffer, int64_t offset,
                         uint32_t bytes_to_write) {
    if (!file_) return false;

    if (!SeekFileStream(file_, offset, SEEK_SET)) {
        current_pos_ = -1;
        return false;
    }

    size_t written = std::fwrite(buffer, 1, bytes_to_write, file_);
    if (written != bytes_to_write) {
        current_pos_ = -1;
        return false;
    }

    current_pos_ = offset + written;

    uint64_t new_end = static_cast<uint64_t>(current_pos_);
    if (new_end > cached_size_) {
        cached_size_ = new_end;
    }

    return true;
}

bool StormFileIO::Flush() {
    if (!file_) {
        return false;
    }

    errno = 0;
    if (std::fflush(file_) != 0) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }

    return FlushNativeFileBuffers(file_);
}

bool StormFileIO::SetLastWriteTimeNsSince2000(
    const std::int64_t time_ns_since_2000) {
    if (!file_) {
        return false;
    }

    errno = 0;
    if (std::fflush(file_) != 0) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }

#if defined(_WIN32)
    int fd = -1;
    if (!GetFileDescriptor(file_, &fd)) {
        return false;
    }

    const auto native_handle = _get_osfhandle(fd);
    if (native_handle == -1) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }

    const auto file_time_ticks =
        GameTimeNsSince2000ToFileTimeTicks(time_ns_since_2000);
    FILETIME last_write_time{};
    last_write_time.dwLowDateTime =
        static_cast<DWORD>(file_time_ticks & 0xFFFFFFFFull);
    last_write_time.dwHighDateTime =
        static_cast<DWORD>(file_time_ticks >> 32u);

    if (::SetFileTime(reinterpret_cast<HANDLE>(native_handle), nullptr, nullptr,
                      &last_write_time) == FALSE) {
        openwow::platform::SetPlatformLastError(::GetLastError());
        return false;
    }
#else
    int fd = -1;
    if (!GetFileDescriptor(file_, &fd)) {
        return false;
    }

    timespec times[2]{};
#if defined(UTIME_OMIT)
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT;
#else
    struct stat native_stat {};
    if (fstat(fd, &native_stat) != 0) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }
#if defined(__APPLE__) || defined(__FreeBSD__)
    times[0].tv_sec = native_stat.st_atimespec.tv_sec;
    times[0].tv_nsec = native_stat.st_atimespec.tv_nsec;
#else
    times[0].tv_sec = native_stat.st_atim.tv_sec;
    times[0].tv_nsec = native_stat.st_atim.tv_nsec;
#endif
#endif
    times[1] = FileTimeTicksToPosixTimespec(
        GameTimeNsSince2000ToFileTimeTicks(time_ns_since_2000));
    if (futimens(fd, times) != 0) {
        openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(errno));
        return false;
    }
#endif

    return true;
}

bool StormFileIO::GetSize(uint64_t* out_size) {
    if (!out_size) {
        return false;
    }

    if (!file_) {
        StormSetLastError(8);
        return false;
    }

    if (!SeekFileStream(file_, 0, SEEK_END)) {
        current_pos_ = -1;
        StormSetLastError(8);
        return false;
    }

    std::int64_t size = 0;
    if (!TellFileStream(file_, &size)) {
        current_pos_ = -1;
        StormSetLastError(8);
        return false;
    }

    *out_size = static_cast<uint64_t>(size);
    cached_size_ = *out_size;
    current_pos_ = static_cast<int64_t>(*out_size);
    return true;
}

bool StormFileIO::Truncate(int64_t offset) {
    if (!file_) return false;

    if (!SeekFileStream(file_, offset, SEEK_SET)) {
        current_pos_ = -1;
        return false;
    }
    current_pos_ = offset;

#if defined(_WIN32)

    const bool ok = _chsize_s(_fileno(file_), static_cast<__int64>(offset)) == 0;
#else
    const bool ok = ftruncate(fileno(file_), static_cast<off_t>(offset)) == 0;
#endif

    if (!ok) {
        current_pos_ = -1;
        return false;
    }

    cached_size_ = static_cast<uint64_t>(offset);
    return true;
}

bool StormFileIO::QueryPosition(int64_t* out_offset) {
    if (out_offset == nullptr || !file_) {
        return false;
    }

    if (!TellFileStream(file_, out_offset)) {
        current_pos_ = -1;
        return false;
    }

    current_pos_ = *out_offset;
    return true;
}

bool StormFileIO::Seek(int64_t offset) {
    if (!file_) {
        return false;
    }

    if (!SeekFileStream(file_, offset, SEEK_SET)) {
        current_pos_ = -1;
        return false;
    }

    current_pos_ = offset;
    return true;
}

bool StormFileIO::GetCachedSize(uint64_t* out_size) const {
    if (!out_size) return false;
    *out_size = cached_size_;
    return true;
}

bool StormFileIO::GetCachedSizeParts(std::uint32_t* out_parts) const {
    out_parts[0] = static_cast<std::uint32_t>(cached_size_ & 0xFFFFFFFFu);
    out_parts[1] = static_cast<std::uint32_t>(cached_size_ >> 32);
    return true;
}

StormBufferedFileIO::StormBufferedFileIO() {
    io_buffer_ = static_cast<std::uint8_t*>(std::malloc(kMaxChunkSize));
    if (io_buffer_ != nullptr) {
        io_buffer_size_ = kMaxChunkSize;
    }
}

bool StormBufferedFileIO::Open(const char* path, const char* mode) {
    if (!StormFileIO::Open(path, mode)) {
        native_path_.clear();
        logical_end_ = 0;
        return false;
    }

    native_path_ = path ? path : "";
    logical_end_ = cached_size_;
    return true;
}

StormBufferedFileIO::~StormBufferedFileIO() {
    FinalizeLogicalSize();
    if (io_buffer_) {
        std::free(io_buffer_);
        io_buffer_ = nullptr;
    }
    io_buffer_size_ = 0;
}

bool StormBufferedFileIO::FinalizeLogicalSize() {
    const auto reopen_path = native_path_;
    const auto logical_end = logical_end_;

    Close();
    if (reopen_path.empty()) {
        return false;
    }

    file_ = std::fopen(reopen_path.c_str(), "r+b");
    if (file_ == nullptr) {
        current_pos_ = -1;
        return false;
    }

    current_pos_ = 0;
    cached_size_ = logical_end;
    const bool truncated = StormFileIO::Truncate(static_cast<std::int64_t>(logical_end));
    Close();
    return truncated;
}

void StormBufferedFileIO::UpdateLogicalEnd(const std::uint64_t value) {
    if (value > logical_end_) {
        logical_end_ = value;
    }
}

bool StormBufferedFileIO::BufferedRead(void* dest, uint32_t ,
                                        int64_t offset, uint32_t size,
                                        uint32_t* bytes_read) {
    const auto dest_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(dest));
    if ((alignment_mask_ & dest_address) == 0 && (alignment_mask_ & size) == 0) {
        if (bytes_read != nullptr) {
            return ReadAllowShort(dest, offset, size, bytes_read);
        }
        return Read(dest, offset, size, nullptr);
    }

    if (!io_buffer_) {
        return false;
    }

    uint32_t total_read = 0;
    auto* dst = static_cast<uint8_t*>(dest);

    while (size > 0) {
        uint32_t chunk = std::min(size, kMaxChunkSize);
        if (chunk & alignment_mask_) {
            chunk = (~alignment_mask_) & (alignment_mask_ + chunk);
        }

        uint32_t chunk_read = 0;
        if (!ReadAllowShort(io_buffer_, offset, chunk, &chunk_read)) {
            break;
        }

        uint32_t copy_size = std::min(chunk_read, size);
        std::memcpy(dst, io_buffer_, copy_size);

        dst += copy_size;
        offset += copy_size;
        size -= copy_size;
        total_read += copy_size;

        if (chunk_read < chunk) break;
    }

    if (bytes_read) {
        *bytes_read = total_read;
        return true;
    }
    return size == 0;
}

bool StormBufferedFileIO::BufferedWrite(const void* src, int64_t offset, uint32_t size) {
    const auto src_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(src));
    if ((alignment_mask_ & src_address) == 0 && (alignment_mask_ & size) == 0) {
        const bool wrote = Write(src, offset, size);
        if (wrote) {
            UpdateLogicalEnd(static_cast<std::uint64_t>(offset) + size);
        }
        return wrote;
    }

    if (!io_buffer_) {
        return false;
    }

    auto* s = static_cast<const uint8_t*>(src);
    std::uint64_t end = static_cast<std::uint64_t>(offset);

    while (size > 0) {
        uint32_t chunk = std::min(size, kMaxChunkSize);
        std::memcpy(io_buffer_, s, chunk);

        if (!Write(io_buffer_, offset, chunk)) {
            return false;
        }

        s += chunk;
        offset += chunk;
        size -= chunk;
        end = static_cast<std::uint64_t>(offset);
    }

    UpdateLogicalEnd(end);
    return true;
}

bool StormBufferedFileIO::SeekAligned(int64_t offset) {
    logical_end_ = static_cast<std::uint64_t>(offset);
    return StormFileIO::Truncate(static_cast<std::int64_t>(
        AlignUpToMask(static_cast<std::uint64_t>(offset), alignment_mask_)));
}

bool StormBufferedFileIO::GetCachedSize(uint64_t* out_size) const {
    if (out_size == nullptr) {
        return false;
    }

    *out_size = logical_end_;
    return true;
}

bool StormBufferedFileIO::GetCachedSizeParts(std::uint32_t* out_parts) const {
    if (out_parts == nullptr) {
        return false;
    }

    out_parts[0] = static_cast<std::uint32_t>(logical_end_ & 0xFFFFFFFFu);
    out_parts[1] = static_cast<std::uint32_t>(logical_end_ >> 32u);
    return true;
}

}
