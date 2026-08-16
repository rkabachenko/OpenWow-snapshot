
#pragma once

#include "openwow/audio/codecs/audio_decoder.h"
#include "openwow/audio/resources/sound_data.h"

#include <cstdint>
#include <vector>

namespace openwow::audio {

class WavDecoder final : public IAudioDecoder {
public:
    WavDecoder() = default;
    ~WavDecoder() override = default;

    bool Open(const std::vector<std::uint8_t>& data) override;
    std::size_t Decode(float* output, std::size_t frames) override;
    [[nodiscard]] std::uint32_t GetSampleRate() const override;
    [[nodiscard]] std::uint32_t GetChannels() const override;
    [[nodiscard]] std::uint64_t GetTotalFrames() const override;
    bool Seek(std::uint64_t frame) override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] AudioFormat GetFormat() const override { return AudioFormat::WAV; }

    void Reset();

    [[nodiscard]] std::uint64_t GetCurrentFrame() const;

    [[nodiscard]] bool IsFinished() const;

    [[nodiscard]] double GetDurationSeconds() const;

    [[nodiscard]] double GetPositionSeconds() const;

    [[nodiscard]] float GetProgress() const;

    [[nodiscard]] std::size_t GetTotalSamples() const;

    [[nodiscard]] std::size_t GetDataSizeBytes() const;

    [[nodiscard]] std::uint32_t GetBitRate() const;

    [[nodiscard]] bool IsMono() const;
    [[nodiscard]] bool IsStereo() const;

private:
    SoundData decoded_;
    std::size_t pos_{0};
    bool open_{false};
};

}
