
#include "openwow/audio/codecs/ogg/vorbis_codebook_lookup.h"

#define OPENWOW_STB_VORBIS_LOOKUP1_VALUES(entries, dim)                                            \
  openwow::audio::ComputeVorbisLookup1QuantValues((dim), (entries))
#define OPENWOW_STB_VORBIS_ILOG(value) openwow::audio::detail::ComputeVorbisIlog((value))
#define OPENWOW_STB_VORBIS_USE_STOCK_CODEBOOK_DECODE 1
#define STB_VORBIS_IMPLEMENTATION
#include "stb/stb_vorbis.h"
#undef OPENWOW_STB_VORBIS_ILOG
#undef OPENWOW_STB_VORBIS_LOOKUP1_VALUES
#undef OPENWOW_STB_VORBIS_USE_STOCK_CODEBOOK_DECODE

#include "openwow/audio/codecs/ogg/ogg_decoder.h"
#include "openwow/audio/codecs/ogg/ogg_logical_streams.h"
#include "openwow/audio/codecs/ogg/vorbis_header_parity.h"

#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace openwow::audio {

namespace {

struct StbVorbisCloser {
  void operator()(stb_vorbis *vorbis) const noexcept {
    if (vorbis) {
      stb_vorbis_close(vorbis);
    }
  }
};

using StbVorbisPtr = std::unique_ptr<stb_vorbis, StbVorbisCloser>;

struct LogicalStreamDecoder {
  std::vector<std::uint8_t> canonical_bytes;
  StbVorbisPtr vorbis;
  std::uint64_t first_frame{0};
  std::uint64_t frame_count{0};
};

bool ResetLogicalStream(LogicalStreamDecoder &stream) {
  return stream.vorbis && stb_vorbis_seek_start(stream.vorbis.get()) != 0;
}

std::optional<std::uint64_t>
ComputeLogicalStreamFrameCount(const std::span<const std::uint8_t> logical_stream) {
  if (logical_stream.empty()) {
    return std::nullopt;
  }

  OggPageParserState parser;
  parser.data = logical_stream.data();
  parser.buffered_bytes = logical_stream.size();

  std::optional<std::uint64_t> final_granule_position;
  while (parser.returned_bytes < logical_stream.size()) {
    OggPageView page;
    if (ParseOggPage(&parser, &page) <= 0) {
      return std::nullopt;
    }
    if (!HasOggPageEosFlag(page.page)) {
      continue;
    }

    const std::int64_t granule_position = ReadOggPageGranulePosition(page.page);
    if (granule_position < 0) {
      return std::nullopt;
    }
    final_granule_position = static_cast<std::uint64_t>(granule_position);
  }

  return final_granule_position;
}

std::size_t FindLogicalStreamForFrame(const std::vector<LogicalStreamDecoder> &streams,
                                      const std::uint64_t frame) {
  for (std::size_t i = 0; i < streams.size(); ++i) {
    const auto &stream = streams[i];
    if (frame < stream.first_frame + stream.frame_count) {
      return i;
    }
  }
  return streams.empty() ? 0 : streams.size() - 1;
}

bool SeekLogicalStreamToFrame(LogicalStreamDecoder &stream, const std::uint32_t channels,
                              const std::uint64_t local_frame) {
  if (local_frame == 0) {
    return true;
  }

  if (local_frame <= std::numeric_limits<unsigned int>::max() &&
      stb_vorbis_seek(stream.vorbis.get(), static_cast<unsigned int>(local_frame)) != 0) {
    return true;
  }

  if (!ResetLogicalStream(stream)) {
    return false;
  }

  constexpr std::uint64_t kDiscardFramesPerChunk = 4096;
  std::vector<float> discard_buffer(kDiscardFramesPerChunk * channels);
  std::uint64_t remaining_frames = local_frame;
  while (remaining_frames > 0) {
    const auto chunk_frames =
        static_cast<int>(std::min<std::uint64_t>(remaining_frames, kDiscardFramesPerChunk));
    const int decoded = stb_vorbis_get_samples_float_interleaved(
        stream.vorbis.get(), static_cast<int>(channels), discard_buffer.data(),
        chunk_frames * static_cast<int>(channels));
    if (decoded <= 0) {
      return false;
    }

    remaining_frames -= static_cast<std::uint64_t>(decoded);
  }

  return true;
}

}

struct OggDecoder::Impl {
  std::vector<std::uint8_t> file_data;
  std::vector<LogicalStreamDecoder> streams;
  std::size_t current_stream{0};

  std::uint32_t sample_rate{0};
  std::uint32_t channels{0};
  std::uint64_t total_frames{0};

  bool open{false};
};

OggDecoder::OggDecoder() : impl_(std::make_unique<Impl>()) {}
OggDecoder::~OggDecoder() = default;
OggDecoder::OggDecoder(OggDecoder &&) noexcept = default;
OggDecoder &OggDecoder::operator=(OggDecoder &&) noexcept = default;

bool OggDecoder::Open(const std::vector<std::uint8_t> &data) {

  impl_ = std::make_unique<Impl>();
  const auto fail_open = [&]() {
    impl_ = std::make_unique<Impl>();
    return false;
  };

  if (data.empty())
    return false;

  impl_->file_data = data;

  std::size_t cursor = 0;
  while (cursor < impl_->file_data.size()) {
    std::vector<std::uint8_t> decode_stream;
    if (!ExtractNextVorbisDecodeStream(impl_->file_data.data(), impl_->file_data.size(), &cursor,
                                       &decode_stream) ||
        decode_stream.empty()) {
      return fail_open();
    }

    std::span<const std::uint8_t> stream_bytes(decode_stream.data(), decode_stream.size());
    std::vector<std::uint8_t> canonical_stream;
    if (CanonicalizeOpeningOggPacketPrefix(stream_bytes, &canonical_stream)) {
      decode_stream = std::move(canonical_stream);
      stream_bytes = std::span<const std::uint8_t>(decode_stream.data(), decode_stream.size());
    }

    if (!ValidateVorbisHeaderParity(stream_bytes)) {
      return fail_open();
    }
    std::vector<std::uint8_t> sanitized_stream;
    if (!SanitizeVorbisAudioPacketParity(stream_bytes, &sanitized_stream) ||
        sanitized_stream.empty()) {
      return fail_open();
    }
    decode_stream = std::move(sanitized_stream);
    stream_bytes = std::span<const std::uint8_t>(decode_stream.data(), decode_stream.size());
    if (stream_bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return fail_open();
    }

    int error = 0;
    StbVorbisPtr vorbis(stb_vorbis_open_memory(
        stream_bytes.data(), static_cast<int>(stream_bytes.size()), &error, nullptr));
    if (!vorbis || error != VORBIS__no_error) {
      return fail_open();
    }

    const stb_vorbis_info info = stb_vorbis_get_info(vorbis.get());
    if ((info.channels != 1 && info.channels != 2) || info.sample_rate <= 0) {
      return fail_open();
    }

    const auto stream_sample_rate = static_cast<std::uint32_t>(info.sample_rate);
    const auto stream_channels = static_cast<std::uint32_t>(info.channels);
    if (impl_->sample_rate == 0) {
      impl_->sample_rate = stream_sample_rate;
      impl_->channels = stream_channels;
    } else if (impl_->sample_rate != stream_sample_rate || impl_->channels != stream_channels) {
      return fail_open();
    }

    const auto stream_frames = ComputeLogicalStreamFrameCount(stream_bytes);
    if (!stream_frames.has_value() ||
        *stream_frames > std::numeric_limits<std::uint64_t>::max() - impl_->total_frames) {
      return fail_open();
    }

    LogicalStreamDecoder decoded_stream;
    decoded_stream.first_frame = impl_->total_frames;
    decoded_stream.frame_count = *stream_frames;
    decoded_stream.canonical_bytes = std::move(decode_stream);
    decoded_stream.vorbis = std::move(vorbis);
    impl_->total_frames += *stream_frames;
    impl_->streams.push_back(std::move(decoded_stream));
  }

  if (impl_->streams.empty() || impl_->sample_rate == 0 || impl_->channels == 0 ||
      impl_->total_frames == 0) {
    return fail_open();
  }

  impl_->open = true;
  return true;
}

std::size_t OggDecoder::Decode(float *output, std::size_t frames) {
  if (!impl_->open || output == nullptr || frames == 0) {
    return 0;
  }

  const int channel_count = static_cast<int>(impl_->channels);
  std::size_t decoded_frames = 0;
  while (decoded_frames < frames && impl_->current_stream < impl_->streams.size()) {
    LogicalStreamDecoder &stream = impl_->streams[impl_->current_stream];
    const auto remaining_frames = frames - decoded_frames;
    if (remaining_frames > static_cast<std::size_t>(std::numeric_limits<int>::max()) /
                               static_cast<std::size_t>(channel_count)) {
      return decoded_frames;
    }

    const int decoded = stb_vorbis_get_samples_float_interleaved(
        stream.vorbis.get(), channel_count, output + decoded_frames * impl_->channels,
        static_cast<int>(remaining_frames * impl_->channels));
    if (decoded < 0) {
      return decoded_frames;
    }
    if (decoded == 0) {
      ++impl_->current_stream;
      continue;
    }

    decoded_frames += static_cast<std::size_t>(decoded);
  }

  return decoded_frames;
}

std::uint32_t OggDecoder::GetSampleRate() const {
  return impl_->sample_rate;
}

std::uint32_t OggDecoder::GetChannels() const {
  return impl_->channels;
}

std::uint64_t OggDecoder::GetTotalFrames() const {
  return impl_->total_frames;
}

bool OggDecoder::Seek(std::uint64_t frame) {
  if (!impl_->open || impl_->streams.empty() || frame > impl_->total_frames) {
    return false;
  }

  const std::size_t target_stream = FindLogicalStreamForFrame(impl_->streams, frame);

  for (auto &stream : impl_->streams) {
    if (!ResetLogicalStream(stream)) {
      return false;
    }
  }

  LogicalStreamDecoder &stream = impl_->streams[target_stream];
  const std::uint64_t local_frame = frame - stream.first_frame;
  if (!SeekLogicalStreamToFrame(stream, impl_->channels, local_frame)) {
    return false;
  }

  impl_->current_stream = target_stream;
  return true;
}

bool OggDecoder::IsOpen() const {
  return impl_->open;
}

}
