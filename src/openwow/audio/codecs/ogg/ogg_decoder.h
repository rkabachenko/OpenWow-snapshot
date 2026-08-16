
#pragma once

#include "openwow/audio/codecs/audio_decoder.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace openwow::audio {

class OggDecoder final : public IAudioDecoder {
public:
    OggDecoder();
    ~OggDecoder() override;

    OggDecoder(const OggDecoder&) = delete;
    OggDecoder& operator=(const OggDecoder&) = delete;
    OggDecoder(OggDecoder&&) noexcept;
    OggDecoder& operator=(OggDecoder&&) noexcept;

    bool Open(const std::vector<std::uint8_t>& data) override;
    std::size_t Decode(float* output, std::size_t frames) override;
    [[nodiscard]] std::uint32_t GetSampleRate() const override;
    [[nodiscard]] std::uint32_t GetChannels() const override;
    [[nodiscard]] std::uint64_t GetTotalFrames() const override;
    bool Seek(std::uint64_t frame) override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] AudioFormat GetFormat() const override { return AudioFormat::OGG; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
