
#include "openwow/audio/codecs/ogg/ogg_decompress.h"
#include "openwow/audio/codecs/ogg/ogg_logical_streams.h"
#include "openwow/audio/codecs/ogg/vorbis_header_parity.h"
#include "openwow/audio/codecs/ogg/vorbis_codebook_lookup.h"
#include "openwow/audio/codecs/ogg/vorbis_pcm_conversion.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.h"

#include <array>
#include <cstring>
#include <limits>
#include <memory>

namespace openwow::audio {

namespace {

constexpr std::size_t kWavHeaderBytes = 44;
constexpr std::size_t kDecodeChunkBytes = 8192;

struct StbVorbisCloser {
  void operator()(stb_vorbis *vorbis) const noexcept {
    if (vorbis) {
      stb_vorbis_close(vorbis);
    }
  }
};

using StbVorbisPtr = std::unique_ptr<stb_vorbis, StbVorbisCloser>;

bool AppendDecodedLogicalStream(const std::span<const std::uint8_t> stream,
                                std::uint32_t *sample_rate, std::uint16_t *channels,
                                std::vector<std::uint8_t> *pcm_out) {
  if (stream.empty() || !sample_rate || !channels || !pcm_out) {
    return false;
  }
  if (stream.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }

  int error = 0;
  StbVorbisPtr vorbis(
      stb_vorbis_open_memory(stream.data(), static_cast<int>(stream.size()), &error, nullptr));
  if (!vorbis || error != VORBIS__no_error) {
    return false;
  }

  const stb_vorbis_info info = stb_vorbis_get_info(vorbis.get());
  if ((info.channels != 1 && info.channels != 2) || info.sample_rate <= 0) {
    return false;
  }

  const auto stream_rate = static_cast<std::uint32_t>(info.sample_rate);
  const auto stream_channels = static_cast<std::uint16_t>(info.channels);
  if (*sample_rate == 0) {
    *sample_rate = stream_rate;
    *channels = stream_channels;
  } else if (*sample_rate != stream_rate || *channels != stream_channels) {
    return false;
  }

  std::array<std::uint8_t, kDecodeChunkBytes> chunk{};
  std::array<float *, 2> channel_pcm{};
  const std::size_t bytes_per_frame =
      static_cast<std::size_t>(stream_channels) * sizeof(std::int16_t);
  std::size_t chunk_fill = 0;

  const auto append_chunk = [&](const std::size_t bytes) {
    if (bytes == 0) {
      return true;
    }
    if (pcm_out->size() > std::numeric_limits<std::size_t>::max() - bytes) {
      return false;
    }
    pcm_out->insert(pcm_out->end(), chunk.begin(),
                    chunk.begin() + static_cast<std::ptrdiff_t>(bytes));
    return true;
  };

  while (true) {
    float **decoded_pcm = nullptr;
    const int frames = stb_vorbis_get_frame_float(vorbis.get(), nullptr, &decoded_pcm);
    if (frames < 0) {
      return false;
    }
    if (frames == 0) {
      break;
    }
    if (!decoded_pcm) {
      return false;
    }

    std::size_t frame_offset = 0;
    while (frame_offset < static_cast<std::size_t>(frames)) {
      for (std::uint32_t channel = 0; channel < stream_channels; ++channel) {
        channel_pcm[channel] = decoded_pcm[channel] + frame_offset;
      }

      const auto writable = std::span<std::uint8_t>(chunk).subspan(chunk_fill);
      const std::size_t written_frames = ConvertVorbisFrameToSignedPcm16Le(
          channel_pcm.data(), stream_channels, static_cast<std::size_t>(frames) - frame_offset,
          writable);
      if (written_frames == 0) {
        return false;
      }

      frame_offset += written_frames;
      chunk_fill += written_frames * bytes_per_frame;
      if (chunk_fill == chunk.size()) {
        if (!append_chunk(chunk_fill)) {
          return false;
        }
        chunk_fill = 0;
      }
    }
  }

  return append_chunk(chunk_fill);
}

void StoreLittleEndian16(std::uint8_t *const destination, const std::uint16_t value) {
  destination[0] = static_cast<std::uint8_t>(value);
  destination[1] = static_cast<std::uint8_t>(value >> 8);
}

void StoreLittleEndian32(std::uint8_t *const destination, const std::uint32_t value) {
  destination[0] = static_cast<std::uint8_t>(value);
  destination[1] = static_cast<std::uint8_t>(value >> 8);
  destination[2] = static_cast<std::uint8_t>(value >> 16);
  destination[3] = static_cast<std::uint8_t>(value >> 24);
}

void WriteWAVHeader(std::uint8_t *buf, std::uint32_t sample_rate, std::uint16_t channels,
                    std::uint32_t data_size) {

  std::memcpy(buf + 0, "RIFF", 4);
  StoreLittleEndian32(buf + 4, data_size + 36);
  std::memcpy(buf + 8, "WAVE", 4);

  std::memcpy(buf + 12, "fmt ", 4);
  StoreLittleEndian32(buf + 16, 16);
  StoreLittleEndian16(buf + 20, 1);
  StoreLittleEndian16(buf + 22, channels);
  StoreLittleEndian32(buf + 24, sample_rate);
  StoreLittleEndian32(buf + 28, sample_rate * channels * 2u);
  StoreLittleEndian16(buf + 32, static_cast<std::uint16_t>(channels * 2u));
  StoreLittleEndian16(buf + 34, 16);

  std::memcpy(buf + 36, "data", 4);
  StoreLittleEndian32(buf + 40, data_size);
}

}

bool OggVorbis_DecodeToWAV(const std::uint8_t *ogg_data, std::uint32_t ogg_size,
                           std::vector<std::uint8_t> &wav_out) {
  wav_out.clear();
  if (!ogg_data || ogg_size == 0) {
    return false;
  }

  std::vector<std::uint8_t> pcm_bytes;
  std::uint32_t sample_rate = 0;
  std::uint16_t channels = 0;
  std::size_t cursor = 0;
  bool decoded_any_stream = false;

  while (cursor < ogg_size) {
    std::vector<std::uint8_t> decode_stream;
    if (!ExtractNextVorbisDecodeStream(ogg_data, ogg_size, &cursor, &decode_stream) ||
        decode_stream.empty()) {
      wav_out.clear();
      return false;
    }

    std::span<const std::uint8_t> stream_bytes(decode_stream.data(), decode_stream.size());
    std::vector<std::uint8_t> canonical_stream;
    if (CanonicalizeOpeningOggPacketPrefix(stream_bytes, &canonical_stream)) {
      decode_stream = std::move(canonical_stream);
      stream_bytes = std::span<const std::uint8_t>(decode_stream.data(), decode_stream.size());
    }

    if (!ValidateVorbisHeaderParity(stream_bytes)) {
      wav_out.clear();
      return false;
    }
    std::vector<std::uint8_t> sanitized_stream;
    if (!SanitizeVorbisAudioPacketParity(stream_bytes, &sanitized_stream) ||
        sanitized_stream.empty()) {
      wav_out.clear();
      return false;
    }
    decode_stream = std::move(sanitized_stream);
    stream_bytes = std::span<const std::uint8_t>(decode_stream.data(), decode_stream.size());
    if (!AppendDecodedLogicalStream(stream_bytes, &sample_rate, &channels, &pcm_bytes)) {
      wav_out.clear();
      return false;
    }
    decoded_any_stream = true;
  }

  if (!decoded_any_stream || sample_rate == 0 || channels == 0) {
    wav_out.clear();
    return false;
  }
  if (pcm_bytes.size() > std::numeric_limits<std::uint32_t>::max() - kWavHeaderBytes) {
    wav_out.clear();
    return false;
  }

  wav_out.resize(kWavHeaderBytes + pcm_bytes.size());
  WriteWAVHeader(wav_out.data(), sample_rate, channels,
                 static_cast<std::uint32_t>(pcm_bytes.size()));
  if (!pcm_bytes.empty()) {
    std::memcpy(wav_out.data() + kWavHeaderBytes, pcm_bytes.data(), pcm_bytes.size());
  }
  return true;
}

}
