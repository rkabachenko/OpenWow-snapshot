#include "openwow/game/tabard_renderer.h"

#include "openwow/data/blp/blp_texture_loader.h"
#include "openwow/foundation/compiler/printf_format.h"

#include <bit>
#include <cstdarg>
#include <cstdio>
#include <functional>

namespace openwow::game {

namespace {

constexpr std::uint32_t kTabardEmblemRenderTargetWidth = 0x80u;
constexpr std::uint8_t kTabardEmblemRenderTargetMipLevel = 2u;

const char* TabardHalfSuffix(TabardTextureHalf half, const char* upper_suffix,
                             const char* lower_suffix) {
    switch (half) {
    case TabardTextureHalf::Upper:
        return upper_suffix;
    case TabardTextureHalf::Lower:
        return lower_suffix;
    }

    return "";
}

[[nodiscard]] const char* TabardEmblemRenderTargetName(TabardTextureHalf half) {
    return TabardHalfSuffix(half, "TabardModelFrameUpper", "TabardModelFrameLower");
}

[[nodiscard]] std::uint32_t TabardEmblemRenderTargetHeight(TabardTextureHalf half) {
    return half == TabardTextureHalf::Upper ? 0x40u : 0x20u;
}

constexpr std::size_t kTabardTexturePathBytes = 260;

OPENWOW_PRINTF_FORMAT(1, 2)
std::string FormatTabardTexturePath(const char* format, ...) {
    char buffer[kTabardTexturePathBytes] = {};
    std::va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written <= 0) {
        return {};
    }
    return std::string(buffer);
}

[[nodiscard]] std::int32_t SignedTabardTextureIndex(std::uint32_t raw_index) {
    return std::bit_cast<std::int32_t>(raw_index);
}

}

std::string BuildGuildTabardBackgroundTexturePath(
    TabardTextureHalf half, std::uint32_t background_index) {
    return FormatTabardTexturePath(
        "Textures\\GuildEmblems\\Background_%02d_%s",
        SignedTabardTextureIndex(background_index),
        TabardHalfSuffix(half, "TU_U", "TL_U"));
}

std::string BuildGuildTabardEmblemTexturePath(
    TabardTextureHalf half, std::uint32_t emblem_style, std::uint32_t emblem_color) {
    return FormatTabardTexturePath(
        "Textures\\GuildEmblems\\Emblem_%02d_%02d_%s",
        SignedTabardTextureIndex(emblem_style),
        SignedTabardTextureIndex(emblem_color),
        TabardHalfSuffix(half, "TU_U", "TL_U"));
}

std::string BuildGuildTabardBorderTexturePath(
    TabardTextureHalf half, std::uint32_t border_style, std::uint32_t border_color) {
    return FormatTabardTexturePath(
        "Textures\\GuildEmblems\\Border_%02d_%02d_%s",
        SignedTabardTextureIndex(border_style),
        SignedTabardTextureIndex(border_color),
        TabardHalfSuffix(half, "TU_U", "TL_U"));
}

TabardEmblemRenderTargetDescriptor BuildGuildTabardEmblemRenderTargetDescriptor(
    TabardTextureHalf half, std::uint32_t emblem_style, std::uint32_t emblem_color) {
    TabardEmblemRenderTargetDescriptor descriptor;
    descriptor.sourceTexturePath =
        BuildGuildTabardEmblemTexturePath(half, emblem_style, emblem_color) + ".BLP";
    descriptor.renderTargetName = TabardEmblemRenderTargetName(half);
    descriptor.width = kTabardEmblemRenderTargetWidth;
    descriptor.height = TabardEmblemRenderTargetHeight(half);
    descriptor.pitch = descriptor.width * 4u;
    descriptor.forceWhiteRgb = true;
    descriptor.copySourceAlpha = true;
    return descriptor;
}

std::optional<TabardEmblemRenderTargetImage> TryBuildGuildTabardEmblemRenderTargetImage(
    const TabardEmblemRenderTargetDescriptor& descriptor,
    const std::vector<std::uint8_t>& source_texture_bytes) {
    if (descriptor.width == 0 || descriptor.height == 0) {
        return std::nullopt;
    }

    const auto parsed_texture = openwow::data::BLPTextureLoader::Load(source_texture_bytes);
    if (!parsed_texture.isValid ||
        parsed_texture.mipCount <= kTabardEmblemRenderTargetMipLevel) {
        return std::nullopt;
    }

    auto source_mip_rgba = openwow::data::BLPTextureLoader::DecompressMip(
        parsed_texture, kTabardEmblemRenderTargetMipLevel);
    const std::size_t expected_bytes =
        static_cast<std::size_t>(descriptor.width) *
        static_cast<std::size_t>(descriptor.height) * 4u;
    if (source_mip_rgba.size() != expected_bytes) {
        return std::nullopt;
    }

    TabardEmblemRenderTargetImage image;
    image.width = descriptor.width;
    image.height = descriptor.height;
    image.pitch = descriptor.pitch;
    image.pixelsRgba.resize(expected_bytes);

    for (std::size_t offset = 0; offset < expected_bytes; offset += 4u) {
        if (descriptor.forceWhiteRgb) {
            image.pixelsRgba[offset + 0] = 0xFF;
            image.pixelsRgba[offset + 1] = 0xFF;
            image.pixelsRgba[offset + 2] = 0xFF;
        } else {
            image.pixelsRgba[offset + 0] = source_mip_rgba[offset + 0];
            image.pixelsRgba[offset + 1] = source_mip_rgba[offset + 1];
            image.pixelsRgba[offset + 2] = source_mip_rgba[offset + 2];
        }

        image.pixelsRgba[offset + 3] =
            descriptor.copySourceAlpha ? source_mip_rgba[offset + 3] : 0xFF;
    }

    return image;
}

static constexpr uint32_t kTabardBackgroundBase = 70000;
static constexpr uint32_t kTabardBorderBase     = 71000;
static constexpr uint32_t kTabardEmblemBase     = 72000;

void TabardRenderer::SetDesign(const TabardDesign& design) {
    design_ = design;
}

TabardDesign TabardRenderer::GetDesign() const {
    return design_;
}

std::vector<TabardTextureLayer> TabardRenderer::GetTextureLayers() const {
    std::vector<TabardTextureLayer> layers;
    layers.reserve(3);

    {
        TabardTextureLayer l;
        l.component     = TabardComponent::Background;
        l.textureFileId = GetBackgroundTextureId();
        l.tintColor     = design_.backgroundColor;
        layers.push_back(l);
    }

    {
        TabardTextureLayer l;
        l.component     = TabardComponent::Border;
        l.textureFileId = GetBorderTextureId(design_.borderStyle);
        l.tintColor     = design_.borderColor;
        layers.push_back(l);
    }

    {
        TabardTextureLayer l;
        l.component     = TabardComponent::Emblem;
        l.textureFileId = GetEmblemTextureId(design_.emblemStyle);
        l.tintColor     = design_.emblemColor;
        layers.push_back(l);
    }

    return layers;
}

bool TabardRenderer::IsValidDesign() const {
    return design_.emblemStyle < kTabardMaxEmblemStyles
        && design_.borderStyle < kTabardMaxBorderStyles;
}

uint32_t TabardRenderer::GetEmblemTextureId(uint32_t emblemStyle) const {
    return kTabardEmblemBase + emblemStyle;
}

uint32_t TabardRenderer::GetBorderTextureId(uint32_t borderStyle) const {
    return kTabardBorderBase + borderStyle;
}

uint32_t TabardRenderer::GetBackgroundTextureId() const {
    return kTabardBackgroundBase;
}

uint64_t TabardRenderer::GetCompositeHash() const {

    uint64_t hash = 14695981039346656037ULL;
    auto mix = [&hash](uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ULL;
    };
    mix(design_.emblemStyle);
    mix(design_.emblemColor.r);
    mix(design_.emblemColor.g);
    mix(design_.emblemColor.b);
    mix(design_.borderStyle);
    mix(design_.borderColor.r);
    mix(design_.borderColor.g);
    mix(design_.borderColor.b);
    mix(design_.backgroundColor.r);
    mix(design_.backgroundColor.g);
    mix(design_.backgroundColor.b);
    return hash;
}

void TabardRenderer::SetGuildName(const std::string& name) {
    guildName_ = name;
}

std::string TabardRenderer::GetGuildName() const {
    return guildName_;
}

bool TabardRenderer::HasGuild() const {
    return !guildName_.empty();
}

bool TabardRenderer::IsDefault() const {
    return design_.emblemStyle == 0
        && design_.borderStyle == 0
        && design_.emblemColor == TabardColor{}
        && design_.borderColor == TabardColor{}
        && design_.backgroundColor == TabardColor{};
}

void TabardRenderer::Reset() {
    design_    = {};
    guildName_.clear();
}

}
