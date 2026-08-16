#include "openwow/core/screenshot_jpeg_writer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace openwow::core {

namespace {

constexpr std::uint32_t kScreenshotJpegMinDimension = 8;
constexpr std::uint32_t kScreenshotJpegMaxDimension = 65500;
constexpr std::uint32_t kScreenshotJpegMinQuality = 10;
constexpr std::uint32_t kScreenshotJpegMaxQuality = 100;

constexpr std::array<std::uint8_t, 64> kZigZag = {
    0, 1, 5, 6, 14, 15, 27, 28,
    2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63,
};

constexpr std::uint8_t kLumaQuantTables[11][64] = {
    {20, 16, 25, 39, 50, 46, 62, 68, 16, 18, 23, 38, 38, 53, 65, 68, 25, 23, 31, 38, 53, 65, 68, 68, 39, 38, 38, 53, 65, 68, 68, 68, 50, 38, 53, 65, 68, 68, 68, 68, 46, 53, 65, 68, 68, 68, 68, 68, 62, 65, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68},
    {18, 14, 14, 21, 30, 35, 34, 39, 14, 16, 16, 19, 26, 24, 30, 39, 14, 16, 17, 21, 24, 34, 46, 62, 21, 19, 21, 26, 33, 48, 62, 65, 30, 26, 24, 33, 51, 65, 65, 65, 35, 24, 34, 48, 65, 65, 65, 65, 34, 30, 46, 62, 65, 65, 65, 65, 39, 39, 62, 65, 65, 65, 65, 65},
    {16, 11, 11, 16, 23, 27, 31, 30, 11, 12, 12, 15, 20, 23, 23, 30, 11, 12, 13, 16, 23, 26, 35, 47, 16, 15, 16, 23, 26, 37, 47, 64, 23, 20, 23, 26, 39, 51, 64, 64, 27, 23, 26, 37, 51, 64, 64, 64, 31, 23, 35, 47, 64, 64, 64, 64, 30, 30, 47, 64, 64, 64, 64, 64},
    {12, 8, 8, 12, 17, 21, 24, 23, 8, 9, 9, 11, 15, 19, 18, 23, 8, 9, 10, 12, 19, 20, 27, 36, 12, 11, 12, 21, 20, 28, 36, 53, 17, 15, 19, 20, 30, 39, 51, 59, 21, 19, 20, 28, 39, 51, 59, 59, 24, 18, 27, 36, 51, 59, 59, 59, 23, 23, 36, 53, 59, 59, 59, 59},
    {8, 6, 6, 8, 12, 14, 16, 17, 6, 6, 6, 8, 10, 13, 12, 15, 6, 6, 7, 8, 13, 14, 18, 24, 8, 8, 8, 14, 13, 19, 24, 35, 12, 10, 13, 13, 20, 26, 34, 39, 14, 13, 14, 19, 26, 34, 39, 39, 16, 12, 18, 24, 34, 39, 39, 39, 17, 15, 24, 35, 39, 39, 39, 39},
    {6, 4, 4, 6, 9, 11, 12, 16, 4, 5, 5, 6, 8, 10, 12, 12, 4, 5, 5, 6, 10, 12, 14, 19, 6, 6, 6, 11, 12, 15, 19, 28, 9, 8, 10, 12, 16, 20, 27, 31, 11, 10, 12, 15, 20, 27, 31, 31, 12, 12, 14, 19, 27, 31, 31, 31, 16, 12, 19, 28, 31, 31, 31, 31},
    {4, 3, 3, 4, 6, 7, 8, 10, 3, 3, 3, 4, 5, 6, 8, 10, 3, 3, 3, 4, 6, 9, 12, 12, 4, 4, 4, 7, 9, 12, 12, 17, 6, 5, 6, 9, 12, 13, 17, 20, 7, 6, 9, 12, 13, 17, 20, 20, 8, 8, 12, 12, 17, 20, 20, 20, 10, 10, 12, 17, 20, 20, 20, 20},
    {2, 2, 2, 2, 3, 4, 5, 6, 2, 2, 2, 2, 3, 4, 5, 6, 2, 2, 2, 2, 4, 5, 7, 9, 2, 2, 2, 4, 5, 7, 9, 12, 3, 3, 4, 5, 8, 10, 12, 12, 4, 4, 5, 7, 10, 12, 12, 12, 5, 5, 7, 9, 12, 12, 12, 12, 6, 6, 9, 12, 12, 12, 12, 12},
    {1, 1, 1, 1, 2, 2, 2, 3, 1, 1, 1, 1, 2, 2, 2, 3, 1, 1, 1, 1, 2, 3, 4, 5, 1, 1, 1, 2, 3, 4, 5, 7, 2, 2, 2, 3, 4, 5, 7, 8, 2, 2, 3, 4, 5, 7, 8, 8, 2, 2, 4, 5, 7, 8, 8, 8, 3, 3, 5, 7, 8, 8, 8, 8},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {8, 5, 5, 8, 11, 13, 15, 17, 5, 6, 6, 7, 10, 12, 12, 15, 5, 6, 6, 8, 12, 13, 17, 23, 8, 7, 8, 13, 13, 18, 23, 34, 11, 10, 12, 13, 19, 25, 33, 38, 13, 12, 13, 18, 25, 33, 38, 38, 15, 12, 17, 23, 33, 38, 38, 38, 17, 15, 23, 34, 38, 38, 38, 38},
};

constexpr std::uint8_t kChromaQuantTables[11][64] = {
    {21, 25, 32, 38, 54, 68, 68, 68, 25, 28, 24, 38, 54, 68, 68, 68, 32, 24, 32, 43, 66, 68, 68, 68, 38, 38, 43, 53, 68, 68, 68, 68, 54, 54, 66, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68},
    {20, 19, 22, 27, 26, 33, 49, 62, 19, 25, 23, 22, 26, 33, 45, 56, 22, 23, 26, 29, 33, 39, 59, 65, 27, 22, 29, 36, 39, 51, 65, 65, 26, 26, 33, 39, 51, 62, 65, 65, 33, 33, 39, 51, 62, 65, 65, 65, 49, 45, 59, 65, 65, 65, 65, 65, 62, 56, 65, 65, 65, 65, 65, 65},
    {17, 15, 17, 21, 20, 26, 38, 48, 15, 19, 18, 17, 20, 26, 35, 43, 17, 18, 20, 22, 26, 30, 46, 53, 21, 17, 22, 28, 30, 39, 53, 64, 20, 20, 26, 30, 39, 48, 64, 64, 26, 26, 30, 39, 48, 63, 64, 64, 38, 35, 46, 53, 64, 64, 64, 64, 48, 43, 53, 64, 64, 64, 64, 64},
    {13, 11, 13, 16, 20, 20, 29, 37, 11, 14, 14, 14, 16, 20, 26, 32, 13, 14, 15, 17, 20, 23, 35, 40, 16, 14, 17, 21, 23, 30, 40, 50, 20, 16, 20, 23, 30, 37, 50, 59, 20, 20, 23, 30, 37, 48, 59, 59, 29, 26, 35, 40, 50, 59, 59, 59, 37, 32, 40, 50, 59, 59, 59, 59},
    {9, 8, 9, 11, 14, 17, 19, 24, 8, 10, 9, 11, 14, 13, 17, 22, 9, 9, 13, 14, 13, 15, 23, 26, 11, 11, 14, 14, 15, 20, 26, 33, 14, 14, 13, 15, 20, 24, 33, 39, 17, 13, 15, 20, 24, 32, 39, 39, 19, 17, 23, 26, 33, 39, 39, 39, 24, 22, 26, 33, 39, 39, 39, 39},
    {7, 7, 13, 24, 26, 31, 31, 31, 7, 12, 16, 21, 31, 31, 31, 31, 13, 16, 17, 31, 31, 31, 31, 31, 24, 21, 31, 31, 31, 31, 31, 31, 26, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31},
    {4, 5, 8, 15, 20, 20, 20, 20, 5, 7, 10, 14, 20, 20, 20, 20, 8, 10, 14, 20, 20, 20, 20, 20, 15, 14, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20},
    {3, 3, 5, 9, 13, 15, 15, 15, 3, 4, 6, 11, 14, 12, 12, 12, 5, 6, 9, 14, 12, 12, 12, 12, 9, 11, 14, 12, 12, 12, 12, 12, 13, 14, 12, 12, 12, 12, 12, 12, 15, 12, 12, 12, 12, 12, 12, 12, 15, 12, 12, 12, 12, 12, 12, 12, 15, 12, 12, 12, 12, 12, 12, 12},
    {1, 1, 2, 5, 7, 8, 8, 8, 1, 2, 3, 5, 8, 8, 8, 8, 2, 3, 4, 8, 8, 8, 8, 8, 5, 5, 8, 8, 8, 8, 8, 8, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {8, 9, 16, 29, 32, 38, 38, 38, 9, 14, 20, 26, 38, 38, 38, 38, 16, 20, 21, 38, 38, 38, 38, 38, 29, 26, 38, 38, 38, 38, 38, 38, 32, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38},
};

constexpr std::uint8_t kStdDcLuminanceNrCodes[17] =
    {0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kStdDcLuminanceValues[12] =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::uint8_t kStdAcLuminanceNrCodes[17] =
    {0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
constexpr std::uint8_t kStdAcLuminanceValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31,
    0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
    0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
    0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8,
    0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
    0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};
constexpr std::uint8_t kStdDcChrominanceNrCodes[17] =
    {0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0};
constexpr std::uint8_t kStdDcChrominanceValues[12] =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::uint8_t kStdAcChrominanceNrCodes[17] =
    {0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr std::uint8_t kStdAcChrominanceValues[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06,
    0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81,
    0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
    0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
    0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};

struct JpegHuffmanCode {
    unsigned short code = 0;
    unsigned short size = 0;
};

template <typename Sink>
class BufferedJpegDestinationWriter {
private:
    Sink& sink_;
    std::vector<std::uint8_t> buffer_;

public:
    BufferedJpegDestinationWriter(Sink& sink, const std::size_t buffer_size)
        : sink_(sink),
          buffer_(buffer_size, 0),
          ok(sink_.IsReady() && !buffer_.empty()) {
        if (ok) {
            ResetBuffer();
        }
    }

    void WriteByte(const std::uint8_t value) {
        if (!ok) {
            return;
        }

        *next_output_byte++ = value;
        --free_in_buffer;
        (void)FlushIfFilled();
    }

    void WriteData(const void* data, std::size_t size) {
        if (!ok || size == 0u) {
            return;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        while (size != 0u) {
            const std::size_t chunk = std::min(size, free_in_buffer);
            std::memcpy(next_output_byte, bytes, chunk);
            next_output_byte += chunk;
            free_in_buffer -= chunk;
            bytes += chunk;
            size -= chunk;

            if (!FlushIfFilled()) {
                return;
            }
        }
    }

    bool Finish() {
        if (!ok) {
            return false;
        }

        const std::size_t buffered_bytes = PendingBytes();
        if (buffered_bytes != 0u && !sink_.Write(buffer_.data(), buffered_bytes)) {
            ok = false;
            return false;
        }

        ResetBuffer();
        ok = sink_.Finish();
        return ok;
    }

    [[nodiscard]] std::size_t PendingBytes() const {
        return buffer_.size() - free_in_buffer;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& Buffer() const {
        return buffer_;
    }

    std::uint8_t* next_output_byte = nullptr;
    std::size_t free_in_buffer = 0;
    bool ok = false;

private:
    void ResetBuffer() {
        next_output_byte = buffer_.data();
        free_in_buffer = buffer_.size();
    }

    bool FlushIfFilled() {
        if (free_in_buffer != 0u) {
            return true;
        }

        if (!sink_.Write(buffer_.data(), buffer_.size())) {
            ok = false;
            return false;
        }

        ResetBuffer();
        return true;
    }

};

struct JpegFileSink {
    explicit JpegFileSink(const std::filesystem::path& output_path)
        : file(std::fopen(output_path.string().c_str(), "wb")) {}

    ~JpegFileSink() {
        if (file != nullptr) {
            std::fclose(file);
        }
    }

    [[nodiscard]] bool IsReady() const {
        return file != nullptr;
    }

    bool Write(const std::uint8_t* data, const std::size_t size) {
        return file != nullptr && std::fwrite(data, 1, size, file) == size;
    }

    bool Finish() {
        return file != nullptr && std::fflush(file) == 0 && std::ferror(file) == 0;
    }

    FILE* file = nullptr;
};

struct JpegFileDestinationWriter {
    static constexpr std::size_t kDestinationBufferSize = 4096u;

    explicit JpegFileDestinationWriter(const std::filesystem::path& output_path)
        : sink(output_path),
          writer(sink, kDestinationBufferSize),
          ok(writer.ok) {}

    void WriteByte(const std::uint8_t value) {
        writer.WriteByte(value);
        ok = writer.ok;
    }

    void WriteData(const void* data, const std::size_t size) {
        writer.WriteData(data, size);
        ok = writer.ok;
    }

    void WriteWord(const std::uint16_t value) {
        WriteByte(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        WriteByte(static_cast<std::uint8_t>(value & 0xFF));
    }

    bool Finish() {
        ok = writer.Finish();
        return ok;
    }

    JpegFileSink sink;
    BufferedJpegDestinationWriter<JpegFileSink> writer;
    bool ok = false;
};

using SampleBlock = std::array<int, 64>;
using QuantTable = std::array<std::uint8_t, 64>;
using QuantDivisorTable = std::array<int, 64>;
using HuffmanTable = std::array<JpegHuffmanCode, 256>;

struct JpegColorTransformTables {
    std::array<int, 256> red_to_y{};
    std::array<int, 256> green_to_y{};
    std::array<int, 256> blue_to_y{};
    std::array<int, 256> red_to_cb{};
    std::array<int, 256> green_to_cb{};
    std::array<int, 256> blue_to_cb{};
    std::array<int, 256> red_to_cr{};
    std::array<int, 256> green_to_cr{};
    std::array<int, 256> blue_to_cr{};
};

constexpr int kCenterSample = 128;
constexpr int kColorScaleBits = 16;
constexpr int kColorHalfRound = 1 << (kColorScaleBits - 1);
constexpr int kColorCbCrOffset = (kCenterSample << kColorScaleBits) + kColorHalfRound - 1;

constexpr int kFix0_298631336 = 2446;
constexpr int kFix0_390180644 = 3196;
constexpr int kFix0_541196100 = 4433;
constexpr int kFix0_765366865 = 6270;
constexpr int kFix0_899976223 = 7373;
constexpr int kFix1_175875602 = 9633;
constexpr int kFix1_501321110 = 12299;
constexpr int kFix1_847759065 = 15137;
constexpr int kFix1_961570560 = 16069;
constexpr int kFix2_053119869 = 16819;
constexpr int kFix2_562915447 = 20995;
constexpr int kFix3_072711026 = 25172;

template <typename ByteWriter>
bool EmitEntropyBits(ByteWriter& writer,
                     std::uint32_t& bit_buffer,
                     int& bit_count,
                     const JpegHuffmanCode& bits) {
    if (bits.size == 0u) {
        return false;
    }

    bit_count += bits.size;
    bit_buffer |= static_cast<std::uint32_t>(bits.code) << (24 - bit_count);

    while (bit_count >= 8) {
        const auto value = static_cast<std::uint8_t>((bit_buffer >> 16) & 0xFFu);
        writer.WriteByte(value);
        if (value == 0xFFu) {
            writer.WriteByte(0);
        }
        bit_buffer <<= 8;
        bit_count -= 8;
    }

    return true;
}

QuantTable ResolveQuantTable(const std::uint8_t (&tables)[11][64],
                             std::uint32_t quality) {
    quality = std::clamp(quality, 10u, 100u);

    QuantTable output{};
    if ((quality % 10u) == 0u) {
        const std::size_t table_index = (quality / 10u) - 1u;
        for (std::size_t i = 0; i < output.size(); ++i) {
            output[i] = tables[table_index][i];
        }
        return output;
    }

    const std::size_t upper_index = quality / 10u;
    const std::uint8_t* lower = tables[upper_index - 1u];
    const std::uint8_t* upper = tables[upper_index];
    if (quality > 50u && quality < 60u) {
        lower = tables[10];
    }

    const double factor = static_cast<double>(quality % 10u) * 0.1;
    for (std::size_t i = 0; i < output.size(); ++i) {
        const int delta = static_cast<int>(upper[i]) - static_cast<int>(lower[i]);
        output[i] = static_cast<std::uint8_t>(
            static_cast<int>(lower[i]) + static_cast<int>(delta * factor));
    }
    return output;
}

QuantTable ToZigZagOrder(const QuantTable& natural) {
    QuantTable zig_zag{};
    for (std::size_t i = 0; i < natural.size(); ++i) {
        zig_zag[kZigZag[i]] = natural[i];
    }
    return zig_zag;
}

QuantDivisorTable BuildSlowForwardDctDivisors(const QuantTable& natural_quant) {
    QuantDivisorTable divisors{};
    for (std::size_t i = 0; i < divisors.size(); ++i) {
        divisors[i] = static_cast<int>(natural_quant[i]) * 8;
    }
    return divisors;
}

JpegColorTransformTables BuildColorTransformTables() {
    JpegColorTransformTables tables{};
    int red_to_y = 0;
    int green_to_y = 0;
    int blue_to_y = kColorHalfRound;
    int red_to_cb = 0;
    int green_to_cb = 0;
    int blue_to_cb = kColorCbCrOffset;
    int red_to_cr = kColorCbCrOffset;
    int green_to_cr = 0;
    int blue_to_cr = 0;

    for (int sample = 0; sample < 256; ++sample) {
        tables.red_to_y[static_cast<std::size_t>(sample)] = red_to_y;
        tables.green_to_y[static_cast<std::size_t>(sample)] = green_to_y;
        tables.blue_to_y[static_cast<std::size_t>(sample)] = blue_to_y;
        tables.red_to_cb[static_cast<std::size_t>(sample)] = red_to_cb;
        tables.green_to_cb[static_cast<std::size_t>(sample)] = green_to_cb;
        tables.blue_to_cb[static_cast<std::size_t>(sample)] = blue_to_cb;
        tables.red_to_cr[static_cast<std::size_t>(sample)] = red_to_cr;
        tables.green_to_cr[static_cast<std::size_t>(sample)] = green_to_cr;
        tables.blue_to_cr[static_cast<std::size_t>(sample)] = blue_to_cr;

        red_to_y += 19595;
        green_to_y += 38470;
        blue_to_y += 7471;
        red_to_cb -= 11059;
        green_to_cb -= 21709;
        blue_to_cb += 32768;
        red_to_cr += 32768;
        green_to_cr -= 27439;
        blue_to_cr -= 5329;
    }

    return tables;
}

const JpegColorTransformTables& GetColorTransformTables() {
    static const JpegColorTransformTables tables = BuildColorTransformTables();
    return tables;
}

int ArithmeticRightShift(const int value, const int bits) {
    if (value >= 0) {
        return value >> bits;
    }

    const auto magnitude = static_cast<unsigned int>(-value);
    const auto bias = (1u << bits) - 1u;
    return -static_cast<int>((magnitude + bias) >> bits);
}

int Descale(const int value, const int bits) {
    return ArithmeticRightShift(value + (1 << (bits - 1)), bits);
}

detail::ScreenshotJpegYccSample ConvertRgbToYccSample(const std::uint8_t red,
                                                      const std::uint8_t green,
                                                      const std::uint8_t blue) {
    const auto& tables = GetColorTransformTables();
    return {
        .y = static_cast<std::uint8_t>(
            ArithmeticRightShift(tables.red_to_y[red]
                               + tables.green_to_y[green]
                               + tables.blue_to_y[blue],
                                 kColorScaleBits)),
        .cb = static_cast<std::uint8_t>(
            ArithmeticRightShift(tables.red_to_cb[red]
                               + tables.green_to_cb[green]
                               + tables.blue_to_cb[blue],
                                 kColorScaleBits)),
        .cr = static_cast<std::uint8_t>(
            ArithmeticRightShift(tables.red_to_cr[red]
                               + tables.green_to_cr[green]
                               + tables.blue_to_cr[blue],
                                 kColorScaleBits)),
    };
}

int QuantizeCoefficient(const int value, const int divisor) {
    const int half = divisor >> 1;
    if (value >= 0) {
        const int rounded = value + half;
        if (rounded < divisor) {
            return 0;
        }
        return rounded / divisor;
    }

    const int rounded = half - value;
    if (rounded < divisor) {
        return 0;
    }
    return -(rounded / divisor);
}

void ApplySlowForwardDct(SampleBlock& block) {
    for (int row = 0; row < 8; ++row) {
        int* data = block.data() + row * 8;

        const int tmp0 = data[0] + data[7];
        const int tmp7 = data[0] - data[7];
        const int tmp1 = data[1] + data[6];
        const int tmp6 = data[1] - data[6];
        const int tmp2 = data[2] + data[5];
        const int tmp5 = data[2] - data[5];
        const int tmp3 = data[3] + data[4];
        const int tmp4 = data[3] - data[4];

        const int tmp10 = tmp0 + tmp3;
        const int tmp11 = tmp1 + tmp2;
        const int tmp12 = tmp1 - tmp2;
        const int tmp13 = tmp0 - tmp3;

        data[0] = 4 * (tmp10 + tmp11);
        data[4] = 4 * (tmp10 - tmp11);
        data[2] = Descale(
            kFix0_541196100 * (tmp12 + tmp13) + kFix0_765366865 * tmp12, 11);
        data[6] = Descale(
            kFix0_541196100 * (tmp12 + tmp13) - kFix1_847759065 * tmp13, 11);

        const int odd_sum = kFix1_175875602 * (tmp6 + tmp4 + tmp5 + tmp7);
        const int odd0 = odd_sum - kFix1_961570560 * (tmp6 + tmp4);
        const int odd1 = odd_sum - kFix0_390180644 * (tmp5 + tmp7);

        data[7] = Descale(
            odd0 + kFix0_298631336 * tmp4 - kFix0_899976223 * (tmp7 + tmp4), 11);
        data[5] = Descale(
            odd1 + kFix2_053119869 * tmp5 - kFix2_562915447 * (tmp5 + tmp6), 11);
        data[3] = Descale(
            odd0 + kFix3_072711026 * tmp6 - kFix2_562915447 * (tmp5 + tmp6), 11);
        data[1] = Descale(
            odd1 + kFix1_501321110 * tmp7 - kFix0_899976223 * (tmp7 + tmp4), 11);
    }

    for (int col = 0; col < 8; ++col) {
        int* data = block.data() + col;

        const int tmp0 = data[0] + data[56];
        const int tmp7 = data[0] - data[56];
        const int tmp1 = data[8] + data[48];
        const int tmp6 = data[8] - data[48];
        const int tmp2 = data[16] + data[40];
        const int tmp5 = data[16] - data[40];
        const int tmp3 = data[24] + data[32];
        const int tmp4 = data[24] - data[32];

        const int tmp10 = tmp0 + tmp3;
        const int tmp11 = tmp1 + tmp2;
        const int tmp12 = tmp1 - tmp2;
        const int tmp13 = tmp0 - tmp3;

        data[0] = ArithmeticRightShift(tmp10 + tmp11 + 2, 2);
        data[32] = ArithmeticRightShift(tmp10 - tmp11 + 2, 2);
        data[16] = Descale(
            kFix0_541196100 * (tmp12 + tmp13) + kFix0_765366865 * tmp12, 15);
        data[48] = Descale(
            kFix0_541196100 * (tmp12 + tmp13) - kFix1_847759065 * tmp13, 15);

        const int odd_sum = kFix1_175875602 * (tmp6 + tmp4 + tmp5 + tmp7);
        const int odd0 = odd_sum - kFix1_961570560 * (tmp6 + tmp4);
        const int odd1 = odd_sum - kFix0_390180644 * (tmp5 + tmp7);

        data[56] = Descale(
            odd0 + kFix0_298631336 * tmp4 - kFix0_899976223 * (tmp7 + tmp4), 15);
        data[40] = Descale(
            odd1 + kFix2_053119869 * tmp5 - kFix2_562915447 * (tmp5 + tmp6), 15);
        data[24] = Descale(
            odd0 + kFix3_072711026 * tmp6 - kFix2_562915447 * (tmp5 + tmp6), 15);
        data[8] = Descale(
            odd1 + kFix1_501321110 * tmp7 - kFix0_899976223 * (tmp7 + tmp4), 15);
    }
}

void ConvertBgraBlockToYCbCrSamples(const std::vector<std::uint8_t>& bgra_pixels,
                                    const std::uint32_t width,
                                    const std::uint32_t height,
                                    const std::uint32_t block_x,
                                    const std::uint32_t block_y,
                                    SampleBlock& luma,
                                    SampleBlock& cb,
                                    SampleBlock& cr) {
    for (std::uint32_t row = 0; row < 8; ++row) {

        const std::uint32_t clamped_row = std::min(block_y + row, height - 1u);
        const std::size_t base = static_cast<std::size_t>(clamped_row) * width * 4u;

        for (std::uint32_t col = 0; col < 8; ++col) {
            const std::uint32_t clamped_col = std::min(block_x + col, width - 1u);
            const std::size_t pixel = base + static_cast<std::size_t>(clamped_col) * 4u;
            const auto blue = bgra_pixels[pixel + 0];
            const auto green = bgra_pixels[pixel + 1];
            const auto red = bgra_pixels[pixel + 2];
            const std::size_t index = static_cast<std::size_t>(row) * 8u + col;
            const auto ycc = ConvertRgbToYccSample(red, green, blue);

            luma[index] = static_cast<int>(ycc.y) - kCenterSample;
            cb[index] = static_cast<int>(ycc.cb) - kCenterSample;
            cr[index] = static_cast<int>(ycc.cr) - kCenterSample;
        }
    }
}

void BuildHuffmanTable(const std::uint8_t* nr_codes,
                       const std::uint8_t* values,
                       HuffmanTable& table) {
    std::uint16_t code_value = 0;
    int value_index = 0;

    for (int bit_length = 1; bit_length <= 16; ++bit_length) {
        for (int count = 0; count < nr_codes[bit_length]; ++count) {
            table[values[value_index]].code = code_value;
            table[values[value_index]].size = static_cast<unsigned short>(bit_length);
            ++value_index;
            ++code_value;
        }
        code_value <<= 1;
    }
}

bool FlushEntropyState(JpegFileDestinationWriter& writer,
                       std::uint32_t& bit_buffer,
                       int& bit_count) {

    static constexpr JpegHuffmanCode kFillBits{0x7F, 7};
    if (!EmitEntropyBits(writer, bit_buffer, bit_count, kFillBits) || !writer.ok) {
        return false;
    }
    bit_buffer = 0;
    bit_count = 0;
    return true;
}

JpegHuffmanCode CalcBits(int value) {
    int magnitude = value < 0 ? -value : value;
    value = value < 0 ? value - 1 : value;

    JpegHuffmanCode bits{};
    bits.size = 1;
    while ((magnitude >>= 1) != 0) {
        ++bits.size;
    }
    bits.code = static_cast<unsigned short>(value & ((1 << bits.size) - 1));
    return bits;
}

bool ProcessDataUnit(JpegFileDestinationWriter& writer,
                     std::uint32_t& bit_buffer,
                     int& bit_count,
                     SampleBlock& block,
                     const QuantDivisorTable& divisors,
                     int previous_dc,
                     const HuffmanTable& dc_huffman,
                     const HuffmanTable& ac_huffman,
                     int& next_dc) {
    std::array<int, 64> du{};

    ApplySlowForwardDct(block);

    for (std::size_t i = 0; i < block.size(); ++i) {
        du[kZigZag[i]] = QuantizeCoefficient(block[i], divisors[i]);
    }

    const int diff = du[0] - previous_dc;
    if (diff == 0) {
        if (!EmitEntropyBits(writer, bit_buffer, bit_count, dc_huffman[0]) || !writer.ok) {
            return false;
        }
    } else {
        const auto bits = CalcBits(diff);
        if (!EmitEntropyBits(writer, bit_buffer, bit_count, dc_huffman[bits.size])
            || !writer.ok
            || !EmitEntropyBits(writer, bit_buffer, bit_count, bits)
            || !writer.ok) {
            return false;
        }
    }

    int end0pos = 63;
    while (end0pos > 0 && du[end0pos] == 0) {
        --end0pos;
    }
    if (end0pos == 0) {
        if (!EmitEntropyBits(writer, bit_buffer, bit_count, ac_huffman[0x00]) || !writer.ok) {
            return false;
        }
        next_dc = du[0];
        return true;
    }

    for (int i = 1; i <= end0pos; ++i) {
        const int start = i;
        while (i <= end0pos && du[i] == 0) {
            ++i;
        }

        int zeroes = i - start;
        while (zeroes >= 16) {
            if (!EmitEntropyBits(writer, bit_buffer, bit_count, ac_huffman[0xF0]) || !writer.ok) {
                return false;
            }
            zeroes -= 16;
        }

        const auto bits = CalcBits(du[i]);
        if (!EmitEntropyBits(writer, bit_buffer, bit_count,
                             ac_huffman[(zeroes << 4) + bits.size])
            || !writer.ok
            || !EmitEntropyBits(writer, bit_buffer, bit_count, bits)
            || !writer.ok) {
            return false;
        }
    }

    if (end0pos != 63) {
        if (!EmitEntropyBits(writer, bit_buffer, bit_count, ac_huffman[0x00]) || !writer.ok) {
            return false;
        }
    }

    next_dc = du[0];
    return true;
}

bool WriteJfifHeaders(JpegFileDestinationWriter& writer,
                      std::uint16_t width,
                      std::uint16_t height,
                      const QuantTable& luma_zig_zag,
                      const QuantTable& chroma_zig_zag,
                      std::string_view comment) {
    static constexpr std::array<std::uint8_t, 3> kFrameQuantizationSelectors = {
        0x00, 0x01, 0x01,
    };
    static constexpr std::array<std::uint8_t, 3> kScanHuffmanSelectors = {
        0x00, 0x11, 0x11,
    };
    static constexpr std::uint8_t kApp0[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
        'J',  'F',  'I',  'F', 0x00, 0x01, 0x01, 0x00,
        0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    };
    static constexpr std::uint8_t kStartOfFramePrefix[] = {
        0xFF, 0xC0, 0x00, 0x11, 0x08,
    };
    static constexpr std::uint8_t kStartOfScan[] = {
        0xFF, 0xDA, 0x00, 0x0C,
        0x03,
        0x01, 0x00,
        0x02, 0x11,
        0x03, 0x11,
        0x00, 0x3F, 0x00,
    };

    const auto emit_quantization_table = [&writer](const std::uint8_t table_id,
                                                   const QuantTable& zig_zag_table) {
        writer.WriteByte(0xFF);
        writer.WriteByte(0xDB);
        writer.WriteWord(0x0043);
        writer.WriteByte(table_id);
        writer.WriteData(zig_zag_table.data(), zig_zag_table.size());
    };
    const auto emit_huffman_table = [&writer](const std::uint8_t table_info,
                                              const std::uint8_t* code_counts,
                                              const std::size_t value_count,
                                              const std::uint8_t* values) {
        writer.WriteByte(0xFF);
        writer.WriteByte(0xC4);
        writer.WriteWord(static_cast<std::uint16_t>(value_count + 19u));
        writer.WriteByte(table_info);
        writer.WriteData(code_counts + 1, 16);
        writer.WriteData(values, value_count);
    };

    writer.WriteData(kApp0, sizeof(kApp0));

    std::array<bool, 2> emitted_quant_tables = {false, false};
    for (const std::uint8_t table_id : kFrameQuantizationSelectors) {
        if (table_id >= emitted_quant_tables.size() || emitted_quant_tables[table_id]) {
            continue;
        }

        emit_quantization_table(
            table_id, table_id == 0 ? luma_zig_zag : chroma_zig_zag);
        emitted_quant_tables[table_id] = true;
    }

    writer.WriteData(kStartOfFramePrefix, sizeof(kStartOfFramePrefix));
    writer.WriteWord(height);
    writer.WriteWord(width);
    writer.WriteByte(0x03);
    writer.WriteByte(0x01);
    writer.WriteByte(0x11);
    writer.WriteByte(0x00);
    writer.WriteByte(0x02);
    writer.WriteByte(0x11);
    writer.WriteByte(0x01);
    writer.WriteByte(0x03);
    writer.WriteByte(0x11);
    writer.WriteByte(0x01);

    std::array<bool, 2> emitted_dc_tables = {false, false};
    std::array<bool, 2> emitted_ac_tables = {false, false};
    for (const std::uint8_t selector : kScanHuffmanSelectors) {
        const std::uint8_t dc_table_id = static_cast<std::uint8_t>((selector >> 4) & 0x0F);
        if (dc_table_id < emitted_dc_tables.size() && !emitted_dc_tables[dc_table_id]) {
            if (dc_table_id == 0) {
                emit_huffman_table(
                    0x00,
                    kStdDcLuminanceNrCodes,
                    sizeof(kStdDcLuminanceValues),
                    kStdDcLuminanceValues);
            } else {
                emit_huffman_table(
                    0x01,
                    kStdDcChrominanceNrCodes,
                    sizeof(kStdDcChrominanceValues),
                    kStdDcChrominanceValues);
            }
            emitted_dc_tables[dc_table_id] = true;
        }

        const std::uint8_t ac_table_id = static_cast<std::uint8_t>(selector & 0x0F);
        if (ac_table_id < emitted_ac_tables.size() && !emitted_ac_tables[ac_table_id]) {
            if (ac_table_id == 0) {
                emit_huffman_table(
                    0x10,
                    kStdAcLuminanceNrCodes,
                    sizeof(kStdAcLuminanceValues),
                    kStdAcLuminanceValues);
            } else {
                emit_huffman_table(
                    0x11,
                    kStdAcChrominanceNrCodes,
                    sizeof(kStdAcChrominanceValues),
                    kStdAcChrominanceValues);
            }
            emitted_ac_tables[ac_table_id] = true;
        }
    }

    if (!comment.empty()) {
        constexpr std::size_t kMaxMarkerPayloadSize = 0xFFFDu;
        if (comment.size() > kMaxMarkerPayloadSize) {
            return false;
        }
        writer.WriteByte(0xFF);
        writer.WriteByte(0xFE);
        writer.WriteWord(static_cast<std::uint16_t>(comment.size() + 2u));
        writer.WriteData(comment.data(), comment.size());
    }

    writer.WriteData(kStartOfScan, sizeof(kStartOfScan));
    return writer.ok;
}

}

bool WriteWotlkScreenshotJpeg(const std::filesystem::path& path,
                              const std::vector<std::uint8_t>& bgra_pixels,
                              std::uint32_t width,
                              std::uint32_t height,
                              std::uint32_t quality,
                              std::string_view comment) {

    if (bgra_pixels.empty()
        || width < kScreenshotJpegMinDimension
        || height < kScreenshotJpegMinDimension
        || width > kScreenshotJpegMaxDimension
        || height > kScreenshotJpegMaxDimension
        || quality < kScreenshotJpegMinQuality
        || quality > kScreenshotJpegMaxQuality) {
        return false;
    }

    const std::uint64_t expected_size =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;
    if (bgra_pixels.size() < expected_size) {
        return false;
    }

    const QuantTable luma_natural = ResolveQuantTable(kLumaQuantTables, quality);
    const QuantTable chroma_natural = ResolveQuantTable(kChromaQuantTables, quality);
    const QuantTable luma_zig_zag = ToZigZagOrder(luma_natural);
    const QuantTable chroma_zig_zag = ToZigZagOrder(chroma_natural);
    const QuantDivisorTable luma_divisors = BuildSlowForwardDctDivisors(luma_natural);
    const QuantDivisorTable chroma_divisors = BuildSlowForwardDctDivisors(chroma_natural);

    HuffmanTable dc_luma{};
    HuffmanTable ac_luma{};
    HuffmanTable dc_chroma{};
    HuffmanTable ac_chroma{};
    BuildHuffmanTable(kStdDcLuminanceNrCodes, kStdDcLuminanceValues, dc_luma);
    BuildHuffmanTable(kStdAcLuminanceNrCodes, kStdAcLuminanceValues, ac_luma);
    BuildHuffmanTable(kStdDcChrominanceNrCodes, kStdDcChrominanceValues, dc_chroma);
    BuildHuffmanTable(kStdAcChrominanceNrCodes, kStdAcChrominanceValues, ac_chroma);

    JpegFileDestinationWriter writer(path);
    if (!writer.ok) {
        return false;
    }

    if (!WriteJfifHeaders(writer, static_cast<std::uint16_t>(width),
                          static_cast<std::uint16_t>(height),
                          luma_zig_zag, chroma_zig_zag, comment)) {
        return false;
    }

    std::uint32_t bit_buffer = 0;
    int bit_count = 0;
    int dc_y = 0;
    int dc_u = 0;
    int dc_v = 0;
    SampleBlock block_y{};
    SampleBlock block_u{};
    SampleBlock block_v{};

    for (std::uint32_t y = 0; y < height; y += 8) {
        for (std::uint32_t x = 0; x < width; x += 8) {
            ConvertBgraBlockToYCbCrSamples(
                bgra_pixels, width, height, x, y, block_y, block_u, block_v);
            if (!ProcessDataUnit(writer, bit_buffer, bit_count, block_y,
                                 luma_divisors, dc_y, dc_luma, ac_luma, dc_y)
                || !ProcessDataUnit(writer, bit_buffer, bit_count, block_u,
                                    chroma_divisors, dc_u, dc_chroma, ac_chroma, dc_u)
                || !ProcessDataUnit(writer, bit_buffer, bit_count, block_v,
                                    chroma_divisors, dc_v, dc_chroma, ac_chroma, dc_v)
                || !writer.ok) {
                return false;
            }
        }
    }

    if (!FlushEntropyState(writer, bit_buffer, bit_count)) {
        return false;
    }
    writer.WriteByte(0xFF);
    writer.WriteByte(0xD9);
    if (!writer.ok) {
        return false;
    }

    return writer.Finish();
}

bool detail::EmitScreenshotJpegEntropyBitsForTests(
    std::vector<std::uint8_t>& output,
    std::uint32_t& bit_buffer,
    int& bit_count,
    ScreenshotJpegEntropyCode bits) {
    struct VectorByteWriter {
        explicit VectorByteWriter(std::vector<std::uint8_t>& target_bytes)
            : target(target_bytes) {}

        void WriteByte(std::uint8_t value) {
            target.push_back(value);
        }

        std::vector<std::uint8_t>& target;
    };

    VectorByteWriter writer(output);
    return EmitEntropyBits(writer, bit_buffer, bit_count,
                           JpegHuffmanCode{bits.code, bits.size});
}

namespace {

struct BufferedWriteTraceSink {
    [[nodiscard]] bool IsReady() const {
        return true;
    }

    bool Write(const std::uint8_t* data, const std::size_t size) {
        flush_sizes.push_back(size);
        flushed_bytes.insert(flushed_bytes.end(), data, data + size);
        return true;
    }

    [[nodiscard]] bool Finish() const {
        return true;
    }

    std::vector<std::size_t> flush_sizes;
    std::vector<std::uint8_t> flushed_bytes;
};

[[nodiscard]] detail::ScreenshotJpegBufferedWriteTrace BuildBufferedWriteTrace(
    const BufferedWriteTraceSink& sink,
    const BufferedJpegDestinationWriter<BufferedWriteTraceSink>& writer) {
    detail::ScreenshotJpegBufferedWriteTrace trace;
    trace.flush_sizes = sink.flush_sizes;
    trace.flushed_bytes = sink.flushed_bytes;
    trace.pending_bytes.assign(writer.Buffer().begin(),
                               writer.Buffer().begin() + writer.PendingBytes());
    trace.free_in_buffer = writer.free_in_buffer;
    trace.ok = writer.ok;
    return trace;
}

}

detail::ScreenshotJpegBufferedWriteTrace
detail::TraceScreenshotJpegBufferedByteWritesForTests(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t buffer_size) {
    BufferedWriteTraceSink sink;
    BufferedJpegDestinationWriter<BufferedWriteTraceSink> writer(sink, buffer_size);

    for (const std::uint8_t byte : bytes) {
        writer.WriteByte(byte);
    }

    return BuildBufferedWriteTrace(sink, writer);
}

detail::ScreenshotJpegBufferedWriteTrace
detail::TraceScreenshotJpegBufferedDataWriteForTests(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t buffer_size) {
    BufferedWriteTraceSink sink;
    BufferedJpegDestinationWriter<BufferedWriteTraceSink> writer(sink, buffer_size);
    writer.WriteData(bytes.data(), bytes.size());
    return BuildBufferedWriteTrace(sink, writer);
}

std::array<int, 64> detail::BuildScreenshotJpegSlowDivisorsForTests(
    const std::array<std::uint8_t, 64>& natural_quant) {
    return BuildSlowForwardDctDivisors(natural_quant);
}

std::array<int, 64> detail::ProcessScreenshotJpegSlowBlockForTests(
    const std::array<int, 64>& level_shifted_samples,
    const std::array<std::uint8_t, 64>& natural_quant) {
    SampleBlock block = level_shifted_samples;
    const QuantDivisorTable divisors = BuildSlowForwardDctDivisors(natural_quant);

    ApplySlowForwardDct(block);

    std::array<int, 64> quantized_coefficients{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        quantized_coefficients[i] = QuantizeCoefficient(block[i], divisors[i]);
    }

    return quantized_coefficients;
}

int detail::QuantizeScreenshotJpegCoefficientForTests(
    const int value,
    const int divisor) {
    return QuantizeCoefficient(value, divisor);
}

detail::ScreenshotJpegYccSample detail::ConvertScreenshotJpegRgbToYccForTests(
    const std::uint8_t red,
    const std::uint8_t green,
    const std::uint8_t blue) {
    return ConvertRgbToYccSample(red, green, blue);
}

}
