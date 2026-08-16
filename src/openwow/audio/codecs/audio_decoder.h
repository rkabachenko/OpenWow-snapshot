
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::audio {

enum class AudioFormat : std::uint8_t {
    Unknown = 0,
    WAV     = 1,
    MP3     = 2,
    OGG     = 3,
    FLAC    = 4,
    WMA     = 5,
};

[[nodiscard]] const char* AudioFormatName(AudioFormat fmt);

class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    virtual bool Open(const std::vector<std::uint8_t>& data) = 0;

    virtual std::size_t Decode(float* output, std::size_t frames) = 0;

    [[nodiscard]] virtual std::uint32_t GetSampleRate() const = 0;

    [[nodiscard]] virtual std::uint32_t GetChannels() const = 0;

    [[nodiscard]] virtual std::uint64_t GetTotalFrames() const = 0;

    virtual bool Seek(std::uint64_t frame) = 0;

    [[nodiscard]] virtual bool IsOpen() const = 0;

    [[nodiscard]] virtual AudioFormat GetFormat() const = 0;
};

[[nodiscard]] AudioFormat DetectAudioFormat(const std::uint8_t* data, std::size_t size);

[[nodiscard]] AudioFormat DetectAudioFormat(const std::vector<std::uint8_t>& data);

[[nodiscard]] std::unique_ptr<IAudioDecoder> CreateAudioDecoder(
    const std::vector<std::uint8_t>& data);

[[nodiscard]] std::unique_ptr<IAudioDecoder> CreateAudioDecoder(
    AudioFormat format, const std::vector<std::uint8_t>& data);

}
