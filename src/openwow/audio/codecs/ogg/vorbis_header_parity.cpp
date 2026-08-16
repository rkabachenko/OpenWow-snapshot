
#include "openwow/audio/codecs/ogg/vorbis_header_parity.h"

#include "openwow/audio/codecs/ogg/ogg_logical_streams.h"
#include "openwow/audio/codecs/ogg/vorbis_bit_reader.h"
#include "openwow/audio/codecs/ogg/vorbis_codebook_lookup.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace openwow::audio {

using detail::VorbisBitReader;

namespace {

constexpr std::size_t kOggPageHeaderBytes = 27;
constexpr std::size_t kOggPageHeaderTypeOffset = 5;
constexpr std::size_t kOggPageChecksumOffset = 22;
constexpr std::uint8_t kOggPageContinuedPacketFlag = 0x01;
constexpr std::uint8_t kOggPageBosFlag = 0x02;
constexpr std::uint8_t kOggPageEosFlag = 0x04;
constexpr std::array<std::uint8_t, 7> kVorbisIdentificationSignature = {0x01, 'v', 'o', 'r',
                                                                        'b',  'i', 's'};
constexpr std::array<std::uint8_t, 7> kVorbisCommentSignature = {0x03, 'v', 'o', 'r',
                                                                 'b',  'i', 's'};
constexpr std::array<std::uint8_t, 7> kVorbisSetupSignature = {0x05, 'v', 'o', 'r', 'b', 'i', 's'};
constexpr std::uint32_t kVorbisCodebookSync = 0x564342u;

struct PacketBuffer {
  std::vector<std::uint8_t> bytes;
  std::vector<std::size_t> offsets;
};

struct LogicalOggPacketSlice {
  std::size_t page_index{0};
  std::size_t segment_offset{0};
  std::size_t segment_count{0};
  std::size_t body_offset{0};
  std::size_t body_size{0};
  bool begins_packet{false};
};

struct LogicalOggPacketRecord {
  PacketBuffer packet;
  std::vector<LogicalOggPacketSlice> slices;
  std::size_t completed_page_index{0};
};

struct LogicalOggPageRecord {
  const std::uint8_t *page{nullptr};
  std::size_t header_size{0};
  std::size_t body_size{0};
  std::size_t segment_count{0};
  std::uint8_t header_type{0};
  std::int64_t granule_position{-1};
  std::uint32_t serial_number{0};
  std::size_t last_completed_packet_index{std::numeric_limits<std::size_t>::max()};
};

struct RebuiltLogicalOggPage {
  std::vector<std::uint8_t> segments;
  std::vector<std::uint8_t> body;
  bool continued{false};
  std::int64_t granule_position{-1};
};

constexpr std::uint32_t BuildOggCrcEntry(const std::uint32_t seed) {
  std::uint32_t value = seed << 24;
  for (int bit = 0; bit < 8; ++bit) {
    value = (value & 0x80000000u) != 0 ? (value << 1) ^ 0x04C11DB7u : (value << 1);
  }
  return value;
}

constexpr std::array<std::uint32_t, 256> BuildOggCrcTable() {
  std::array<std::uint32_t, 256> table{};
  for (std::size_t i = 0; i < table.size(); ++i) {
    table[i] = BuildOggCrcEntry(static_cast<std::uint32_t>(i));
  }
  return table;
}

constexpr auto kOggCrcTable = BuildOggCrcTable();

std::uint32_t UpdateOggCrc(std::uint32_t crc, const std::uint8_t byte) {
  const auto index = static_cast<std::uint8_t>((crc >> 24) ^ byte);
  return (crc << 8) ^ kOggCrcTable[index];
}

void WriteLe32(std::vector<std::uint8_t> *bytes, const std::size_t offset,
               const std::uint32_t value) {
  (*bytes)[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
  (*bytes)[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  (*bytes)[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  (*bytes)[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void WriteLe64(std::vector<std::uint8_t> *bytes, const std::size_t offset,
               const std::uint64_t value) {
  (*bytes)[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
  (*bytes)[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  (*bytes)[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  (*bytes)[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
  (*bytes)[offset + 4] = static_cast<std::uint8_t>((value >> 32) & 0xFFu);
  (*bytes)[offset + 5] = static_cast<std::uint8_t>((value >> 40) & 0xFFu);
  (*bytes)[offset + 6] = static_cast<std::uint8_t>((value >> 48) & 0xFFu);
  (*bytes)[offset + 7] = static_cast<std::uint8_t>((value >> 56) & 0xFFu);
}

std::uint32_t ComputeSerializedOggPageChecksum(const std::vector<std::uint8_t> &page_bytes,
                                               const std::size_t header_size,
                                               const std::size_t body_size) {
  std::uint32_t crc = 0;
  for (std::size_t i = 0; i < header_size; ++i) {
    const bool checksum_byte =
        i >= kOggPageChecksumOffset && i < kOggPageChecksumOffset + sizeof(std::uint32_t);
    crc = UpdateOggCrc(crc, checksum_byte ? 0 : page_bytes[i]);
  }
  for (std::size_t i = 0; i < body_size; ++i) {
    crc = UpdateOggCrc(crc, page_bytes[header_size + i]);
  }
  return crc;
}

bool ReadVorbisPacketsAndPages(std::span<const std::uint8_t> logical_stream,
                               std::vector<LogicalOggPageRecord> *pages,
                               std::vector<LogicalOggPacketRecord> *packets) {
  if (!pages || !packets || logical_stream.empty()) {
    return false;
  }

  pages->clear();
  packets->clear();

  OggPageParserState state{};
  state.data = logical_stream.data();
  state.buffered_bytes = logical_stream.size();

  LogicalOggPacketRecord packet;
  while (state.returned_bytes < logical_stream.size()) {
    OggPageView page;
    const auto parse_result = ParseOggPage(&state, &page);
    if (parse_result <= 0 || !page.page || page.header_size < kOggPageHeaderBytes) {
      return false;
    }

    const std::size_t page_index = pages->size();
    const auto *segments = page.page + kOggPageHeaderBytes;
    const std::size_t segment_count = page.header_size - kOggPageHeaderBytes;
    const auto header_type = page.page[kOggPageHeaderTypeOffset];
    const bool expects_continued_packet = !packet.slices.empty();
    if (((header_type & kOggPageContinuedPacketFlag) != 0) != expects_continued_packet) {
      return false;
    }

    pages->push_back({
        .page = page.page,
        .header_size = page.header_size,
        .body_size = page.body_size,
        .segment_count = segment_count,
        .header_type = header_type,
        .granule_position = ReadOggPageGranulePosition(page.page),
        .serial_number = ReadOggPageSerialNumber(page.page),
    });

    const auto page_offset = static_cast<std::size_t>(page.page - logical_stream.data());
    std::size_t body_offset = 0;
    for (std::size_t segment_index = 0; segment_index < segment_count; ++segment_index) {
      const std::size_t segment_size = segments[segment_index];
      if (body_offset + segment_size > page.body_size) {
        return false;
      }

      const std::size_t absolute_body_offset = page_offset + page.header_size + body_offset;
      packet.packet.bytes.insert(packet.packet.bytes.end(), page.body + body_offset,
                                 page.body + body_offset + segment_size);
      for (std::size_t i = 0; i < segment_size; ++i) {
        packet.packet.offsets.push_back(absolute_body_offset + i);
      }
      packet.slices.push_back({
          .page_index = page_index,
          .segment_offset = segment_index,
          .segment_count = 1,
          .body_offset = body_offset,
          .body_size = segment_size,
          .begins_packet = packet.slices.empty(),
      });

      body_offset += segment_size;
      if (segment_size < 255) {
        packet.completed_page_index = page_index;
        packets->push_back(std::move(packet));
        pages->back().last_completed_packet_index = packets->size() - 1;
        packet = {};
      }
    }

    if (body_offset != page.body_size) {
      return false;
    }
  }

  return packet.packet.bytes.empty() && packet.slices.empty();
}

bool ReadVorbisHeaderPackets(std::span<const std::uint8_t> logical_stream,
                             std::array<PacketBuffer, 3> *packets) {
  if (!packets || logical_stream.empty()) {
    return false;
  }

  OggPageParserState state{};
  state.data = logical_stream.data();
  state.buffered_bytes = logical_stream.size();

  PacketBuffer packet;
  std::size_t packet_count = 0;
  while (packet_count < packets->size()) {
    OggPageView page;
    const auto parse_result = ParseOggPage(&state, &page);
    if (parse_result <= 0 || !page.page || page.header_size < kOggPageHeaderBytes) {
      return false;
    }

    const auto page_offset = static_cast<std::size_t>(page.page - logical_stream.data());
    const auto *segments = page.page + kOggPageHeaderBytes;
    const std::size_t segment_count = page.header_size - kOggPageHeaderBytes;
    std::size_t body_offset = 0;
    for (std::size_t i = 0; i < segment_count; ++i) {
      const std::size_t segment_size = segments[i];
      if (body_offset + segment_size > page.body_size) {
        return false;
      }

      const std::size_t absolute_body_offset = page_offset + page.header_size + body_offset;
      packet.bytes.insert(packet.bytes.end(), page.body + body_offset,
                          page.body + body_offset + segment_size);
      for (std::size_t j = 0; j < segment_size; ++j) {
        packet.offsets.push_back(absolute_body_offset + j);
      }

      body_offset += segment_size;
      if (segment_size < 255) {
        (*packets)[packet_count] = std::move(packet);
        packet = PacketBuffer{};
        ++packet_count;
        if (packet_count == packets->size()) {
          break;
        }
      }
    }

    if (body_offset != page.body_size) {
      return false;
    }
  }

  return true;
}

bool PacketStartsWith(const PacketBuffer &packet, const std::array<std::uint8_t, 7> &signature) {
  return packet.bytes.size() >= signature.size() &&
         std::equal(signature.begin(), signature.end(), packet.bytes.begin());
}

bool ReadVorbisU32(VorbisBitReader *reader, std::uint32_t *value) {
  return reader && value && reader->ReadBits(32, value);
}

bool ReadVorbisSignedLength(VorbisBitReader *reader, std::uint32_t *value) {
  if (!ReadVorbisU32(reader, value)) {
    return false;
  }
  return static_cast<std::int32_t>(*value) >= 0;
}

bool SkipVorbisPayloadBytes(VorbisBitReader *reader, const std::uint32_t byte_count) {
  if (!reader) {
    return false;
  }
  const std::size_t bit_count = static_cast<std::size_t>(byte_count) * std::size_t{8};
  if (bit_count / std::size_t{8} != byte_count) {
    return false;
  }
  return reader->SkipBits(bit_count);
}

bool ValidateVorbisIdentificationHeaderPacket(const PacketBuffer &packet,
                                              std::uint32_t *channels_out) {
  if (!channels_out || !PacketStartsWith(packet, kVorbisIdentificationSignature)) {
    return false;
  }

  const std::span<const std::uint8_t> payload(
      packet.bytes.data() + kVorbisIdentificationSignature.size(),
      packet.bytes.size() - kVorbisIdentificationSignature.size());
  VorbisBitReader reader(payload);

  std::uint32_t version = 0;
  std::uint32_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t value = 0;
  if (!ReadVorbisU32(&reader, &version) || !reader.ReadBits(8, &channels) ||
      !ReadVorbisU32(&reader, &sample_rate) || !ReadVorbisU32(&reader, &value) ||
      !ReadVorbisU32(&reader, &value) || !ReadVorbisU32(&reader, &value) ||
      !reader.ReadBits(4, &value)) {
    return false;
  }
  const std::uint32_t blocksize_0 = 1u << value;
  if (!reader.ReadBits(4, &value)) {
    return false;
  }
  const std::uint32_t blocksize_1 = 1u << value;
  std::uint32_t framing = 0;
  if (!reader.ReadBits(1, &framing)) {
    return false;
  }

  if (version != 0 || channels < 1 || sample_rate < 1 || blocksize_0 < 64 ||
      blocksize_1 < blocksize_0 || blocksize_1 > 8192 || framing != 1) {
    return false;
  }

  *channels_out = channels;
  return true;
}

bool ValidateVorbisCommentHeaderPacket(const PacketBuffer &packet) {
  if (!PacketStartsWith(packet, kVorbisCommentSignature)) {
    return false;
  }

  const std::span<const std::uint8_t> payload(packet.bytes.data() + kVorbisCommentSignature.size(),
                                              packet.bytes.size() - kVorbisCommentSignature.size());
  VorbisBitReader reader(payload);

  std::uint32_t vendor_length = 0;
  if (!ReadVorbisSignedLength(&reader, &vendor_length) ||
      !SkipVorbisPayloadBytes(&reader, vendor_length)) {
    return false;
  }

  std::uint32_t comment_count = 0;
  if (!ReadVorbisSignedLength(&reader, &comment_count)) {
    return false;
  }

  for (std::uint32_t i = 0; i < comment_count; ++i) {
    std::uint32_t comment_length = 0;
    if (!ReadVorbisSignedLength(&reader, &comment_length) ||
        !SkipVorbisPayloadBytes(&reader, comment_length)) {
      return false;
    }
  }

  std::uint32_t framing = 0;
  return reader.ReadBits(1, &framing) && framing == 1;
}

bool SkipVorbisCodebook(VorbisBitReader *reader) {
  if (!reader) {
    return false;
  }

  std::uint32_t value = 0;
  if (!reader->ReadBits(24, &value) || value != kVorbisCodebookSync) {
    return false;
  }

  std::uint32_t dimensions = 0;
  std::uint32_t entries = 0;
  if (!reader->ReadBits(16, &dimensions) || !reader->ReadBits(24, &entries)) {
    return false;
  }

  bool ordered = false;
  if (!reader->ReadBits(1, &value)) {
    return false;
  }
  ordered = value != 0;

  if (ordered) {
    if (!reader->ReadBits(5, &value)) {
      return false;
    }

    std::uint32_t current_entry = 0;
    while (current_entry < entries) {
      const int bits = detail::ComputeVorbisBitWidth(entries - current_entry);
      if (!reader->ReadBits(static_cast<std::uint32_t>(bits), &value)) {
        return false;
      }
      current_entry = std::min(entries, current_entry + value);
    }
  } else {
    bool sparse = false;
    if (!reader->ReadBits(1, &value)) {
      return false;
    }
    sparse = value != 0;

    for (std::uint32_t i = 0; i < entries; ++i) {
      bool present = true;
      if (sparse) {
        if (!reader->ReadBits(1, &value)) {
          return false;
        }
        present = value != 0;
      }
      if (!present) {
        continue;
      }

      if (!reader->ReadBits(5, &value)) {
        return false;
      }
    }
  }

  std::uint32_t lookup_type = 0;
  if (!reader->ReadBits(4, &lookup_type) || lookup_type > 2) {
    return false;
  }
  if (lookup_type == 0) {
    return true;
  }

  if (!reader->ReadBits(32, &value)) {
    return false;
  }
  if (!reader->ReadBits(32, &value)) {
    return false;
  }
  std::uint32_t value_bits_minus_one = 0;
  if (!reader->ReadBits(4, &value_bits_minus_one) || !reader->ReadBits(1, &value)) {
    return false;
  }
  const std::uint32_t value_bits = value_bits_minus_one + 1u;

  std::uint64_t lookup_values = 0;
  if (lookup_type == 1) {
    const int quant_values =
        ComputeVorbisLookup1QuantValues(static_cast<int>(dimensions), static_cast<int>(entries));
    if (quant_values < 0) {
      return false;
    }
    lookup_values = static_cast<std::uint32_t>(quant_values);
  } else {
    lookup_values = static_cast<std::uint64_t>(entries) * dimensions;
  }

  for (std::uint64_t i = 0; i < lookup_values; ++i) {
    if (!reader->ReadBits(value_bits, &value)) {
      return false;
    }
  }

  return true;
}

bool SkipVorbisFloorConfig(VorbisBitReader *reader, const std::uint32_t codebook_count) {
  if (!reader) {
    return false;
  }

  std::uint32_t floor_type = 0;
  if (!reader->ReadBits(16, &floor_type) || floor_type > 1) {
    return false;
  }

  std::uint32_t value = 0;
  if (floor_type == 0) {
    std::uint32_t order = 0;
    std::uint32_t rate = 0;
    std::uint32_t bark_map_size = 0;
    if (!reader->ReadBits(8, &order) || !reader->ReadBits(16, &rate) ||
        !reader->ReadBits(16, &bark_map_size) || !reader->ReadBits(6, &value) ||
        !reader->ReadBits(8, &value) || !reader->ReadBits(4, &value)) {
      return false;
    }
    if (order < 1 || rate < 1 || bark_map_size < 1) {
      return false;
    }
    const std::uint32_t book_count = value + 1u;
    for (std::uint32_t i = 0; i < book_count; ++i) {
      if (!reader->ReadBits(8, &value) || value >= codebook_count) {
        return false;
      }
    }
    return true;
  }

  std::uint32_t partitions = 0;
  if (!reader->ReadBits(5, &partitions)) {
    return false;
  }

  std::vector<std::uint32_t> partition_class_list(partitions);
  std::uint32_t max_class = 0;
  for (std::uint32_t i = 0; i < partitions; ++i) {
    if (!reader->ReadBits(4, &partition_class_list[i])) {
      return false;
    }
    max_class = std::max(max_class, partition_class_list[i]);
  }

  std::vector<std::uint32_t> class_dimensions(max_class + 1);
  for (std::uint32_t i = 0; i <= max_class; ++i) {
    std::uint32_t class_subclasses = 0;
    if (!reader->ReadBits(3, &class_dimensions[i]) || !reader->ReadBits(2, &class_subclasses)) {
      return false;
    }
    ++class_dimensions[i];

    if (class_subclasses != 0) {
      if (!reader->ReadBits(8, &value) || value >= codebook_count) {
        return false;
      }
    }

    const std::uint32_t subclass_count = 1u << class_subclasses;
    for (std::uint32_t j = 0; j < subclass_count; ++j) {
      if (!reader->ReadBits(8, &value)) {
        return false;
      }
      const auto book = static_cast<int>(value) - 1;
      if (book >= static_cast<int>(codebook_count)) {
        return false;
      }
    }
  }

  if (!reader->ReadBits(2, &value) || !reader->ReadBits(4, &value)) {
    return false;
  }
  const std::uint32_t rangebits = value;

  std::vector<std::uint32_t> xlist;
  xlist.reserve(258);
  xlist.push_back(0);
  xlist.push_back(1u << rangebits);
  for (std::uint32_t i = 0; i < partitions; ++i) {
    const auto klass = partition_class_list[i];
    for (std::uint32_t j = 0; j < class_dimensions[klass]; ++j) {
      if (!reader->ReadBits(rangebits, &value)) {
        return false;
      }
      xlist.push_back(value);
    }
  }

  auto sorted = xlist;
  std::sort(sorted.begin(), sorted.end());
  return std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
}

bool SkipVorbisResidueConfig(VorbisBitReader *reader, const std::uint32_t codebook_count) {
  if (!reader) {
    return false;
  }

  std::uint32_t value = 0;
  if (!reader->ReadBits(16, &value) || value > 2) {
    return false;
  }

  if (!reader->ReadBits(24, &value) || !reader->ReadBits(24, &value) ||
      !reader->ReadBits(24, &value) || !reader->ReadBits(6, &value)) {
    return false;
  }
  const std::uint32_t classifications = value + 1u;

  if (!reader->ReadBits(8, &value) || value >= codebook_count) {
    return false;
  }

  std::vector<std::uint8_t> cascades(classifications);
  for (std::uint32_t i = 0; i < classifications; ++i) {
    std::uint32_t low_bits = 0;
    std::uint32_t high_bits = 0;
    if (!reader->ReadBits(3, &low_bits) || !reader->ReadBits(1, &value)) {
      return false;
    }
    if (value != 0) {
      if (!reader->ReadBits(5, &high_bits)) {
        return false;
      }
    }
    cascades[i] = static_cast<std::uint8_t>(high_bits * 8u + low_bits);
  }

  for (std::uint32_t i = 0; i < classifications; ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      if ((cascades[i] & (1u << bit)) == 0) {
        continue;
      }
      if (!reader->ReadBits(8, &value) || value >= codebook_count) {
        return false;
      }
    }
  }

  return true;
}

bool SkipVorbisMappingConfig(VorbisBitReader *reader, const std::uint32_t channels,
                             const std::uint32_t floor_count, const std::uint32_t residue_count) {
  if (!reader) {
    return false;
  }

  std::uint32_t value = 0;
  if (!reader->ReadBits(16, &value) || value != 0) {
    return false;
  }

  std::uint32_t submaps = 1;
  if (!reader->ReadBits(1, &value)) {
    return false;
  }
  if (value != 0) {
    if (!reader->ReadBits(4, &value)) {
      return false;
    }
    submaps = value + 1u;
  }

  std::uint32_t coupling_steps = 0;
  if (!reader->ReadBits(1, &value)) {
    return false;
  }
  if (value != 0) {
    if (!reader->ReadBits(8, &value)) {
      return false;
    }
    coupling_steps = value + 1u;
    if (coupling_steps > channels) {
      return false;
    }

    const auto channel_bits =
        static_cast<std::uint32_t>(detail::ComputeVorbisBitWidth(channels - 1));
    for (std::uint32_t i = 0; i < coupling_steps; ++i) {
      std::uint32_t magnitude = 0;
      std::uint32_t angle = 0;
      if (!reader->ReadBits(channel_bits, &magnitude) || !reader->ReadBits(channel_bits, &angle)) {
        return false;
      }
      if (magnitude >= channels || angle >= channels || magnitude == angle) {
        return false;
      }
    }
  }

  if (!reader->ReadBits(2, &value) || value != 0) {
    return false;
  }

  if (submaps > 1) {
    for (std::uint32_t i = 0; i < channels; ++i) {
      if (!reader->ReadBits(4, &value) || value >= submaps) {
        return false;
      }
    }
  }

  for (std::uint32_t i = 0; i < submaps; ++i) {
    std::uint32_t floor = 0;
    std::uint32_t residue = 0;
    if (!reader->ReadBits(8, &value) || !reader->ReadBits(8, &floor) ||
        !reader->ReadBits(8, &residue)) {
      return false;
    }
    if (floor >= floor_count || residue >= residue_count) {
      return false;
    }
  }

  return true;
}

bool ReadVorbisModes(VorbisBitReader *reader, const std::uint32_t mapping_count,
                     std::vector<std::uint8_t> *mode_blockflags = nullptr) {
  if (!reader) {
    return false;
  }

  std::uint32_t mode_count = 0;
  if (!reader->ReadBits(6, &mode_count)) {
    return false;
  }
  ++mode_count;
  if (mode_blockflags) {
    mode_blockflags->clear();
    mode_blockflags->reserve(mode_count);
  }

  for (std::uint32_t i = 0; i < mode_count; ++i) {
    std::uint32_t blockflag = 0;
    std::uint32_t value = 0;
    std::uint32_t mapping = 0;
    if (!reader->ReadBits(1, &blockflag) || !reader->ReadBits(16, &value) || value != 0 ||
        !reader->ReadBits(16, &value) || value != 0 || !reader->ReadBits(8, &mapping)) {
      return false;
    }
    if (mapping >= mapping_count) {
      return false;
    }
    if (mode_blockflags) {
      mode_blockflags->push_back(static_cast<std::uint8_t>(blockflag != 0));
    }
  }

  return true;
}

bool ReadVorbisSetupPacketDetails(const PacketBuffer &setup_packet, const std::uint32_t channels,
                                  std::size_t *byte_offset, std::uint8_t *bit_mask,
                                  std::vector<std::uint8_t> *mode_blockflags) {
  if (!PacketStartsWith(setup_packet, kVorbisSetupSignature)) {
    return false;
  }

  const std::span<const std::uint8_t> payload(
      setup_packet.bytes.data() + kVorbisSetupSignature.size(),
      setup_packet.bytes.size() - kVorbisSetupSignature.size());
  VorbisBitReader reader(payload);
  std::uint32_t value = 0;

  if (!reader.ReadBits(8, &value)) {
    return false;
  }
  const std::uint32_t codebook_count = value + 1u;

  for (std::uint32_t i = 0; i < codebook_count; ++i) {
    if (!SkipVorbisCodebook(&reader)) {
      return false;
    }
  }

  if (!reader.ReadBits(6, &value)) {
    return false;
  }
  const std::uint32_t time_count = value + 1u;
  for (std::uint32_t i = 0; i < time_count; ++i) {
    if (!reader.ReadBits(16, &value) || value != 0) {
      return false;
    }
  }

  std::uint32_t floor_count = 0;
  if (!reader.ReadBits(6, &floor_count)) {
    return false;
  }
  ++floor_count;
  for (std::uint32_t i = 0; i < floor_count; ++i) {
    if (!SkipVorbisFloorConfig(&reader, codebook_count)) {
      return false;
    }
  }

  std::uint32_t residue_count = 0;
  if (!reader.ReadBits(6, &residue_count)) {
    return false;
  }
  ++residue_count;
  for (std::uint32_t i = 0; i < residue_count; ++i) {
    if (!SkipVorbisResidueConfig(&reader, codebook_count)) {
      return false;
    }
  }

  std::uint32_t mapping_count = 0;
  if (!reader.ReadBits(6, &mapping_count)) {
    return false;
  }
  ++mapping_count;
  for (std::uint32_t i = 0; i < mapping_count; ++i) {
    if (!SkipVorbisMappingConfig(&reader, channels, floor_count, residue_count)) {
      return false;
    }
  }

  if (!ReadVorbisModes(&reader, mapping_count, mode_blockflags)) {
    return false;
  }

  if (byte_offset || bit_mask) {
    if (!byte_offset || !bit_mask) {
      return false;
    }

    const std::size_t bit_offset = reader.bit_offset();
    const std::size_t byte_index = bit_offset >> 3;
    const std::size_t bit_index = bit_offset & 7;
    const std::size_t offset_index = byte_index + kVorbisSetupSignature.size();
    if (offset_index >= setup_packet.offsets.size()) {
      return false;
    }

    *byte_offset = setup_packet.offsets[offset_index];
    *bit_mask = static_cast<std::uint8_t>(1u << bit_index);
  }
  return true;
}

bool ValidateVorbisAudioPacketPrefix(const PacketBuffer &packet,
                                     const std::vector<std::uint8_t> &mode_blockflags) {
  if (packet.bytes.empty() || mode_blockflags.empty()) {
    return false;
  }

  VorbisBitReader reader(packet.bytes);
  std::uint32_t value = 0;
  if (!reader.ReadBits(1, &value) || value != 0) {
    return false;
  }

  std::uint32_t mode = 0;
  const auto mode_bits =
      static_cast<std::uint32_t>(
          detail::ComputeVorbisBitWidth(static_cast<std::uint32_t>(mode_blockflags.size() - 1)));
  if (!reader.ReadBits(mode_bits, &mode) || mode >= mode_blockflags.size()) {
    return false;
  }

  if (mode_blockflags[mode] == 0) {
    return true;
  }

  return reader.ReadBits(1, &value) && reader.ReadBits(1, &value);
}

bool RebuildSanitizedVorbisLogicalStream(const std::vector<LogicalOggPageRecord> &pages,
                                         const std::vector<LogicalOggPacketRecord> &packets,
                                         const std::vector<bool> &keep_packet,
                                         std::vector<std::uint8_t> *sanitized_stream) {
  if (!sanitized_stream || pages.empty() || packets.size() != keep_packet.size()) {
    return false;
  }

  const auto serial_number = pages.front().serial_number;
  for (const auto &page : pages) {
    if (page.serial_number != serial_number) {
      return false;
    }
  }

  std::vector<RebuiltLogicalOggPage> rebuilt_pages(pages.size());
  for (std::size_t packet_index = 0; packet_index < packets.size(); ++packet_index) {
    if (!keep_packet[packet_index]) {
      continue;
    }

    const auto &packet = packets[packet_index];
    for (const auto &slice : packet.slices) {
      auto &rebuilt_page = rebuilt_pages[slice.page_index];
      if (rebuilt_page.segments.empty()) {
        rebuilt_page.continued = !slice.begins_packet;
      }

      const auto &source_page = pages[slice.page_index];
      const auto *segments = source_page.page + kOggPageHeaderBytes;
      rebuilt_page.segments.insert(rebuilt_page.segments.end(), segments + slice.segment_offset,
                                   segments + slice.segment_offset + slice.segment_count);
      rebuilt_page.body.insert(
          rebuilt_page.body.end(), source_page.page + source_page.header_size + slice.body_offset,
          source_page.page + source_page.header_size + slice.body_offset + slice.body_size);
    }
  }

  std::vector<std::size_t> output_page_indices;
  output_page_indices.reserve(rebuilt_pages.size());
  for (std::size_t page_index = 0; page_index < rebuilt_pages.size(); ++page_index) {
    auto &rebuilt_page = rebuilt_pages[page_index];
    if (rebuilt_page.segments.empty()) {
      continue;
    }

    const auto last_packet_index = pages[page_index].last_completed_packet_index;
    if (last_packet_index != std::numeric_limits<std::size_t>::max() &&
        keep_packet[last_packet_index]) {
      rebuilt_page.granule_position = pages[page_index].granule_position;
    }
    output_page_indices.push_back(page_index);
  }

  if (output_page_indices.empty()) {
    return false;
  }

  const auto &last_page = rebuilt_pages[output_page_indices.back()];
  if (last_page.granule_position < 0) {
    return false;
  }

  sanitized_stream->clear();
  for (std::size_t output_index = 0; output_index < output_page_indices.size(); ++output_index) {
    const auto page_index = output_page_indices[output_index];
    const auto &rebuilt_page = rebuilt_pages[page_index];
    if (rebuilt_page.segments.size() > std::numeric_limits<std::uint8_t>::max()) {
      return false;
    }

    const auto is_first = output_index == 0;
    const auto is_last = output_index + 1 == output_page_indices.size();
    const auto header_size = kOggPageHeaderBytes + rebuilt_page.segments.size();
    const auto body_size = rebuilt_page.body.size();

    std::vector<std::uint8_t> page_bytes(header_size + body_size, 0);
    std::memcpy(page_bytes.data(), "OggS", 4);
    page_bytes[kOggPageHeaderTypeOffset] = static_cast<std::uint8_t>(
        (rebuilt_page.continued ? kOggPageContinuedPacketFlag : 0) |
        (is_first ? kOggPageBosFlag : 0) | (is_last ? kOggPageEosFlag : 0));
    WriteLe64(&page_bytes, 6, static_cast<std::uint64_t>(rebuilt_page.granule_position));
    WriteLe32(&page_bytes, 14, serial_number);
    WriteLe32(&page_bytes, 18, static_cast<std::uint32_t>(output_index));
    page_bytes[26] = static_cast<std::uint8_t>(rebuilt_page.segments.size());
    std::copy(rebuilt_page.segments.begin(), rebuilt_page.segments.end(),
              page_bytes.begin() + static_cast<std::ptrdiff_t>(kOggPageHeaderBytes));
    std::copy(rebuilt_page.body.begin(), rebuilt_page.body.end(),
              page_bytes.begin() + static_cast<std::ptrdiff_t>(header_size));
    WriteLe32(&page_bytes, kOggPageChecksumOffset,
              ComputeSerializedOggPageChecksum(page_bytes, header_size, body_size));
    sanitized_stream->insert(sanitized_stream->end(), page_bytes.begin(), page_bytes.end());
  }

  return true;
}

}

bool LocateVorbisSetupFramingBit(std::span<const std::uint8_t> logical_stream,
                                 std::size_t *byte_offset, std::uint8_t *bit_mask) {
  if (!byte_offset || !bit_mask) {
    return false;
  }

  std::vector<std::uint8_t> canonical_stream;
  std::size_t removed_prefix_bytes = 0;
  std::span<const std::uint8_t> parsed_stream = logical_stream;
  if (CanonicalizeOpeningOggPacketPrefix(logical_stream, &canonical_stream,
                                         &removed_prefix_bytes)) {
    parsed_stream = canonical_stream;
  }

  std::array<PacketBuffer, 3> packets;
  std::uint32_t channels = 0;
  if (!ReadVorbisHeaderPackets(parsed_stream, &packets)) {
    return false;
  }

  if (!ValidateVorbisIdentificationHeaderPacket(packets[0], &channels)) {
    return false;
  }

  if (!ValidateVorbisCommentHeaderPacket(packets[1])) {
    return false;
  }

  if (!PacketStartsWith(packets[2], kVorbisSetupSignature)) {
    return false;
  }

  std::size_t canonical_byte_offset = 0;
  std::vector<std::uint8_t> mode_blockflags;
  if (!ReadVorbisSetupPacketDetails(packets[2], channels, &canonical_byte_offset, bit_mask,
                                    &mode_blockflags)) {
    return false;
  }

  *byte_offset = canonical_byte_offset + removed_prefix_bytes;
  return *byte_offset < logical_stream.size();
}

bool ValidateVorbisHeaderParity(std::span<const std::uint8_t> logical_stream) {
  std::size_t byte_offset = 0;
  std::uint8_t bit_mask = 0;
  if (!LocateVorbisSetupFramingBit(logical_stream, &byte_offset, &bit_mask) ||
      byte_offset >= logical_stream.size()) {
    return false;
  }

  return (logical_stream[byte_offset] & bit_mask) != 0;
}

bool SanitizeVorbisAudioPacketParity(std::span<const std::uint8_t> logical_stream,
                                     std::vector<std::uint8_t> *sanitized_stream) {
  if (!sanitized_stream || logical_stream.empty()) {
    return false;
  }

  std::vector<std::uint8_t> canonical_stream;
  std::span<const std::uint8_t> parsed_stream = logical_stream;
  if (CanonicalizeOpeningOggPacketPrefix(logical_stream, &canonical_stream)) {
    parsed_stream = canonical_stream;
  }

  std::vector<LogicalOggPageRecord> pages;
  std::vector<LogicalOggPacketRecord> packets;
  if (!ReadVorbisPacketsAndPages(parsed_stream, &pages, &packets) || packets.size() < 3) {
    return false;
  }

  std::uint32_t channels = 0;
  std::vector<std::uint8_t> mode_blockflags;
  if (!ValidateVorbisIdentificationHeaderPacket(packets[0].packet, &channels) ||
      !ValidateVorbisCommentHeaderPacket(packets[1].packet) ||
      !ReadVorbisSetupPacketDetails(packets[2].packet, channels, nullptr, nullptr,
                                    &mode_blockflags)) {
    return false;
  }

  std::vector<bool> keep_packet(packets.size(), true);
  bool dropped_packet = false;
  for (std::size_t packet_index = 3; packet_index < packets.size(); ++packet_index) {
    if (ValidateVorbisAudioPacketPrefix(packets[packet_index].packet, mode_blockflags)) {
      continue;
    }
    keep_packet[packet_index] = false;
    dropped_packet = true;
  }

  if (!dropped_packet) {
    sanitized_stream->assign(parsed_stream.begin(), parsed_stream.end());
    return true;
  }

  return RebuildSanitizedVorbisLogicalStream(pages, packets, keep_packet, sanitized_stream);
}

}
