
#include "openwow/audio/codecs/ogg/ogg_logical_streams.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>

namespace openwow::audio {

namespace {

constexpr std::size_t kOggPageHeaderBytes = 27;
constexpr std::size_t kOggPageVersionOffset = 4;
constexpr std::size_t kOggPageHeaderTypeOffset = 5;
constexpr std::size_t kOggPageGranulePositionOffset = 6;
constexpr std::size_t kOggPageSerialNumberOffset = 14;
constexpr std::size_t kOggPageSequenceNumberOffset = 18;
constexpr std::size_t kOggPageChecksumOffset = 22;
constexpr std::uint8_t kOggPageContinuedPacketFlag = 0x01;
constexpr std::uint8_t kOggPageBosFlag = 0x02;
constexpr std::uint8_t kOggPageEosFlag = 0x04;
constexpr std::array<std::uint8_t, 7> kVorbisIdentificationPacketSignature = {
    0x01, 'v', 'o', 'r', 'b', 'i', 's',
};

std::uint8_t ReadOggPageVersion(const std::uint8_t* page) {
    return page[kOggPageVersionOffset];
}

std::uint8_t ReadOggPageHeaderType(const std::uint8_t* page) {
    return page[kOggPageHeaderTypeOffset];
}

std::uint32_t ReadOggPageSequenceNumber(const std::uint8_t* page) {
    const auto* sequence = page + kOggPageSequenceNumberOffset;
    return static_cast<std::uint32_t>(sequence[0])
         | (static_cast<std::uint32_t>(sequence[1]) << 8)
         | (static_cast<std::uint32_t>(sequence[2]) << 16)
         | (static_cast<std::uint32_t>(sequence[3]) << 24);
}

constexpr std::uint32_t BuildOggCrcEntry(std::uint32_t seed) {
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

std::uint32_t ReadOggPageChecksum(const std::uint8_t* page) {
    return static_cast<std::uint32_t>(page[kOggPageChecksumOffset + 0])
         | (static_cast<std::uint32_t>(page[kOggPageChecksumOffset + 1]) << 8)
         | (static_cast<std::uint32_t>(page[kOggPageChecksumOffset + 2]) << 16)
         | (static_cast<std::uint32_t>(page[kOggPageChecksumOffset + 3]) << 24);
}

std::uint32_t UpdateOggCrc(std::uint32_t crc, std::uint8_t byte) {
    const auto index = static_cast<std::uint8_t>((crc >> 24) ^ byte);
    return (crc << 8) ^ kOggCrcTable[index];
}

std::ptrdiff_t ResyncOggPageParser(OggPageParserState* state,
                                   const std::uint8_t* page,
                                   std::size_t bytes_available) {
    state->header_bytes = 0;
    state->body_bytes = 0;

    const auto search_bytes = bytes_available > 0 ? bytes_available - 1 : 0;
    const void* next_page = search_bytes == 0
                          ? nullptr
                          : std::memchr(page + 1, 'O', search_bytes);
    const auto* resume = next_page != nullptr
                       ? static_cast<const std::uint8_t*>(next_page)
                       : state->data + state->buffered_bytes;
    state->returned_bytes = static_cast<std::size_t>(resume - state->data);
    return static_cast<std::ptrdiff_t>(page - resume);
}

std::uint32_t ComputeOggPageChecksum(const std::uint8_t* page,
                                     std::size_t header_size,
                                     std::size_t body_size) {
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < header_size; ++i) {
        const bool is_checksum_byte =
            i >= kOggPageChecksumOffset && i < kOggPageChecksumOffset + sizeof(std::uint32_t);
        crc = UpdateOggCrc(crc, is_checksum_byte ? 0 : page[i]);
    }

    const auto* body = page + header_size;
    for (std::size_t i = 0; i < body_size; ++i) {
        crc = UpdateOggCrc(crc, body[i]);
    }

    return crc;
}

bool IsOggPageChecksumValid(const std::uint8_t* page,
                            std::size_t header_size,
                            std::size_t body_size) {
    return ReadOggPageChecksum(page) == ComputeOggPageChecksum(page, header_size, body_size);
}

struct OggStreamPageInState {
    bool have_serial{false};
    std::uint32_t serial{0};
    bool have_expected_page_number{false};
    std::uint32_t expected_page_number{0};
    bool have_lacing_entries{false};
    bool tail_is_gap_marker{false};
};

struct ParsedOggPageRecord {
    std::size_t offset{0};
    std::size_t header_size{0};
    std::size_t body_size{0};
    std::size_t segment_count{0};
    std::uint8_t header_type{0};
};

struct ContinuedPacketSkip {
    std::size_t segment_count{0};
    std::size_t body_bytes{0};
    bool packet_continues{false};
};

bool PageInOggLogicalStream(OggStreamPageInState* state, const OggPageView& page) {
    if (!state || !page.page) {
        return false;
    }

    if (ReadOggPageVersion(page.page) > 0) {
        return false;
    }

    const std::uint32_t serial = ReadOggPageSerialNumber(page.page);
    if (!state->have_serial) {
        state->have_serial = true;
        state->serial = serial;
    } else if (serial != state->serial) {
        return false;
    }

    const std::uint32_t page_number = ReadOggPageSequenceNumber(page.page);
    if (state->have_expected_page_number && page_number != state->expected_page_number) {
        state->have_lacing_entries = true;
        state->tail_is_gap_marker = true;
    }

    const std::size_t segment_count = page.header_size - kOggPageHeaderBytes;
    const auto* segments = page.page + kOggPageHeaderBytes;
    std::size_t first_segment = 0;
    if (HasOggPageContinuedPacketFlag(page.page)
        && (!state->have_lacing_entries || state->tail_is_gap_marker)) {
        while (first_segment < segment_count) {
            if (segments[first_segment++] < 255) {
                break;
            }
        }
    }

    for (std::size_t i = first_segment; i < segment_count; ++i) {
        state->have_lacing_entries = true;
        state->tail_is_gap_marker = false;
    }

    state->have_expected_page_number = true;
    state->expected_page_number = page_number + 1;
    return true;
}

bool TrackOpeningBosPageSerial(const OggPageView& page,
                               bool* opening_bos_run_complete,
                               std::vector<std::uint32_t>* opening_bos_serials) {
    if (!page.page || !opening_bos_run_complete || !opening_bos_serials) {
        return false;
    }
    if (*opening_bos_run_complete) {
        return true;
    }
    if (!HasOggPageBosFlag(page.page)) {
        *opening_bos_run_complete = true;
        return true;
    }

    const std::uint32_t serial = ReadOggPageSerialNumber(page.page);
    if (std::find(opening_bos_serials->begin(), opening_bos_serials->end(), serial)
        != opening_bos_serials->end()) {
        return false;
    }

    opening_bos_serials->push_back(serial);
    return true;
}

bool CollectParsedOggPages(const std::span<const std::uint8_t> logical_stream,
                           std::vector<ParsedOggPageRecord>* pages) {
    if (!pages || logical_stream.empty()) {
        return false;
    }

    pages->clear();

    OggPageParserState parser{};
    parser.data = logical_stream.data();
    parser.buffered_bytes = logical_stream.size();

    while (parser.returned_bytes < logical_stream.size()) {
        OggPageView page;
        const auto parse_result = ParseOggPage(&parser, &page);
        if (parse_result <= 0 || !page.page || page.header_size < kOggPageHeaderBytes) {
            return false;
        }

        pages->push_back({
            .offset = static_cast<std::size_t>(page.page - logical_stream.data()),
            .header_size = page.header_size,
            .body_size = page.body_size,
            .segment_count = page.header_size - kOggPageHeaderBytes,
            .header_type = ReadOggPageHeaderType(page.page),
        });
    }

    return !pages->empty();
}

ContinuedPacketSkip CountLeadingContinuedPacketPrefix(const std::uint8_t* segments,
                                                      const std::size_t segment_count) {
    ContinuedPacketSkip skip{};
    if (!segments || segment_count == 0) {
        return skip;
    }

    skip.packet_continues = true;
    while (skip.segment_count < segment_count) {
        const auto segment_size = static_cast<std::size_t>(segments[skip.segment_count]);
        ++skip.segment_count;
        skip.body_bytes += segment_size;
        if (segment_size < 255) {
            skip.packet_continues = false;
            break;
        }
    }

    return skip;
}

void AppendCanonicalOggPage(const std::span<const std::uint8_t> logical_stream,
                            const ParsedOggPageRecord& page,
                            const std::size_t skip_segments,
                            const std::size_t skip_body_bytes,
                            const bool force_bos_flag,
                            const bool clear_continued_flag,
                            std::vector<std::uint8_t>* canonical_stream) {
    if (!canonical_stream) {
        return;
    }

    const auto* source_page = logical_stream.data() + page.offset;
    const auto new_segment_count = page.segment_count - skip_segments;
    const auto new_header_size = kOggPageHeaderBytes + new_segment_count;
    const auto new_body_size = page.body_size - skip_body_bytes;

    const auto page_offset = canonical_stream->size();
    canonical_stream->insert(canonical_stream->end(),
                             source_page,
                             source_page + kOggPageHeaderBytes);

    auto header_type = page.header_type;
    if (force_bos_flag) {
        header_type |= kOggPageBosFlag;
    }
    if (clear_continued_flag) {
        header_type &= ~kOggPageContinuedPacketFlag;
    }
    (*canonical_stream)[page_offset + kOggPageHeaderTypeOffset] = header_type;
    (*canonical_stream)[page_offset + 26] = static_cast<std::uint8_t>(new_segment_count);

    canonical_stream->insert(canonical_stream->end(),
                             source_page + kOggPageHeaderBytes + skip_segments,
                             source_page + kOggPageHeaderBytes + page.segment_count);
    canonical_stream->insert(canonical_stream->end(),
                             source_page + page.header_size + skip_body_bytes,
                             source_page + page.header_size + page.body_size);

    for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
        (*canonical_stream)[page_offset + kOggPageChecksumOffset + i] = 0;
    }

    const auto crc = ComputeOggPageChecksum(canonical_stream->data() + page_offset,
                                            new_header_size,
                                            new_body_size);
    (*canonical_stream)[page_offset + kOggPageChecksumOffset + 0] =
        static_cast<std::uint8_t>(crc & 0xFFu);
    (*canonical_stream)[page_offset + kOggPageChecksumOffset + 1] =
        static_cast<std::uint8_t>((crc >> 8) & 0xFFu);
    (*canonical_stream)[page_offset + kOggPageChecksumOffset + 2] =
        static_cast<std::uint8_t>((crc >> 16) & 0xFFu);
    (*canonical_stream)[page_offset + kOggPageChecksumOffset + 3] =
        static_cast<std::uint8_t>((crc >> 24) & 0xFFu);
}

void AppendRawOggPage(const OggPageView& page, std::vector<std::uint8_t>* stream_bytes) {
    if (!page.page || !stream_bytes) {
        return;
    }

    stream_bytes->insert(stream_bytes->end(), page.page, page.page + page.TotalSize());
}

struct FirstCompletedPagePacketView {
    std::span<const std::uint8_t> bytes;
    bool begin_of_stream{false};
};

bool ExtractFirstCompletedPagePacket(const OggPageView& page,
                                     FirstCompletedPagePacketView* packet) {
    if (!page.page || !packet || page.header_size < kOggPageHeaderBytes) {
        return false;
    }

    const auto segment_count = page.header_size - kOggPageHeaderBytes;
    const auto* segments = page.page + kOggPageHeaderBytes;
    std::size_t segment_index = 0;
    std::size_t body_offset = 0;

    if (HasOggPageContinuedPacketFlag(page.page)) {
        const auto skip = CountLeadingContinuedPacketPrefix(segments, segment_count);
        if (skip.segment_count == 0) {
            return false;
        }
        segment_index = skip.segment_count;
        body_offset = skip.body_bytes;
    }

    if (segment_index >= segment_count || body_offset >= page.body_size) {
        return false;
    }

    const bool begin_of_stream =
        HasOggPageBosFlag(page.page) && segment_index == 0 && body_offset == 0;
    std::size_t packet_size = 0;
    while (segment_index < segment_count) {
        const auto segment_size = static_cast<std::size_t>(segments[segment_index++]);
        if (body_offset + packet_size + segment_size > page.body_size) {
            return false;
        }

        packet_size += segment_size;
        if (segment_size < 255) {
            packet->bytes = std::span<const std::uint8_t>(page.body + body_offset, packet_size);
            packet->begin_of_stream = begin_of_stream;
            return true;
        }
    }

    return false;
}

bool IsVorbisIdentificationPacket(const FirstCompletedPagePacketView& packet) {
    return packet.begin_of_stream
        && packet.bytes.size() >= kVorbisIdentificationPacketSignature.size()
        && std::equal(kVorbisIdentificationPacketSignature.begin(),
                      kVorbisIdentificationPacketSignature.end(),
                      packet.bytes.begin());
}

std::size_t CountCompletedPacketsAtStart(const std::span<const std::uint8_t> logical_stream,
                                         const std::size_t limit) {
    if (logical_stream.empty() || limit == 0) {
        return 0;
    }

    std::vector<ParsedOggPageRecord> pages;
    if (!CollectParsedOggPages(logical_stream, &pages)) {
        return 0;
    }

    std::size_t packet_count = 0;
    bool opening_queue_empty = true;
    for (const auto& page : pages) {
        const auto* page_bytes = logical_stream.data() + page.offset;
        const auto* segments = page_bytes + kOggPageHeaderBytes;
        std::size_t segment_index = 0;

        if ((page.header_type & kOggPageContinuedPacketFlag) != 0 && opening_queue_empty) {
            const auto skip = CountLeadingContinuedPacketPrefix(segments, page.segment_count);
            if (skip.segment_count == 0) {
                return packet_count;
            }
            segment_index = skip.segment_count;
            opening_queue_empty = false;
            if (!skip.packet_continues) {
                opening_queue_empty = true;
            }
        }

        for (; segment_index < page.segment_count; ++segment_index) {
            opening_queue_empty = false;
            if (segments[segment_index] < 255) {
                ++packet_count;
                opening_queue_empty = true;
                if (packet_count >= limit) {
                    return packet_count;
                }
            }
        }
    }

    return packet_count;
}

}

bool HasOggPageContinuedPacketFlag(const std::uint8_t* page) {
    return (ReadOggPageHeaderType(page) & kOggPageContinuedPacketFlag) != 0;
}

bool HasOggPageBosFlag(const std::uint8_t* page) {
    return (ReadOggPageHeaderType(page) & kOggPageBosFlag) != 0;
}

bool HasOggPageEosFlag(const std::uint8_t* page) {
    return (ReadOggPageHeaderType(page) & kOggPageEosFlag) != 0;
}

std::int64_t ReadOggPageGranulePosition(const std::uint8_t* page) {
    const auto* granule = page + kOggPageGranulePositionOffset;
    const std::uint64_t raw = static_cast<std::uint64_t>(granule[0])
                            | (static_cast<std::uint64_t>(granule[1]) << 8)
                            | (static_cast<std::uint64_t>(granule[2]) << 16)
                            | (static_cast<std::uint64_t>(granule[3]) << 24)
                            | (static_cast<std::uint64_t>(granule[4]) << 32)
                            | (static_cast<std::uint64_t>(granule[5]) << 40)
                            | (static_cast<std::uint64_t>(granule[6]) << 48)
                            | (static_cast<std::uint64_t>(granule[7]) << 56);
    return std::bit_cast<std::int64_t>(raw);
}

std::uint32_t ReadOggPageSerialNumber(const std::uint8_t* page) {
    const auto* serial = page + kOggPageSerialNumberOffset;
    return static_cast<std::uint32_t>(serial[0])
         | (static_cast<std::uint32_t>(serial[1]) << 8)
         | (static_cast<std::uint32_t>(serial[2]) << 16)
         | (static_cast<std::uint32_t>(serial[3]) << 24);
}

std::ptrdiff_t ParseOggPage(OggPageParserState* state, OggPageView* page) {
    if (!state || !state->data || state->returned_bytes > state->buffered_bytes) {
        return 0;
    }

    const auto* page_begin = state->data + state->returned_bytes;
    const std::size_t bytes_available = state->buffered_bytes - state->returned_bytes;

    if (state->header_bytes == 0) {
        if (bytes_available < kOggPageHeaderBytes) {
            return 0;
        }
        if (std::memcmp(page_begin, "OggS", 4) != 0) {
            return ResyncOggPageParser(state, page_begin, bytes_available);
        }

        const std::size_t segment_count = page_begin[26];
        const std::size_t header_size = kOggPageHeaderBytes + segment_count;
        if (bytes_available < header_size) {
            return 0;
        }

        std::size_t body_size = 0;
        for (std::size_t i = 0; i < segment_count; ++i) {
            body_size += page_begin[kOggPageHeaderBytes + i];
        }

        state->header_bytes = header_size;
        state->body_bytes = body_size;
    }

    if (state->header_bytes + state->body_bytes > bytes_available) {
        return 0;
    }
    if (!IsOggPageChecksumValid(page_begin, state->header_bytes, state->body_bytes)) {
        return ResyncOggPageParser(state, page_begin, bytes_available);
    }

    if (page) {
        page->page = page_begin;
        page->header_size = state->header_bytes;
        page->body = page_begin + state->header_bytes;
        page->body_size = state->body_bytes;
    }

    const auto page_size = state->header_bytes + state->body_bytes;
    state->unsynced = false;
    state->returned_bytes += page_size;
    state->header_bytes = 0;
    state->body_bytes = 0;
    return static_cast<std::ptrdiff_t>(page_size);
}

int PageOutOggPageWithSyncLossFlag(OggPageParserState* state, OggPageView* page) {
    auto parse_result = ParseOggPage(state, page);
    if (parse_result > 0) {
        return 1;
    }

    while (true) {
        if (parse_result == 0) {
            return 0;
        }
        if (!state->unsynced) {
            state->unsynced = true;
            return -1;
        }

        parse_result = ParseOggPage(state, page);
        if (parse_result > 0) {
            return 1;
        }
    }
}

static bool ReadNextResyncedOggPage(OggPageParserState* parser,
                                    OggPageView* page,
                                    std::size_t* page_begin) {
    if (!parser || !page || !page_begin) {
        return false;
    }

    while (true) {
        *page_begin = parser->returned_bytes;
        const int page_result = PageOutOggPageWithSyncLossFlag(parser, page);
        if (page_result > 0) {
            return true;
        }
        if (page_result == 0) {
            return false;
        }
    }
}

bool ExtractNextVorbisDecodeStream(const std::uint8_t* data,
                                   const std::size_t size,
                                   std::size_t* cursor,
                                   std::vector<std::uint8_t>* stream_bytes) {
    if (!data || !cursor || !stream_bytes || *cursor >= size) {
        return false;
    }

    stream_bytes->clear();

    const std::size_t initial_cursor = *cursor;
    OggPageParserState parser;
    parser.data = data;
    parser.buffered_bytes = size;
    parser.returned_bytes = *cursor;

    bool opening_bos_run_complete = false;
    std::vector<std::uint32_t> opening_bos_serials;
    std::optional<std::uint32_t> selected_serial;
    OggStreamPageInState stream_state;
    std::size_t completed_packets = 0;
    bool skipped_foreign_bos_page = false;

    while (true) {
        std::size_t page_begin = parser.returned_bytes;
        OggPageView page;
        if (!ReadNextResyncedOggPage(&parser, &page, &page_begin)) {
            *cursor = selected_serial.has_value() ? page_begin : initial_cursor;
            stream_bytes->clear();
            return false;
        }

        if (!selected_serial.has_value()) {
            if (page_begin == initial_cursor && !HasOggPageBosFlag(page.page)) {
                *cursor = initial_cursor;
                return false;
            }
            if (!TrackOpeningBosPageSerial(page, &opening_bos_run_complete, &opening_bos_serials)) {
                *cursor = page_begin;
                return false;
            }
            if (!HasOggPageBosFlag(page.page)) {
                *cursor = page_begin;
                return false;
            }

            FirstCompletedPagePacketView first_packet;
            if (!ExtractFirstCompletedPagePacket(page, &first_packet)
                || !IsVorbisIdentificationPacket(first_packet)) {
                continue;
            }

            selected_serial = ReadOggPageSerialNumber(page.page);
        }

        if (ReadOggPageSerialNumber(page.page) != *selected_serial) {
            if (completed_packets < 3 && HasOggPageBosFlag(page.page) && !skipped_foreign_bos_page) {
                skipped_foreign_bos_page = true;
                continue;
            }

            *cursor = page_begin;
            stream_bytes->clear();
            return false;
        }

        if (!PageInOggLogicalStream(&stream_state, page)) {
            *cursor = page_begin;
            stream_bytes->clear();
            return false;
        }

        AppendRawOggPage(page, stream_bytes);
        if (completed_packets < 3) {
            completed_packets = CountCompletedPacketsAtStart(
                std::span<const std::uint8_t>(stream_bytes->data(), stream_bytes->size()), 3);
        }

        if (HasOggPageEosFlag(page.page)) {
            *cursor = parser.returned_bytes;
            return !stream_bytes->empty() && completed_packets >= 3;
        }
    }
}

bool CanonicalizeOpeningOggPacketPrefix(const std::span<const std::uint8_t> logical_stream,
                                        std::vector<std::uint8_t>* canonical_stream,
                                        std::size_t* removed_prefix_bytes) {
    if (!canonical_stream) {
        return false;
    }

    canonical_stream->clear();
    if (removed_prefix_bytes) {
        *removed_prefix_bytes = 0;
    }
    if (logical_stream.empty()) {
        return false;
    }

    std::vector<ParsedOggPageRecord> pages;
    if (!CollectParsedOggPages(logical_stream, &pages)) {
        return false;
    }
    if ((pages.front().header_type & kOggPageContinuedPacketFlag) == 0) {
        return false;
    }

    std::size_t first_visible_page_index = pages.size();
    std::size_t first_page_skip_segments = 0;
    std::size_t first_page_skip_body_bytes = 0;
    bool waiting_for_continued_prefix_end = true;
    bool skipped_packet_continues = true;

    for (std::size_t page_index = 0; page_index < pages.size(); ++page_index) {
        const auto& page = pages[page_index];
        if (!waiting_for_continued_prefix_end) {
            break;
        }

        if ((page.header_type & kOggPageContinuedPacketFlag) == 0 && page_index != 0) {
            if (skipped_packet_continues) {
                return false;
            }
            first_visible_page_index = page_index;
            break;
        }

        const auto* segments = logical_stream.data() + page.offset + kOggPageHeaderBytes;
        const auto skip = CountLeadingContinuedPacketPrefix(segments, page.segment_count);
        if (skip.segment_count == 0) {
            return false;
        }
        if (skip.segment_count < page.segment_count) {
            first_visible_page_index = page_index;
            first_page_skip_segments = skip.segment_count;
            first_page_skip_body_bytes = skip.body_bytes;
            waiting_for_continued_prefix_end = false;
            break;
        }
        skipped_packet_continues = skip.packet_continues;
        if (!skip.packet_continues) {
            first_visible_page_index = page_index + 1;
            waiting_for_continued_prefix_end = false;
            break;
        }
    }

    if (waiting_for_continued_prefix_end || first_visible_page_index >= pages.size()) {
        return false;
    }

    const bool changed = first_visible_page_index != 0 || first_page_skip_segments != 0;
    if (!changed) {
        return false;
    }

    if (removed_prefix_bytes) {
        std::size_t removed = first_page_skip_segments + first_page_skip_body_bytes;
        for (std::size_t page_index = 0; page_index < first_visible_page_index; ++page_index) {
            removed += pages[page_index].header_size + pages[page_index].body_size;
        }
        *removed_prefix_bytes = removed;
    }

    for (std::size_t page_index = first_visible_page_index; page_index < pages.size(); ++page_index) {
        const auto& page = pages[page_index];
        const auto skip_segments =
            page_index == first_visible_page_index ? first_page_skip_segments : 0;
        const auto skip_body_bytes =
            page_index == first_visible_page_index ? first_page_skip_body_bytes : 0;
        AppendCanonicalOggPage(logical_stream,
                               page,
                               skip_segments,
                               skip_body_bytes,
                               page_index == first_visible_page_index,
                               page_index == first_visible_page_index,
                               canonical_stream);
    }

    return !canonical_stream->empty();
}

BufferedOggPageReader::BufferedOggPageReader(const OggPageReaderCallbacks callbacks,
                                             const std::size_t read_chunk_size)
    : callbacks_(callbacks),
      read_chunk_size_(read_chunk_size == 0 ? 8500 : read_chunk_size) {}

OggReadNextPageResult BufferedOggPageReader::ReadNextPage(OggPageView* page,
                                                          const std::int64_t max_scan_bytes) {
    std::optional<std::uint64_t> stop_offset;
    if (max_scan_bytes >= 0) {
        stop_offset = max_scan_bytes == 0
                    ? 0
                    : current_offset() + static_cast<std::uint64_t>(max_scan_bytes);
    }

    while (true) {
        if (stop_offset.has_value() && *stop_offset > 0 && current_offset() >= *stop_offset) {
            return {OggReadNextPageStatus::NoPageWithinLimit, 0};
        }

        const std::uint64_t page_offset = current_offset();
        const auto parse_result = ParseOggPage(&parser_, page);
        if (parse_result > 0) {
            return {OggReadNextPageStatus::Page, page_offset};
        }
        if (parse_result < 0) {
            continue;
        }
        if (stop_offset.has_value() && *stop_offset == 0) {
            return {OggReadNextPageStatus::NoPageWithinLimit, 0};
        }

        const auto read_result = ReadMore();
        switch (read_result.status) {
            case OggReadChunkStatus::Ok:
                break;
            case OggReadChunkStatus::Eof:
                return {OggReadNextPageStatus::Eof, 0};
            case OggReadChunkStatus::Error:
                return {OggReadNextPageStatus::ReadError, 0};
        }
    }
}

void BufferedOggPageReader::Reset(const std::uint64_t absolute_offset) {
    buffer_.clear();
    buffer_offset_ = absolute_offset;
    parser_ = {};
}

std::uint64_t BufferedOggPageReader::current_offset() const {
    return buffer_offset_ + parser_.returned_bytes;
}

OggReadChunkResult BufferedOggPageReader::ReadMore() {
    if (!callbacks_.read) {
        return {OggReadChunkStatus::Error, 0};
    }

    CompactConsumedPrefix();

    const std::size_t previous_size = buffer_.size();
    buffer_.resize(previous_size + read_chunk_size_);
    const auto read_result =
        callbacks_.read(callbacks_.userdata,
                        std::span<std::uint8_t>(buffer_.data() + previous_size,
                                                read_chunk_size_));

    std::size_t bytes_read = read_result.bytes_read;
    OggReadChunkStatus status = read_result.status;
    if (status == OggReadChunkStatus::Ok) {
        if (bytes_read == 0) {
            status = OggReadChunkStatus::Eof;
        } else if (bytes_read > read_chunk_size_) {
            status = OggReadChunkStatus::Error;
            bytes_read = 0;
        }
    } else {
        bytes_read = 0;
    }

    buffer_.resize(previous_size + bytes_read);
    parser_.data = buffer_.empty() ? nullptr : buffer_.data();
    parser_.buffered_bytes = buffer_.size();

    return {status, bytes_read};
}

void BufferedOggPageReader::CompactConsumedPrefix() {
    if (parser_.returned_bytes == 0) {
        return;
    }

    const auto discard = parser_.returned_bytes;
    const auto remaining = buffer_.size() - discard;
    if (remaining > 0) {
        std::memmove(buffer_.data(), buffer_.data() + discard, remaining);
    }
    buffer_.resize(remaining);
    buffer_offset_ += discard;
    parser_.returned_bytes = 0;
    parser_.data = buffer_.empty() ? nullptr : buffer_.data();
    parser_.buffered_bytes = buffer_.size();
}

bool ParseNextLogicalOggStream(const std::uint8_t* data,
                               std::size_t size,
                               std::size_t* cursor,
                               LogicalOggStreamSpan* stream) {
    if (!data || !cursor || !stream || *cursor >= size) {
        return false;
    }

    *stream = {};
    const std::size_t initial_cursor = *cursor;
    OggPageParserState parser;
    parser.data = data;
    parser.buffered_bytes = size;
    parser.returned_bytes = *cursor;

    std::size_t stream_begin = 0;
    bool have_stream_begin = false;
    bool first_page = true;
    bool opening_bos_run_complete = false;
    bool saw_eos = false;
    OggStreamPageInState stream_state;
    std::vector<std::uint32_t> opening_bos_serials;

    while (!saw_eos) {
        const auto page_begin = parser.returned_bytes;
        OggPageView page;
        if (PageOutOggPageWithSyncLossFlag(&parser, &page) <= 0) {
            *cursor = page_begin;
            return false;
        }
        if (!have_stream_begin) {
            stream_begin = page_begin;
            have_stream_begin = true;
        }

        if (first_page) {
            if (!HasOggPageBosFlag(page.page)) {
                *cursor = initial_cursor;
                return false;
            }
            first_page = false;
        }
        if (!TrackOpeningBosPageSerial(page,
                                       &opening_bos_run_complete,
                                       &opening_bos_serials)) {
            *cursor = page_begin;
            return false;
        }

        if (!PageInOggLogicalStream(&stream_state, page)) {
            *cursor = page_begin;
            return false;
        }

        saw_eos = HasOggPageEosFlag(page.page);
    }

    *cursor = parser.returned_bytes;
    stream->data = data + stream_begin;
    stream->size = *cursor - stream_begin;
    return stream->size != 0;
}

}
