#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data {

enum class ADTChunkType : uint32_t {
    MVER = 0,
    MHDR = 1,
    MCIN = 2,
    MTEX = 3,
    MMDX = 4,
    MMID = 5,
    MWMO = 6,
    MWID = 7,
    MDDF = 8,
    MODF = 9,
    MH2O = 10,
    MCNK = 11,
    MFBO = 12,
    MTXF = 13,
};

struct ADTSubchunkInfo {
    ADTChunkType chunkType = ADTChunkType::MVER;
    uint32_t     offset    = 0;
    uint32_t     size      = 0;
};

struct MCNKInfo {
    uint32_t indexX         = 0;
    uint32_t indexY         = 0;
    uint32_t areaId         = 0;
    uint32_t holes          = 0;
    uint32_t numLayers      = 0;
    uint32_t numDoodadRefs  = 0;
    uint32_t numObjectRefs  = 0;
    bool     hasFlightBox   = false;
};

class ADTChunkIndex {
public:
    void AddChunk(ADTChunkType type, uint32_t offset, uint32_t size);
    std::optional<ADTSubchunkInfo> GetChunk(ADTChunkType type) const;
    bool HasChunk(ADTChunkType type) const;
    std::vector<ADTSubchunkInfo> GetAllChunks() const;
    uint32_t GetChunkCount() const;

    void AddMCNK(const MCNKInfo& info);
    std::optional<MCNKInfo> GetMCNK(uint32_t x, uint32_t y) const;
    std::vector<MCNKInfo> GetAllMCNKs() const;
    uint32_t GetMCNKCount() const;

    void SetTextureNames(const std::vector<std::string>& names);
    std::vector<std::string> GetTextureNames() const;
    uint32_t GetTextureCount() const;

    static std::string GetChunkTypeName(ADTChunkType type);

    void Clear();

private:
    std::unordered_map<uint32_t, ADTSubchunkInfo> chunks_;

    std::unordered_map<uint32_t, MCNKInfo> mcnks_;
    std::vector<std::string> textureNames_;
};

}
