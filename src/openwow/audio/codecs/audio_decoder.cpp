
#include "openwow/audio/codecs/audio_decoder.h"
#include "openwow/audio/codecs/flac/flac_decoder.h"
#include "openwow/audio/codecs/mp3/mp3_decoder.h"
#include "openwow/audio/codecs/ogg/ogg_decoder.h"
#include "openwow/audio/codecs/wav/wav_decoder.h"
#include "openwow/audio/codecs/wma/wma_decoder.h"

#include <cstring>

namespace openwow::audio {

const char* AudioFormatName(AudioFormat fmt) {
    switch (fmt) {
        case AudioFormat::WAV: return "WAV";
        case AudioFormat::MP3: return "MP3";
        case AudioFormat::OGG: return "OGG";
        case AudioFormat::FLAC: return "FLAC";
        case AudioFormat::WMA: return "WMA";
        default:               return "Unknown";
    }
}

AudioFormat DetectAudioFormat(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 4) {
        return AudioFormat::Unknown;
    }

    if (size >= 12 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
        return AudioFormat::WAV;
    }

    if (data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S') {
        return AudioFormat::OGG;
    }

    if (size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        return AudioFormat::MP3;
    }

    if (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {

        const std::uint8_t layer = (data[1] >> 1) & 0x03;
        const std::uint8_t bitrate_idx = (data[2] >> 4) & 0x0F;
        const std::uint8_t srate_idx = (data[2] >> 2) & 0x03;
        if (layer != 0 && bitrate_idx != 0 && bitrate_idx != 0x0F && srate_idx != 3) {
            return AudioFormat::MP3;
        }
    }

    if (size > 4) {
        for (std::size_t i = 1; i + 2 < size; ++i) {
            if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0) {
                const std::uint8_t layer = (data[i + 1] >> 1) & 0x03;
                const std::uint8_t bitrate_idx = (data[i + 2] >> 4) & 0x0F;
                const std::uint8_t srate_idx = (data[i + 2] >> 2) & 0x03;
                if (layer != 0 && bitrate_idx != 0 && bitrate_idx != 0x0F &&
                    srate_idx != 3) {
                    return AudioFormat::MP3;
                }
            }
        }
    }

    if (size >= 4 &&
        data[0] == 'f' && data[1] == 'L' && data[2] == 'a' && data[3] == 'C') {
        return AudioFormat::FLAC;
    }

    if (size >= 16) {
        static const std::uint8_t kAsfGuid[16] = {
            0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
            0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C
        };
        if (std::memcmp(data, kAsfGuid, 16) == 0) {
            return AudioFormat::WMA;
        }
    }

    return AudioFormat::Unknown;
}

AudioFormat DetectAudioFormat(const std::vector<std::uint8_t>& data) {
    return DetectAudioFormat(data.data(), data.size());
}

std::unique_ptr<IAudioDecoder> CreateAudioDecoder(
    AudioFormat format, const std::vector<std::uint8_t>& data) {

    std::unique_ptr<IAudioDecoder> decoder;

    switch (format) {
        case AudioFormat::WAV:
            decoder = std::make_unique<WavDecoder>();
            break;
        case AudioFormat::MP3:
            decoder = std::make_unique<Mp3Decoder>();
            break;
        case AudioFormat::OGG:
            decoder = std::make_unique<OggDecoder>();
            break;
        case AudioFormat::FLAC:
            decoder = std::make_unique<FlacDecoder>();
            break;
        case AudioFormat::WMA:
            decoder = std::make_unique<WmaDecoder>();
            break;
        default:
            return nullptr;
    }

    if (!decoder->Open(data)) {
        return nullptr;
    }

    return decoder;
}

std::unique_ptr<IAudioDecoder> CreateAudioDecoder(
    const std::vector<std::uint8_t>& data) {

    const AudioFormat format = DetectAudioFormat(data);
    if (format == AudioFormat::Unknown) {
        return nullptr;
    }
    return CreateAudioDecoder(format, data);
}

}
