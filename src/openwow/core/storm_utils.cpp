
#include "storm_utils.h"

#include "openwow/platform/process/os_platform.h"
#include "storm_error.h"
#include "storm_path.h"
#include "storm_utf8.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"
#include "storm_string.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace openwow::core {

namespace {

using IOUnitGetTypeTagFn = std::uint32_t (*)();

struct SFileOpenFileExTypeTaggedCompatVTable {
    IOUnitGetTypeTagFn vf0_type_tag = nullptr;
};

struct SFileOpenFileExForwardingCompat {
    SFileOpenFileExTypeTaggedCompatVTable* vtable = nullptr;
    void* inner = nullptr;
};

std::uint32_t QuerySFileOpenFileExTypeTag(const void* self) {
    if (!self) {
        return 0;
    }

    const auto* tagged_self =
        static_cast<const SFileOpenFileExForwardingCompat*>(self);
    if (!tagged_self->vtable || !tagged_self->vtable->vf0_type_tag) {
        return 0;
    }

    return tagged_self->vtable->vf0_type_tag();
}

constexpr std::size_t kStormLocalPathCapacity = 1024;
constexpr std::size_t kStormDiskSpacePathCapacity = 260;

detail::GetExeDirectoryFullPathProvider&
MutableGetExeDirectoryFullPathProviderForTests() {
    static detail::GetExeDirectoryFullPathProvider provider = nullptr;
    return provider;
}

std::array<char, kStormLocalPathCapacity> NormalizeStormLocalPath(
    const char* input) {
    std::array<char, kStormLocalPathCapacity> normalized{};
    if (!input) {
        return normalized;
    }

    std::size_t index = 0;
    for (; index + 1 < normalized.size() && input[index] != '\0'; ++index) {
        const char ch = input[index];
        normalized[index] = ch == '/' ? '\\' : ch;
    }
    normalized[index] = '\0';
    return normalized;
}

#if defined(_WIN32)
std::string BuildStormAnsiLocalPath(const char* input) {
    const auto normalized = NormalizeStormLocalPath(input);
    return Utf8ToCurrentCodePageString(normalized.data());
}
#else
std::filesystem::path BuildStormNativeLocalPath(const char* input) {
    auto normalized = NormalizeStormLocalPath(input);
    std::replace(normalized.begin(), normalized.end(), '\\',
                 std::filesystem::path::preferred_separator);
    return std::filesystem::path(normalized.data());
}
#endif

std::array<char, kStormDiskSpacePathCapacity>
BuildStormDiskSpaceDirectoryPath(const char* input) {
    std::array<char, kStormDiskSpacePathCapacity> directory{};
    if (!input || input[0] == '\0') {
        return directory;
    }

    const char* const leaf = FindStormPathLeafName(input);
    const auto prefix_length =
        static_cast<std::size_t>(leaf >= input ? leaf - input : 0);
    const auto copy_capacity = std::min<std::size_t>(
        prefix_length + 1, directory.size());
    SStrCopy(directory.data(), input, copy_capacity);
    return directory;
}

int ConvertCurrentCodePageToUtf8Bounded(const char* input, char* output,
                                        uint32_t output_size) {
#ifdef _WIN32
    if (!input) {
        static constexpr char16_t kEmptyString[]{u'\0'};
        return StormUtf16ToUtf8Bounded(output, output_size, kEmptyString, -1,
                                       nullptr, nullptr);
    }

    const int input_bytes = static_cast<int>(std::strlen(input));
    std::vector<wchar_t> wide(static_cast<std::size_t>(input_bytes) + 1u,
                              L'\0');
    const int wide_chars = ::MultiByteToWideChar(
        CP_ACP, 0, input, input_bytes, wide.data(),
        static_cast<int>(wide.size()));
    const int safe_wide_chars = wide_chars > 0 ? wide_chars : 0;
    wide[static_cast<std::size_t>(safe_wide_chars)] = L'\0';

    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return StormUtf16ToUtf8Bounded(
        output, output_size,
        reinterpret_cast<const char16_t*>(wide.data()), -1, nullptr, nullptr);
#else
    if (!output || output_size == 0) {
        return 1;
    }

    if (!input) {
        output[0] = '\0';
        return 0;
    }

    const size_t input_length = std::strlen(input);
    const size_t copy_length =
        std::min(input_length, static_cast<size_t>(output_size - 1));
    std::memcpy(output, input, copy_length);
    output[copy_length] = '\0';
    return copy_length == input_length ? 0 : 1;
#endif
}

bool QueryExecutableFullPathUtf8(std::string& output) {
    if (const auto provider =
            MutableGetExeDirectoryFullPathProviderForTests()) {
        output = provider();
        return true;
    }

#if defined(_WIN32)
    output = openwow::platform::OS_GetModulePath();
    return !output.empty();
#elif defined(__linux__)
    char path[PATH_MAX]{};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) {
        return false;
    }

    path[length] = '\0';
    output = path;
    return true;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) {
        return false;
    }

    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return false;
    }

    path.resize(std::strlen(path.c_str()));
    output = path;
    return true;
#else
    output.clear();
    return false;
#endif
}

}

const char* SFileOpenFile_FindSubstringNoCaseIfNonNull(const char* haystack,
                                                       const char* needle,
                                                       uint32_t max_len) {
    if (!haystack || !needle) {
        return nullptr;
    }

    return SStrCaseStrBounded(haystack, needle, max_len);
}

int StormUtf16ToUtf8Bounded(char* output, uint32_t output_size,
                            const char16_t* input, int input_length,
                            uint32_t* bytes_written,
                            int* code_units_consumed) {
    const auto finish = [&](int result, size_t consumed_units,
                            uint32_t written_bytes) {
        if (code_units_consumed) {
            *code_units_consumed = static_cast<int>(consumed_units);
        }
        if (bytes_written) {
            *bytes_written = written_bytes;
        }
        return result;
    };

    if (!input) {
        return finish(-1, 0, 0);
    }

    const bool bounded_input = input_length >= 0;
    const size_t input_limit =
        bounded_input ? static_cast<size_t>(input_length) : 0;
    size_t input_offset = 0;
    uint32_t output_offset = 0;

    if (bounded_input && input_offset >= input_limit) {
        return finish(-1, input_offset, output_offset);
    }

    while (true) {
        uint32_t codepoint = input[input_offset];
        size_t code_units_for_codepoint = 1;

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (bounded_input && input_offset + 1 >= input_limit) {
                return finish(-1, input_offset, output_offset);
            }

            if (!bounded_input || input_offset + 1 < input_limit) {
                const uint32_t low_surrogate = input[input_offset + 1];
                if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                    codepoint = low_surrogate
                              + ((codepoint - 0xD7B7u) << 10);
                    code_units_for_codepoint = 2;
                }
            }
        }

        if (codepoint > 0x7FFFFFFF) {
            codepoint = 0xFFFD;
        }

        if (codepoint == 0) {
            if (output && output_offset < output_size) {
                output[output_offset] = '\0';
                return finish(0, input_offset, output_offset + 1);
            }
            return finish(1, input_offset, output_offset);
        }

        char encoded_codepoint[7]{};
        const char* const encoded_end =
            EncodeLegacyUtf8Codepoint(codepoint, encoded_codepoint);
        const uint32_t encoded_length = static_cast<uint32_t>(
            encoded_end - encoded_codepoint);

        if (!output || output_size - output_offset < encoded_length) {
            return finish(encoded_length, input_offset, output_offset);
        }

        std::memcpy(output + output_offset, encoded_codepoint, encoded_length);
        output_offset += encoded_length;
        input_offset += code_units_for_codepoint;

        if (bounded_input && input_offset >= input_limit) {
            return finish(-1, input_offset, output_offset);
        }
    }
}

int StormUtf8ToUtf16Bounded(char16_t* output, int output_length,
                            const char* input, int input_length,
                            int* code_units_written,
                            uint32_t* bytes_consumed) {
    const auto* const input_start =
        reinterpret_cast<const unsigned char*>(input);
    const auto* cursor = input_start;
    const bool bounded_input = input_length >= 0;
    const auto* const input_end =
        bounded_input ? input_start + input_length : nullptr;
    char16_t* out = output;
    char16_t* const out_end =
        output && output_length > 0 ? output + output_length : output;

    const auto finish = [&](int result) {
        if (bytes_consumed) {
            *bytes_consumed = static_cast<uint32_t>(cursor - input_start);
        }
        if (code_units_written) {
            *code_units_written = static_cast<int>(out - output);
        }
        return result;
    };

    if (bounded_input && cursor >= input_end) {
        return finish(-1);
    }

    while (true) {
        const uint8_t width = detail::LegacyUtf8SequenceLength(*cursor);
        if (bounded_input && cursor + width > input_end) {
            return finish(-static_cast<int>(width));
        }

        uint32_t codepoint = 0;
        int consumed = 0;
        if (*cursor != 0) {
            if (width <= 1) {
                codepoint = *cursor;
                consumed = 1;
            } else {
                for (int index = 0; index < width - 1; ++index) {
                    codepoint =
                        (codepoint + static_cast<uint32_t>(cursor[index])) << 6;
                }
                codepoint += static_cast<uint32_t>(cursor[width - 1]);
                codepoint -= detail::kLegacyUtf8Offsets[width];
                consumed = width;
            }
        }

        if (out >= out_end) {
            return finish(1);
        }

        if (codepoint > 0xFFFF) {
            if (codepoint > 0x10FFFF) {
                *out++ = static_cast<char16_t>(0xFFFD);
            } else {
                if (out + 1 >= out_end) {
                    return finish(1);
                }

                codepoint -= 0x10000;
                *out++ = static_cast<char16_t>((codepoint >> 10) + 0xD800);
                *out++ = static_cast<char16_t>((codepoint & 0x3FF) + 0xDC00);
            }
        } else {
            *out++ = static_cast<char16_t>(codepoint);
            if (codepoint == 0) {
                return finish(0);
            }
        }

        cursor += consumed;
        if (bounded_input && cursor >= input_end) {
            return finish(-1);
        }
    }
}

int CurrentCodePageToUtf8Bounded(char* output, uint32_t output_size,
                                 const char* input) {
    return ConvertCurrentCodePageToUtf8Bounded(input, output, output_size);
}

std::string CurrentCodePageToUtf8String(const char* input) {
    const std::size_t input_bytes = input ? std::strlen(input) : 0u;
    std::string output((input_bytes + 1u) * 3u, '\0');
    if (output.empty()) {
        return {};
    }

    const int conversion_result = CurrentCodePageToUtf8Bounded(
        output.data(), static_cast<uint32_t>(output.size()), input);
    if (conversion_result != 0 && output[0] == '\0') {
        return {};
    }

    output.resize(std::strlen(output.c_str()));
    return output;
}

std::string Utf8ToCurrentCodePageString(const char* input) {
#if defined(_WIN32)
    static constexpr char kReplacement[] = "_";

    const size_t input_bytes = input ? std::strlen(input) : 0;
    std::vector<char16_t> wide_buffer(input_bytes + 1u, u'\0');
    int wide_units_written = 0;
    uint32_t bytes_consumed = 0;
    const int decode_result = StormUtf8ToUtf16Bounded(
        wide_buffer.data(), static_cast<int>(wide_buffer.size()),
        input ? input : "", -1, &wide_units_written, &bytes_consumed);
    (void)decode_result;
    (void)bytes_consumed;

    const int wide_chars = wide_units_written > 0 ? wide_units_written - 1 : 0;
    if (wide_chars <= 0) {
        return {};
    }

    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const wchar_t* const wide =
        reinterpret_cast<const wchar_t*>(wide_buffer.data());
    const int required_bytes = ::WideCharToMultiByte(
        CP_ACP, 0, wide, wide_chars, nullptr, 0, kReplacement, nullptr);
    if (required_bytes <= 0) {
        return {};
    }

    std::string output(static_cast<size_t>(required_bytes), '\0');
    const int written_bytes = ::WideCharToMultiByte(
        CP_ACP, 0, wide, wide_chars, output.data(), required_bytes,
        kReplacement, nullptr);
    if (written_bytes <= 0) {
        return {};
    }

    if (written_bytes != required_bytes) {
        output.resize(static_cast<size_t>(written_bytes));
    }
    return output;
#else
    return input ? std::string(input) : std::string{};
#endif
}

bool DeleteStormFilePath(const char* input) {
#if defined(_WIN32)
    const std::string ansi_path = BuildStormAnsiLocalPath(input);
    if (ansi_path.empty()) {
        return false;
    }
    return ::DeleteFileA(ansi_path.c_str()) != FALSE;
#else
    std::error_code ec;
    return std::filesystem::remove(BuildStormNativeLocalPath(input), ec);
#endif
}

bool MoveStormFilePathNoReplace(const char* source, const char* destination) {
#if defined(_WIN32)
    const std::string ansi_source = BuildStormAnsiLocalPath(source);
    const std::string ansi_destination =
        BuildStormAnsiLocalPath(destination);
    if (ansi_source.empty() || ansi_destination.empty()) {
        return false;
    }
    return ::MoveFileA(ansi_source.c_str(), ansi_destination.c_str()) != FALSE;
#else
    const std::filesystem::path native_source =
        BuildStormNativeLocalPath(source);
    const std::filesystem::path native_destination =
        BuildStormNativeLocalPath(destination);
    std::error_code ec;
    if (std::filesystem::exists(native_destination, ec) || ec) {
        return false;
    }
    std::filesystem::rename(native_source, native_destination, ec);
    return !ec;
#endif
}

bool QueryStormPathFreeBytesAvailable(const char* path,
                                      std::uint64_t* out_free_bytes) {
    if (out_free_bytes == nullptr) {
        return false;
    }

    const auto directory = BuildStormDiskSpaceDirectoryPath(path);
    if (directory[0] == '\0') {
        StormSetLastError(8);
        return false;
    }

#if defined(_WIN32)
    const std::string ansi_directory =
        Utf8ToCurrentCodePageString(directory.data());
    if (ansi_directory.empty()) {
        StormSetLastError(8);
        return false;
    }

    ULARGE_INTEGER free_bytes_available_to_caller{};
    ULARGE_INTEGER total_number_of_bytes{};
    CHAR short_path[kStormDiskSpacePathCapacity] = {};
    const DWORD short_path_length = ::GetShortPathNameA(
        ansi_directory.c_str(), short_path,
        static_cast<DWORD>(sizeof(short_path)));
    const char* const query_path =
        short_path_length != 0 && short_path_length < sizeof(short_path)
            ? short_path
            : ansi_directory.c_str();

    if (!::GetDiskFreeSpaceExA(query_path, &free_bytes_available_to_caller,
                               &total_number_of_bytes, nullptr)) {
        StormSetLastError(8);
        return false;
    }

    *out_free_bytes = free_bytes_available_to_caller.QuadPart;
    return true;
#else
    std::error_code ec;
    const std::filesystem::space_info info =
        std::filesystem::space(BuildStormNativeLocalPath(directory.data()), ec);
    if (ec) {
        StormSetLastError(8);
        return false;
    }

    *out_free_bytes = info.available;
    return true;
#endif
}

void SErrAssertHandler_NormalizePath(const char* input, char* output,
                                      uint32_t output_size) {

    if (!output || output_size == 0) return;

    char temp[512];
    uint32_t i = 0;

    if (input) {
        while (*input && i < 511) {
            char c = *input++;
            if (c == '\\') c = '/';
            temp[i++] = c;
        }
    }
    temp[i] = '\0';

    uint32_t copy_len = std::min(i + 1, output_size);
    std::memcpy(output, temp, copy_len);
    output[output_size - 1] = '\0';
}

void* SFileOpenFileEx_CheckMagic(void* handle) {
    auto* wrapper =
        static_cast<SFileOpenFileExForwardingCompat*>(handle);
    if (!wrapper) {
        return nullptr;
    }

    if (QuerySFileOpenFileExTypeTag(wrapper)
        != openwow::vfs::kIOUnitContainerTag) {
        return nullptr;
    }

    auto* lock_wrapper =
        static_cast<SFileOpenFileExForwardingCompat*>(wrapper->inner);
    if (!lock_wrapper) {
        return nullptr;
    }

    if (QuerySFileOpenFileExTypeTag(lock_wrapper)
        != openwow::vfs::kIOUnitContainerLockTag) {
        return nullptr;
    }

    return lock_wrapper->inner;
}

bool SFileOpenFileEx_ValidatedDelegate(void* handle, void* param) {
    auto* wrapper =
        static_cast<SFileOpenFileExForwardingCompat*>(handle);
    if (!wrapper || !param) {
        return false;
    }

    if (QuerySFileOpenFileExTypeTag(wrapper)
        != openwow::vfs::kIOUnitContainerTag) {
        return false;
    }

    return openwow::vfs::IOUnitContainerLock_WrapInner(wrapper->inner, param);
}

bool GetExeDirectory(char* output, uint32_t output_size) {
    if (output_size == 0) {
        return false;
    }

    std::string executable_path;
    if (!QueryExecutableFullPathUtf8(executable_path)) {
        return false;
    }

    std::array<char, kStormLocalPathCapacity> stack_buffer{};
    std::vector<char> heap_buffer;
    char* bounded_path = stack_buffer.data();
    if (output_size > stack_buffer.size()) {
        heap_buffer.resize(output_size);
        bounded_path = heap_buffer.data();
    }

    SStrCopy(bounded_path, executable_path.c_str(), output_size);
    if (output != nullptr) {
        const char* const leaf = FindStormPathLeafName(bounded_path);
        const auto directory_capacity = std::min<std::size_t>(
            output_size, static_cast<std::size_t>(leaf - bounded_path + 1));
        SStrCopy(output, bounded_path, directory_capacity);
    }

    return true;
}

namespace detail {

void SetGetExeDirectoryFullPathProviderForTests(
    GetExeDirectoryFullPathProvider provider) {
    MutableGetExeDirectoryFullPathProviderForTests() = provider;
}

void ResetGetExeDirectoryFullPathProviderForTests() {
    MutableGetExeDirectoryFullPathProviderForTests() = nullptr;
}

}

}
