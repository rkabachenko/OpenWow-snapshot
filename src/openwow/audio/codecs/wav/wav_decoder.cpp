
#include "openwow/audio/codecs/wav/wav_decoder.h"

#include <algorithm>
#include <cstring>

namespace openwow::audio {

bool WavDecoder::Open(const std::vector<std::uint8_t>& data) {
    open_ = false;
    pos_ = 0;
    decoded_ = {};

    auto result = DecodeWav(data);
    if (!result.has_value() || result->Empty()) {
        return false;
    }

    decoded_ = std::move(result.value());
    open_ = true;
    return true;
}

std::size_t WavDecoder::Decode(float* output, std::size_t frames) {
    if (!open_ || output == nullptr || frames == 0) return 0;

    const std::size_t total = decoded_.TotalFrames();
    if (pos_ >= total) return 0;

    const std::size_t available = total - pos_;
    const std::size_t to_read = std::min(frames, available);
    const std::uint32_t ch = decoded_.channels;

    for (std::size_t f = 0; f < to_read; ++f) {
        for (std::uint32_t c = 0; c < ch; ++c) {
            const std::size_t src_idx = (pos_ + f) * ch + c;
            const std::size_t dst_idx = f * ch + c;
            output[dst_idx] = static_cast<float>(decoded_.samples[src_idx]) / 32768.0f;
        }
    }

    pos_ += to_read;
    return to_read;
}

std::uint32_t WavDecoder::GetSampleRate() const {
    return decoded_.sample_rate;
}

std::uint32_t WavDecoder::GetChannels() const {
    return decoded_.channels;
}

std::uint64_t WavDecoder::GetTotalFrames() const {
    return decoded_.TotalFrames();
}

bool WavDecoder::IsOpen() const {
    return open_;
}

bool WavDecoder::Seek(std::uint64_t frame) {
    if (!open_) return false;
    if (frame > decoded_.TotalFrames()) return false;
    pos_ = static_cast<std::size_t>(frame);
    return true;
}

void WavDecoder::Reset() {
    pos_ = 0;
}

std::uint64_t WavDecoder::GetCurrentFrame() const {
    return static_cast<std::uint64_t>(pos_);
}

bool WavDecoder::IsFinished() const {
    return !open_ || pos_ >= decoded_.TotalFrames();
}

double WavDecoder::GetDurationSeconds() const {
    if (!open_ || decoded_.sample_rate == 0) return 0.0;
    return static_cast<double>(decoded_.TotalFrames()) /
           static_cast<double>(decoded_.sample_rate);
}

double WavDecoder::GetPositionSeconds() const {
    if (!open_ || decoded_.sample_rate == 0) return 0.0;
    return static_cast<double>(pos_) /
           static_cast<double>(decoded_.sample_rate);
}

float WavDecoder::GetProgress() const {
    if (!open_) return 0.0f;
    auto total = decoded_.TotalFrames();
    if (total == 0) return 0.0f;
    return static_cast<float>(pos_) / static_cast<float>(total);
}

std::size_t WavDecoder::GetTotalSamples() const {
    return decoded_.samples.size();
}

std::size_t WavDecoder::GetDataSizeBytes() const {
    return decoded_.samples.size() * sizeof(int16_t);
}

std::uint32_t WavDecoder::GetBitRate() const {
    if (!open_) return 0;

    return 16u * decoded_.channels * decoded_.sample_rate;
}

bool WavDecoder::IsMono() const {
    return decoded_.channels == 1;
}

bool WavDecoder::IsStereo() const {
    return decoded_.channels == 2;
}

}
