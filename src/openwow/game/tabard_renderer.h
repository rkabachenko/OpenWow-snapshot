#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class TabardComponent : uint8_t {
    Background = 0,
    Border     = 1,
    Emblem     = 2
};

enum class TabardTextureHalf : uint8_t {
    Upper = 0,
    Lower = 1
};

struct TabardColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const TabardColor& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const TabardColor& o) const { return !(*this == o); }
};

struct TabardDesign {
    uint32_t   emblemStyle     = 0;
    TabardColor emblemColor    = {};
    uint32_t   borderStyle     = 0;
    TabardColor borderColor    = {};
    TabardColor backgroundColor = {};
};

struct TabardTextureLayer {
    TabardComponent component    = TabardComponent::Background;
    uint32_t        textureFileId = 0;
    TabardColor     tintColor    = {};
};

struct TabardEmblemRenderTargetDescriptor {
    std::string   sourceTexturePath;
    std::string   renderTargetName;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
    bool          forceWhiteRgb = false;
    bool          copySourceAlpha = false;
};

struct TabardEmblemRenderTargetImage {
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    std::uint32_t             pitch = 0;
    std::vector<std::uint8_t> pixelsRgba;
};

inline constexpr uint32_t kTabardMaxEmblemStyles = 170;
inline constexpr uint32_t kTabardMaxBorderStyles = 6;

[[nodiscard]] std::string BuildGuildTabardBackgroundTexturePath(
    TabardTextureHalf half, std::uint32_t background_index);
[[nodiscard]] std::string BuildGuildTabardEmblemTexturePath(
    TabardTextureHalf half, std::uint32_t emblem_style, std::uint32_t emblem_color);
[[nodiscard]] std::string BuildGuildTabardBorderTexturePath(
    TabardTextureHalf half, std::uint32_t border_style, std::uint32_t border_color);
[[nodiscard]] TabardEmblemRenderTargetDescriptor
BuildGuildTabardEmblemRenderTargetDescriptor(
    TabardTextureHalf half, std::uint32_t emblem_style, std::uint32_t emblem_color);
[[nodiscard]] std::optional<TabardEmblemRenderTargetImage>
TryBuildGuildTabardEmblemRenderTargetImage(
    const TabardEmblemRenderTargetDescriptor& descriptor,
    const std::vector<std::uint8_t>& source_texture_bytes);

class TabardRenderer {
public:
    void SetDesign(const TabardDesign& design);
    TabardDesign GetDesign() const;

    std::vector<TabardTextureLayer> GetTextureLayers() const;
    bool IsValidDesign() const;

    uint32_t GetEmblemTextureId(uint32_t emblemStyle) const;
    uint32_t GetBorderTextureId(uint32_t borderStyle) const;
    uint32_t GetBackgroundTextureId() const;
    uint64_t GetCompositeHash() const;

    void SetGuildName(const std::string& name);
    std::string GetGuildName() const;
    bool HasGuild() const;
    bool IsDefault() const;
    void Reset();

private:
    TabardDesign design_;
    std::string  guildName_;
};

}
