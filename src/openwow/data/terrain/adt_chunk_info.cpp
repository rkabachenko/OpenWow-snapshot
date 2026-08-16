#include "openwow/data/terrain/adt_chunk_info.h"

namespace openwow::data {

void ADTChunkIndex::AddChunk(ADTChunkType type, uint32_t offset, uint32_t size) {
    ADTSubchunkInfo info;
    info.chunkType = type;
    info.offset    = offset;
    info.size      = size;
    chunks_[static_cast<uint32_t>(type)] = info;
}

std::optional<ADTSubchunkInfo> ADTChunkIndex::GetChunk(ADTChunkType type) const {
    auto it = chunks_.find(static_cast<uint32_t>(type));
    if (it == chunks_.end()) return std::nullopt;
    return it->second;
}

bool ADTChunkIndex::HasChunk(ADTChunkType type) const {
    return chunks_.count(static_cast<uint32_t>(type)) > 0;
}

std::vector<ADTSubchunkInfo> ADTChunkIndex::GetAllChunks() const {
    std::vector<ADTSubchunkInfo> out;
    out.reserve(chunks_.size());
    for (auto& [k, v] : chunks_) out.push_back(v);
    return out;
}

uint32_t ADTChunkIndex::GetChunkCount() const {
    return static_cast<uint32_t>(chunks_.size());
}

void ADTChunkIndex::AddMCNK(const MCNKInfo& info) {
    if (info.indexX >= 16 || info.indexY >= 16) return;
    uint32_t key = info.indexY * 16 + info.indexX;
    mcnks_[key] = info;
}

std::optional<MCNKInfo> ADTChunkIndex::GetMCNK(uint32_t x, uint32_t y) const {
    if (x >= 16 || y >= 16) return std::nullopt;
    uint32_t key = y * 16 + x;
    auto it = mcnks_.find(key);
    if (it == mcnks_.end()) return std::nullopt;
    return it->second;
}

std::vector<MCNKInfo> ADTChunkIndex::GetAllMCNKs() const {
    std::vector<MCNKInfo> out;
    out.reserve(mcnks_.size());
    for (auto& [k, v] : mcnks_) out.push_back(v);
    return out;
}

uint32_t ADTChunkIndex::GetMCNKCount() const {
    return static_cast<uint32_t>(mcnks_.size());
}

void ADTChunkIndex::SetTextureNames(const std::vector<std::string>& names) {
    textureNames_ = names;
}

std::vector<std::string> ADTChunkIndex::GetTextureNames() const {
    return textureNames_;
}

uint32_t ADTChunkIndex::GetTextureCount() const {
    return static_cast<uint32_t>(textureNames_.size());
}

std::string ADTChunkIndex::GetChunkTypeName(ADTChunkType type) {
    switch (type) {
        case ADTChunkType::MVER: return "MVER";
        case ADTChunkType::MHDR: return "MHDR";
        case ADTChunkType::MCIN: return "MCIN";
        case ADTChunkType::MTEX: return "MTEX";
        case ADTChunkType::MMDX: return "MMDX";
        case ADTChunkType::MMID: return "MMID";
        case ADTChunkType::MWMO: return "MWMO";
        case ADTChunkType::MWID: return "MWID";
        case ADTChunkType::MDDF: return "MDDF";
        case ADTChunkType::MODF: return "MODF";
        case ADTChunkType::MH2O: return "MH2O";
        case ADTChunkType::MCNK: return "MCNK";
        case ADTChunkType::MFBO: return "MFBO";
        case ADTChunkType::MTXF: return "MTXF";
    }
    return "UNKNOWN";
}

void ADTChunkIndex::Clear() {
    chunks_.clear();
    mcnks_.clear();
    textureNames_.clear();
}

}
