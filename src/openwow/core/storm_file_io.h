
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace openwow::core {

class StormFileIO {
public:
    StormFileIO() = default;
    virtual ~StormFileIO();

    virtual bool Read(void* buffer, int64_t offset, uint32_t bytes_to_read,
                      uint32_t* bytes_read = nullptr);

    bool ReadAllowShort(void* buffer, int64_t offset, uint32_t bytes_to_read,
                        uint32_t* bytes_read);

    virtual bool Write(const void* buffer, int64_t offset,
                       uint32_t bytes_to_write);

    virtual bool Flush();

    virtual bool GetSize(uint64_t* out_size);

    virtual bool Truncate(int64_t offset);

    bool SetLastWriteTimeNsSince2000(std::int64_t time_ns_since_2000);

    bool QueryPosition(int64_t* out_offset);
    bool Seek(int64_t offset);

    virtual bool GetCachedSize(uint64_t* out_size) const;

    virtual bool GetCachedSizeParts(std::uint32_t* out_parts) const;

    virtual bool Open(const char* path, const char* mode);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }

protected:
    FILE* file_ = nullptr;
    int64_t current_pos_ = 0;
    uint64_t cached_size_ = 0;
};

class StormBufferedFileIO : public StormFileIO {
public:
    StormBufferedFileIO();
    ~StormBufferedFileIO() override;

    bool Open(const char* path, const char* mode) override;

    bool BufferedRead(void* dest, uint32_t flags, int64_t offset,
                      uint32_t size, uint32_t* bytes_read = nullptr);

    bool BufferedWrite(const void* src, int64_t offset, uint32_t size);

    bool SeekAligned(int64_t offset);

    bool GetCachedSize(uint64_t* out_size) const override;
    bool GetCachedSizeParts(std::uint32_t* out_parts) const override;

    void SetAlignmentMask(uint32_t mask) { alignment_mask_ = mask; }
    uint32_t alignment_mask() const { return alignment_mask_; }

protected:
    static constexpr uint32_t kMaxChunkSize = 0x100000;

    void SetLogicalEnd(std::uint64_t value) { logical_end_ = value; }
    [[nodiscard]] std::uint64_t logical_end() const { return logical_end_; }

private:
    bool FinalizeLogicalSize();
    void UpdateLogicalEnd(std::uint64_t value);

    uint8_t* io_buffer_ = nullptr;
    uint32_t io_buffer_size_ = 0;
    uint32_t alignment_mask_ = 0;
    std::uint64_t logical_end_ = 0;
    std::string native_path_;
};

}
