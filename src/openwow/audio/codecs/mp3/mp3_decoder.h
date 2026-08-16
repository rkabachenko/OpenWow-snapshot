
#pragma once

#include "openwow/audio/codecs/audio_decoder.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace openwow::audio {

class Mp3Decoder final : public IAudioDecoder {
public:
    Mp3Decoder();
    ~Mp3Decoder() override;

    Mp3Decoder(const Mp3Decoder&) = delete;
    Mp3Decoder& operator=(const Mp3Decoder&) = delete;
    Mp3Decoder(Mp3Decoder&&) noexcept;
    Mp3Decoder& operator=(Mp3Decoder&&) noexcept;

    bool Open(const std::vector<std::uint8_t>& data) override;

    bool OpenOwned(std::vector<std::uint8_t> data);
    std::size_t Decode(float* output, std::size_t frames) override;
    [[nodiscard]] std::uint32_t GetSampleRate() const override;
    [[nodiscard]] std::uint32_t GetChannels() const override;
    [[nodiscard]] std::uint64_t GetTotalFrames() const override;
    bool Seek(std::uint64_t frame) override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] AudioFormat GetFormat() const override { return AudioFormat::MP3; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
