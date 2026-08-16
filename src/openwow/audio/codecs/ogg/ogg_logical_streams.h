
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::audio {

struct OggPageView {
    const std::uint8_t* page{nullptr};
    std::size_t header_size{0};
    const std::uint8_t* body{nullptr};
    std::size_t body_size{0};

    [[nodiscard]] std::size_t TotalSize() const {
        return header_size + body_size;
    }
};

struct OggPageParserState {
    const std::uint8_t* data{nullptr};
    std::size_t buffered_bytes{0};
    std::size_t returned_bytes{0};
    bool unsynced{false};
    std::size_t header_bytes{0};
    std::size_t body_bytes{0};
};

[[nodiscard]] bool HasOggPageContinuedPacketFlag(const std::uint8_t* page);

[[nodiscard]] bool HasOggPageBosFlag(const std::uint8_t* page);

[[nodiscard]] bool HasOggPageEosFlag(const std::uint8_t* page);

[[nodiscard]] std::int64_t ReadOggPageGranulePosition(const std::uint8_t* page);

[[nodiscard]] std::uint32_t ReadOggPageSerialNumber(const std::uint8_t* page);

[[nodiscard]] std::ptrdiff_t ParseOggPage(OggPageParserState* state, OggPageView* page);

[[nodiscard]] int PageOutOggPageWithSyncLossFlag(OggPageParserState* state, OggPageView* page);

enum class OggReadChunkStatus {
    Ok,
    Eof,
    Error,
};

struct OggReadChunkResult {
    OggReadChunkStatus status{OggReadChunkStatus::Error};
    std::size_t bytes_read{0};
};

using OggPageReadCallback = OggReadChunkResult (*)(void* userdata,
                                                   std::span<std::uint8_t> buffer);

struct OggPageReaderCallbacks {
    OggPageReadCallback read{nullptr};
    void* userdata{nullptr};
};

enum class OggReadNextPageStatus {
    Page,
    NoPageWithinLimit,
    Eof,
    ReadError,
};

struct OggReadNextPageResult {
    OggReadNextPageStatus status{OggReadNextPageStatus::ReadError};
    std::uint64_t page_offset{0};
};

class BufferedOggPageReader {
public:
    explicit BufferedOggPageReader(OggPageReaderCallbacks callbacks,
                                   std::size_t read_chunk_size = 8500);

    [[nodiscard]] OggReadNextPageResult ReadNextPage(OggPageView* page,
                                                     std::int64_t max_scan_bytes = -1);
    void Reset(std::uint64_t absolute_offset = 0);
    [[nodiscard]] std::uint64_t current_offset() const;

private:
    [[nodiscard]] OggReadChunkResult ReadMore();
    void CompactConsumedPrefix();

    OggPageReaderCallbacks callbacks_{};
    std::vector<std::uint8_t> buffer_;
    OggPageParserState parser_{};
    std::uint64_t buffer_offset_{0};
    std::size_t read_chunk_size_{8500};
};

struct LogicalOggStreamSpan {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

[[nodiscard]] bool ExtractNextVorbisDecodeStream(const std::uint8_t* data,
                                                 std::size_t size,
                                                 std::size_t* cursor,
                                                 std::vector<std::uint8_t>* stream_bytes);

[[nodiscard]] bool CanonicalizeOpeningOggPacketPrefix(std::span<const std::uint8_t> logical_stream,
                                                      std::vector<std::uint8_t>* canonical_stream,
                                                      std::size_t* removed_prefix_bytes = nullptr);

[[nodiscard]] bool ParseNextLogicalOggStream(const std::uint8_t* data,
                                             std::size_t size,
                                             std::size_t* cursor,
                                             LogicalOggStreamSpan* stream);

}
