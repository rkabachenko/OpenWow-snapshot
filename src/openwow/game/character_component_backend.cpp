#include "openwow/game/character_component_backend.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/platform/system/os_system_info.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string_view>

namespace openwow::game {

namespace {

CharacterComponentBackendRuntimeState g_character_component_backend_runtime_state;
constexpr std::size_t kCharacterComponentInitialGeneralMipChainCount = 10;
constexpr std::size_t kCharacterComponentCompressedGeneralMipChainCount = 1;
constexpr std::size_t kCharacterComponentCompressedSpecialMipChainCount = 9;
constexpr int kCharacterComponentGeneralMipChainFormat = 2;
constexpr int kCharacterComponentSpecialMipChainFormat = 0;
constexpr char kCharacterComponentWorkerThreadName[] = "Component";
constexpr std::size_t kCharacterComponentPage0Index = 0;
constexpr std::size_t kCharacterComponentPage1Index = 1;
constexpr std::size_t kCharacterComponentPage2Index = 2;
constexpr std::size_t kCharacterComponentPage3Index = 3;
constexpr std::size_t kCharacterComponentPage4Index = 4;
constexpr std::size_t kCharacterComponentPage5Index = 5;
constexpr std::size_t kCharacterComponentPage6Index = 6;
constexpr std::size_t kCharacterComponentPage7Index = 7;
constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage0SlotByItemSlot = {-1, -1, 0, 1, -1, -1, -1, -1, -1, -1, 0, 0};
constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage1SlotByItemSlot = {-1, -1, 0, 1, -1, -1, -1, 2, 3, -1, 0, 0};
constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage2SlotByItemSlot = {-1, -1, -1, -1, -1, -1, -1, -1, 0, -1, 0, 0};
constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage3SlotByItemSlot = {-1, -1, 0, 1, -1, -1, -1, -1, -1, 4, 0, 0};
constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage4SlotByItemSlot = {-1, -1, 0, 1, 5, -1, -1, -1, -1, 4, 0, 0};
constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage5SlotByItemSlot = {-1, -1, -1, 1, 2, 0, -1, -1, -1, -1, 0, 0};

constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage6BaseSlotByItemSlot = {-1, -1, -1, 1, -1, 0, 2, -1, -1, -1, 0, 0};

constexpr std::array<int, kCharacterModelVisualItemSlotCount>
    kCharacterComponentPage7SlotByItemSlot = {-1, -1, -1, -1, -1, -1, 0, -1, -1, -1, 0, 0};
constexpr std::size_t kCharacterModelChestItemSlot = 3;
constexpr std::size_t kCharacterModelGlovesItemSlot = 8;
constexpr std::size_t kCharacterModelWaistItemSlot = 5;
constexpr std::size_t kCharacterModelLegsItemSlot = 6;
constexpr int kCharacterComponentPage1ChestLayerSlot = 5;
constexpr int kCharacterComponentPage1GlovesLayerSlot = 6;
constexpr int kCharacterComponentPage6ChestLayerSlot = 4;
constexpr int kCharacterComponentPage6WaistPrimaryLayerSlot = 3;
constexpr int kCharacterComponentPage6WaistFallbackLayerSlot = 4;
constexpr std::uint32_t kCharacterComponentSectionLookupBaseSection = 0u;
constexpr std::uint32_t kCharacterComponentSectionLookupBaseType = 0u;

template <typename T> [[nodiscard]] T ClampTextureLevel(T value, T lo, T hi) {
  return std::clamp(value, lo, hi);
}

[[nodiscard]] bool PageTokensMatch(const CharacterComponentTextureHandle &lhs,
                                   const CharacterComponentTextureHandle &rhs,
                                   std::size_t page_index) {
  if (page_index >= lhs.page_tokens.size() || page_index >= rhs.page_tokens.size()) {
    return false;
  }
  return lhs.page_tokens[page_index] == rhs.page_tokens[page_index];
}

[[nodiscard]] std::int32_t ArithmeticShiftRightOne(std::int32_t value) {
  if (value >= 0) {
    return value / 2;
  }

  return -static_cast<std::int32_t>((static_cast<std::uint32_t>(-value) + 1u) / 2u);
}

[[nodiscard]] std::uint32_t HalveDimensionClampOne(std::uint32_t value) {
  return std::max(value >> 1, 1u);
}

[[nodiscard]] std::uint32_t MipDimension(std::uint16_t value, std::uint32_t level) {
  return std::max(static_cast<std::uint32_t>(value) >> level, 1u);
}

[[nodiscard]] constexpr std::uint32_t CharacterComponentLookupRowIndex(const std::uint32_t race,
                                                                       const std::uint32_t gender) {
  return 2u * race + gender;
}

constexpr std::uint32_t kCharacterBaseSkinArgbBytesPerPixel = 4u;
constexpr std::uint32_t kCharacterBaseSkinBc1BlockBytes = 8u;

[[nodiscard]] std::shared_ptr<CharacterComponentMipChain>
CloneCharacterBaseSkinUploadSnapshot(const std::shared_ptr<CharacterComponentMipChain> &source) {
  if (!source) {
    return nullptr;
  }

  return std::make_shared<CharacterComponentMipChain>(*source);
}

[[nodiscard]] std::shared_ptr<CharacterComponentMipChain>
CreateCharacterBaseSkinUploadSnapshot(const int request_type) {
  auto snapshot = std::make_shared<CharacterComponentMipChain>();
  std::uint32_t edge = g_character_component_backend_runtime_state.config.composite_texture_edge;
  if (edge == 0u) {
    edge = 256u;
  }

  std::uint32_t width = edge;
  std::uint32_t height = edge;
  while (true) {
    if (request_type == 6) {
      const std::uint32_t block_width = std::max(width >> 2u, 1u);
      const std::uint32_t block_height = std::max(height >> 2u, 1u);
      CharacterComponentMipChain::Bc1MipLevel level;
      level.width = width;
      level.height = height;
      level.blocks.assign(static_cast<std::size_t>(block_width) * block_height *
                              kCharacterBaseSkinBc1BlockBytes,
                          0u);
      snapshot->bc1_mip_levels.push_back(std::move(level));
    } else {
      CharacterComponentMipChain::MipLevelSurface level;
      level.width = width;
      level.height = height;
      level.pixels.assign(static_cast<std::size_t>(width) * height, 0u);
      snapshot->mip_levels.push_back(std::move(level));
    }

    if (width == 1u && height == 1u) {
      break;
    }

    width = std::max(width >> 1u, 1u);
    height = std::max(height >> 1u, 1u);
  }

  return snapshot;
}

void EnsureCharacterBaseSkinUploadSnapshot(CharacterBaseSkinRenderTargetState &base_skin,
                                           const int request_type) {
  auto &snapshot =
      (request_type == 6) ? base_skin.special_upload_snapshot : base_skin.general_upload_snapshot;
  if (!snapshot) {
    snapshot = CreateCharacterBaseSkinUploadSnapshot(request_type);
  }
}

void RememberCharacterBaseSkinUploadSnapshot(
    CharacterBaseSkinRenderTargetState &base_skin, const int request_type,
    const std::shared_ptr<CharacterComponentMipChain> &source_snapshot) {
  if (!source_snapshot) {
    return;
  }

  auto &snapshot =
      (request_type == 6) ? base_skin.special_upload_snapshot : base_skin.general_upload_snapshot;
  snapshot = CloneCharacterBaseSkinUploadSnapshot(source_snapshot);
}

struct CharacterBaseSkinUploadSelection {
  CharacterBaseSkinUploadSourceKind source = CharacterBaseSkinUploadSourceKind::None;
  const CharacterComponentMipChain *chain = nullptr;
};

[[nodiscard]] CharacterBaseSkinUploadSelection
SelectCharacterBaseSkinUploadSource(const CharacterModelRefreshState &model) {
  if (model.pending_request && model.pending_request->output_chain) {
    return {CharacterBaseSkinUploadSourceKind::PendingRequest,
            model.pending_request->output_chain.get()};
  }

  if (model.request_type == 6 && model.base_skin_render_target.special_upload_snapshot) {
    return {CharacterBaseSkinUploadSourceKind::SpecialFallback,
            model.base_skin_render_target.special_upload_snapshot.get()};
  }

  if (model.request_type != 6 && model.base_skin_render_target.general_upload_snapshot) {
    return {CharacterBaseSkinUploadSourceKind::GeneralFallback,
            model.base_skin_render_target.general_upload_snapshot.get()};
  }

  return {};
}

[[nodiscard]] const void *
ResolveCharacterBaseSkinUploadDataPointer(const CharacterComponentMipChain *chain,
                                          const int request_type, const int mip_level) {
  if (!chain || mip_level < 0) {
    return nullptr;
  }

  const auto level_index = static_cast<std::size_t>(mip_level);
  if (request_type == 6) {
    if (level_index >= chain->bc1_mip_levels.size()) {
      return nullptr;
    }

    const auto &level = chain->bc1_mip_levels[level_index];
    return level.blocks.empty() ? nullptr : static_cast<const void *>(level.blocks.data());
  }

  if (level_index >= chain->mip_levels.size()) {
    return nullptr;
  }

  const auto &level = chain->mip_levels[level_index];
  return level.pixels.empty() ? nullptr : static_cast<const void *>(level.pixels.data());
}

[[nodiscard]] std::uint32_t PaletteBgraToOpaqueArgb(std::uint32_t palette_bgra) {
  const auto blue = palette_bgra & 0xFFu;
  const auto green = (palette_bgra >> 8) & 0xFFu;
  const auto red = (palette_bgra >> 16) & 0xFFu;
  return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

[[nodiscard]] std::uint8_t BlendOpaqueChannel(std::uint8_t destination, std::uint8_t source,
                                              std::uint32_t alpha) {
  return static_cast<std::uint8_t>((((255u - alpha) * destination) + (alpha * source)) >> 8);
}

void AssertRegionFitsSurface(
    [[maybe_unused]] const CharacterComponentMipChain::MipLevelSurface &surface,
    [[maybe_unused]] CharacterComponentMipFillRegion region) {
  assert(region.x >= 0);
  assert(region.y >= 0);
  assert(region.width <= surface.width);
  assert(region.height <= surface.height);
  assert(static_cast<std::uint32_t>(region.x) <= surface.width - region.width);
  assert(static_cast<std::uint32_t>(region.y) <= surface.height - region.height);
  assert(surface.pixels.size() == static_cast<std::size_t>(surface.width) * surface.height);
}

[[nodiscard]] const std::vector<std::uint8_t> &
RequireSourceMipPayload(const CharacterComponentPalettedTexture &source_texture,
                        std::uint32_t level) {
  assert(level < source_texture.mip_payloads.size());
  return source_texture.mip_payloads[level];
}

struct CharacterComponentRgbSample {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

[[nodiscard]] CharacterComponentMipFillRegion
HalveMipFillRegion(CharacterComponentMipFillRegion region) {
  region.x = ArithmeticShiftRightOne(region.x);
  region.y = ArithmeticShiftRightOne(region.y);
  region.width = HalveDimensionClampOne(region.width);
  region.height = HalveDimensionClampOne(region.height);
  return region;
}

[[nodiscard]] CharacterComponentMipOrigin HalveMipOrigin(CharacterComponentMipOrigin origin) {
  origin.x = ArithmeticShiftRightOne(origin.x);
  origin.y = ArithmeticShiftRightOne(origin.y);
  return origin;
}

[[nodiscard]] CharacterComponentRgbSample PaletteEntryToRgb(std::uint32_t palette_bgra) {
  CharacterComponentRgbSample sample;
  sample.blue = static_cast<std::uint8_t>(palette_bgra & 0xFFu);
  sample.green = static_cast<std::uint8_t>((palette_bgra >> 8) & 0xFFu);
  sample.red = static_cast<std::uint8_t>((palette_bgra >> 16) & 0xFFu);
  return sample;
}

[[nodiscard]] std::uint32_t MakeOpaqueArgb(CharacterComponentRgbSample sample) {
  return 0xFF000000u | (static_cast<std::uint32_t>(sample.red) << 16) |
         (static_cast<std::uint32_t>(sample.green) << 8) | static_cast<std::uint32_t>(sample.blue);
}

struct CharacterComponentBc1Vector {
  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
};

[[nodiscard]] CharacterComponentBc1Vector Add(CharacterComponentBc1Vector lhs,
                                              CharacterComponentBc1Vector rhs) {
  lhs.red += rhs.red;
  lhs.green += rhs.green;
  lhs.blue += rhs.blue;
  return lhs;
}

[[nodiscard]] CharacterComponentBc1Vector Subtract(CharacterComponentBc1Vector lhs,
                                                   CharacterComponentBc1Vector rhs) {
  lhs.red -= rhs.red;
  lhs.green -= rhs.green;
  lhs.blue -= rhs.blue;
  return lhs;
}

[[nodiscard]] CharacterComponentBc1Vector Multiply(CharacterComponentBc1Vector value,
                                                   float scalar) {
  value.red *= scalar;
  value.green *= scalar;
  value.blue *= scalar;
  return value;
}

[[nodiscard]] float Dot(CharacterComponentBc1Vector lhs, CharacterComponentBc1Vector rhs) {
  return lhs.red * rhs.red + lhs.green * rhs.green + lhs.blue * rhs.blue;
}

[[nodiscard]] CharacterComponentBc1Vector NormalizeAxis(CharacterComponentBc1Vector axis) {
  const auto length = std::sqrt(Dot(axis, axis));
  if (length == 0.0f) {
    return {};
  }

  return Multiply(axis, 1.0f / length);
}

[[nodiscard]] CharacterComponentBc1Vector PixelToBc1Vector(std::uint32_t pixel) {
  return {
      static_cast<float>((pixel >> 16) & 0xFFu) / 255.0f,
      static_cast<float>((pixel >> 8) & 0xFFu) / 255.0f,
      static_cast<float>(pixel & 0xFFu) / 255.0f,
  };
}

[[nodiscard]] std::uint16_t PackRgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return static_cast<std::uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

[[nodiscard]] std::uint8_t QuantizeChannel(float value) {
  const auto scaled = static_cast<int>(value * 255.0f + 0.5f);
  return static_cast<std::uint8_t>(std::clamp(scaled, 0, 255));
}

[[nodiscard]] CharacterComponentBc1Vector
LoadCharacterComponentBc1BlockPixel(const CharacterComponentMipChain::MipLevelSurface &surface,
                                    std::uint32_t x, std::uint32_t y) {
  return PixelToBc1Vector(
      surface.pixels[static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)]);
}

void EncodeCharacterComponentBc1Block(const CharacterComponentMipChain::MipLevelSurface &surface,
                                      std::uint32_t origin_x, std::uint32_t origin_y,
                                      std::uint8_t *destination_block) {
  std::array<CharacterComponentBc1Vector, 16> samples{};
  CharacterComponentBc1Vector average;

  std::size_t sample_index = 0;
  for (std::uint32_t row = 0; row < 4; ++row) {
    for (std::uint32_t column = 0; column < 4; ++column) {
      const auto sample =
          LoadCharacterComponentBc1BlockPixel(surface, origin_x + column, origin_y + row);
      samples[sample_index++] = sample;
      average = Add(average, sample);
    }
  }

  average = Multiply(average, 1.0f / 16.0f);

  float covariance_rr = 0.0f;
  float covariance_rg = 0.0f;
  float covariance_rb = 0.0f;
  float covariance_gg = 0.0f;
  float covariance_gb = 0.0f;
  float covariance_bb = 0.0f;

  for (const auto &sample : samples) {
    const auto centered = Subtract(sample, average);
    covariance_rr += centered.red * centered.red;
    covariance_rg += centered.red * centered.green;
    covariance_rb += centered.red * centered.blue;
    covariance_gg += centered.green * centered.green;
    covariance_gb += centered.green * centered.blue;
    covariance_bb += centered.blue * centered.blue;
  }

  CharacterComponentBc1Vector axis{1.0f, 1.0f, 1.0f};
  for (int iteration = 0; iteration < 8; ++iteration) {
    const CharacterComponentBc1Vector next{
        covariance_rr * axis.red + covariance_rg * axis.green + covariance_rb * axis.blue,
        covariance_rg * axis.red + covariance_gg * axis.green + covariance_gb * axis.blue,
        covariance_rb * axis.red + covariance_gb * axis.green + covariance_bb * axis.blue,
    };

    const auto max_component =
        std::max({next.red, next.green, next.blue});
    if (max_component == 0.0f) {
      axis = {};
      break;
    }

    axis = Multiply(next, 1.0f / max_component);
  }

  axis = NormalizeAxis(axis);

  float min_projection = 1.0f;
  float max_projection = -1.0f;
  for (const auto &sample : samples) {
    const auto projection = Dot(Subtract(sample, average), axis);
    min_projection = std::min(min_projection, projection);
    max_projection = std::max(max_projection, projection);
  }

  const auto max_red = QuantizeChannel((average.red + axis.red * max_projection));
  const auto max_green = QuantizeChannel((average.green + axis.green * max_projection));
  const auto max_blue = QuantizeChannel((average.blue + axis.blue * max_projection));
  const auto min_red = QuantizeChannel((average.red + axis.red * min_projection));
  const auto min_green = QuantizeChannel((average.green + axis.green * min_projection));
  const auto min_blue = QuantizeChannel((average.blue + axis.blue * min_projection));

  std::uint16_t color0 = PackRgb565(max_red, max_green, max_blue);
  std::uint16_t color1 = PackRgb565(min_red, min_green, min_blue);
  std::array<std::uint32_t, 4> selector_lookup{1u, 3u, 2u, 0u};

  if (color0 < color1) {
    std::swap(color0, color1);
    selector_lookup = {0u, 2u, 3u, 1u};
  }

  std::uint32_t selectors = 0;
  if (color0 != color1) {
    const auto projection_scale = 3.0f / (max_projection - min_projection);
    for (std::size_t index = 0; index < samples.size(); ++index) {
      const auto projection = Dot(Subtract(samples[index], average), axis);
      const auto quantized_bucket = static_cast<std::size_t>(std::clamp(
          static_cast<int>(((projection - min_projection) * projection_scale) + 0.5f), 0, 3));
      selectors |= selector_lookup[quantized_bucket] << (index * 2);
    }
  }

  destination_block[0] = static_cast<std::uint8_t>(color0 & 0xFFu);
  destination_block[1] = static_cast<std::uint8_t>(color0 >> 8);
  destination_block[2] = static_cast<std::uint8_t>(color1 & 0xFFu);
  destination_block[3] = static_cast<std::uint8_t>(color1 >> 8);
  destination_block[4] = static_cast<std::uint8_t>(selectors & 0xFFu);
  destination_block[5] = static_cast<std::uint8_t>((selectors >> 8) & 0xFFu);
  destination_block[6] = static_cast<std::uint8_t>((selectors >> 16) & 0xFFu);
  destination_block[7] = static_cast<std::uint8_t>((selectors >> 24) & 0xFFu);
}

void AppendCharacterComponentBc1MipLevel(
    const CharacterComponentMipChain::MipLevelSurface &source_surface, std::uint32_t logical_width,
    std::uint32_t logical_height, CharacterComponentMipChain &destination_chain) {
  CharacterComponentMipChain::Bc1MipLevel compressed_level;
  compressed_level.width = logical_width;
  compressed_level.height = logical_height;

  const auto block_width = std::max((logical_width + 3u) / 4u, 1u);
  const auto block_height = std::max((logical_height + 3u) / 4u, 1u);
  compressed_level.blocks.resize(static_cast<std::size_t>(block_width) * block_height * 8u);

  std::size_t block_index = 0;
  for (std::uint32_t block_y = 0; block_y < block_height; ++block_y) {
    for (std::uint32_t block_x = 0; block_x < block_width; ++block_x) {
      EncodeCharacterComponentBc1Block(source_surface, block_x * 4u, block_y * 4u,
                                       compressed_level.blocks.data() + (block_index * 8u));
      ++block_index;
    }
  }

  destination_chain.bc1_mip_levels.push_back(std::move(compressed_level));
}

void FinalizeCharacterComponentSpecialMipChainToBc1(const CharacterComponentMipChain &source_chain,
                                                    CharacterComponentMipChain &destination_chain) {
  destination_chain.bc1_mip_levels.clear();

  const auto source_end =
      std::find_if(source_chain.mip_levels.begin(), source_chain.mip_levels.end(),
                   [](const CharacterComponentMipChain::MipLevelSurface &level) {
                     return level.width < 4u || level.height < 4u;
                   });
  if (source_chain.mip_levels.begin() == source_end) {
    return;
  }

  const auto source_count =
      static_cast<std::size_t>(std::distance(source_chain.mip_levels.begin(), source_end));
  destination_chain.bc1_mip_levels.reserve(source_count + 1u);

  for (auto it = source_chain.mip_levels.begin(); it != source_end; ++it) {
    AppendCharacterComponentBc1MipLevel(*it, it->width, it->height, destination_chain);
  }

  const auto &tail_level = source_chain.mip_levels[source_count - 1u];
  if (tail_level.width == 4u && tail_level.height == 4u) {
    AppendCharacterComponentBc1MipLevel(tail_level, 2u, 2u, destination_chain);
  }
}

[[nodiscard]] std::uint32_t BlendOpaqueArgb(std::uint32_t destination,
                                            CharacterComponentRgbSample source,
                                            std::uint32_t alpha) {
  const auto blended_red =
      BlendOpaqueChannel(static_cast<std::uint8_t>((destination >> 16) & 0xFFu), source.red, alpha);
  const auto blended_green = BlendOpaqueChannel(
      static_cast<std::uint8_t>((destination >> 8) & 0xFFu), source.green, alpha);
  const auto blended_blue =
      BlendOpaqueChannel(static_cast<std::uint8_t>(destination & 0xFFu), source.blue, alpha);
  return 0xFF000000u | (static_cast<std::uint32_t>(blended_red) << 16) |
         (static_cast<std::uint32_t>(blended_green) << 8) |
         static_cast<std::uint32_t>(blended_blue);
}

[[nodiscard]] std::uint8_t ExpandNibbleToByte(std::uint8_t nibble) {
  return static_cast<std::uint8_t>((nibble << 4) | nibble);
}

template <typename SampleFn>
[[nodiscard]] std::uint8_t
UpsampleSourceScalar(std::uint32_t destination_x, std::uint32_t destination_y,
                     std::uint32_t source_width, std::uint32_t source_height, SampleFn sample) {
  const auto source_x0 = destination_x >> 1;
  const auto source_y0 = destination_y >> 1;
  const auto source_x1 = std::min(source_x0 + 1u, source_width - 1u);
  const auto source_y1 = std::min(source_y0 + 1u, source_height - 1u);

  if ((destination_y & 1u) == 0u) {
    if ((destination_x & 1u) == 0u) {
      return sample(source_x0, source_y0);
    }
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(sample(source_x0, source_y0)) +
                                      static_cast<std::uint32_t>(sample(source_x1, source_y0))) >>
                                     1);
  }

  if ((destination_x & 1u) == 0u) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(sample(source_x0, source_y0)) +
                                      static_cast<std::uint32_t>(sample(source_x0, source_y1))) >>
                                     1);
  }

  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(sample(source_x0, source_y0)) +
                                    static_cast<std::uint32_t>(sample(source_x1, source_y0)) +
                                    static_cast<std::uint32_t>(sample(source_x0, source_y1)) +
                                    static_cast<std::uint32_t>(sample(source_x1, source_y1))) >>
                                   2);
}

[[nodiscard]] CharacterComponentRgbSample
UpsamplePaletteSample(const CharacterComponentPalettedTexture &source_texture,
                      const std::uint8_t *indices, std::uint32_t source_width,
                      std::uint32_t source_height, std::uint32_t destination_x,
                      std::uint32_t destination_y) {
  const auto sample_channel = [&](std::uint32_t source_x, std::uint32_t source_y,
                                  auto channel_accessor) {
    const auto palette_index = indices[static_cast<std::size_t>(source_y) * source_width +
                                       static_cast<std::size_t>(source_x)];
    return channel_accessor(PaletteEntryToRgb(source_texture.palette_bgra[palette_index]));
  };

  CharacterComponentRgbSample sample;
  sample.red = UpsampleSourceScalar(destination_x, destination_y, source_width, source_height,
                                    [&](std::uint32_t source_x, std::uint32_t source_y) {
                                      return sample_channel(
                                          source_x, source_y,
                                          [](CharacterComponentRgbSample rgb) { return rgb.red; });
                                    });
  sample.green = UpsampleSourceScalar(
      destination_x, destination_y, source_width, source_height,
      [&](std::uint32_t source_x, std::uint32_t source_y) {
        return sample_channel(source_x, source_y,
                              [](CharacterComponentRgbSample rgb) { return rgb.green; });
      });
  sample.blue = UpsampleSourceScalar(
      destination_x, destination_y, source_width, source_height,
      [&](std::uint32_t source_x, std::uint32_t source_y) {
        return sample_channel(source_x, source_y,
                              [](CharacterComponentRgbSample rgb) { return rgb.blue; });
      });
  return sample;
}

void AssertOpaqueSourceOriginFitsMip(
    [[maybe_unused]] CharacterComponentMipOrigin source_origin,
    [[maybe_unused]] CharacterComponentMipFillRegion destination_region,
    [[maybe_unused]] std::uint32_t source_width, [[maybe_unused]] std::uint32_t source_height) {
  assert(source_origin.x >= 0);
  assert(source_origin.y >= 0);
  assert(static_cast<std::uint32_t>(source_origin.x) + destination_region.width <= source_width);
  assert(static_cast<std::uint32_t>(source_origin.y) + destination_region.height <= source_height);
}

void AssertMaskedMipPayloadSize([[maybe_unused]] const std::vector<std::uint8_t> &source_mip,
                                std::uint32_t source_width, std::uint32_t source_height,
                                std::uint32_t destination_width, std::uint32_t destination_height) {
  const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
  const auto alpha_row_stride = static_cast<std::size_t>(source_width >> 3);
  [[maybe_unused]] const auto required_size = source_pixel_count +
                             static_cast<std::size_t>(destination_height - 1) * alpha_row_stride +
                             static_cast<std::size_t>((destination_width - 1) >> 3) + 1u;
  assert(source_mip.size() >= required_size);
}

void AssertArgb4MipPayloadSize([[maybe_unused]] const std::vector<std::uint8_t> &source_mip,
                               std::uint32_t source_width, std::uint32_t source_height,
                               std::uint32_t destination_width, std::uint32_t destination_height) {
  const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
  const auto alpha_row_stride = static_cast<std::size_t>(source_width >> 1);
  [[maybe_unused]] const auto required_size = source_pixel_count +
                             static_cast<std::size_t>(destination_height - 1) * alpha_row_stride +
                             static_cast<std::size_t>((destination_width - 1) >> 1) + 1u;
  assert(source_mip.size() >= required_size);
}

void AssertDirectCompositeRange(
    [[maybe_unused]] const CharacterComponentMipChain &chain,
    [[maybe_unused]] const CharacterComponentPalettedTexture &source_texture,
    [[maybe_unused]] std::uint32_t source_start_level,
    [[maybe_unused]] std::uint32_t destination_start_level) {
  assert(source_start_level <= source_texture.mip_count);
  assert(destination_start_level <= chain.mip_levels.size());
  assert(chain.mip_levels.size() - destination_start_level >=
         static_cast<std::size_t>(source_texture.mip_count - source_start_level));
}

void CompositeOpaquePalettedCharacterComponentMipRegionAcrossMips(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, CharacterComponentMipOrigin source_origin,
    std::uint32_t source_start_level, std::uint32_t destination_start_level) {
  if (source_start_level >= source_texture.mip_count) {
    return;
  }

  AssertDirectCompositeRange(chain, source_texture, source_start_level, destination_start_level);

  auto current_destination = destination_region;
  auto current_source_origin = source_origin;

  for (std::uint32_t source_level = source_start_level, destination_level = destination_start_level;
       source_level < source_texture.mip_count; ++source_level, ++destination_level) {
    const auto source_width = MipDimension(source_texture.width, source_level);
    const auto source_height = MipDimension(source_texture.height, source_level);
    const auto &source_mip = RequireSourceMipPayload(source_texture, source_level);
    auto &surface = chain.mip_levels[destination_level];
    AssertRegionFitsSurface(surface, current_destination);
    AssertOpaqueSourceOriginFitsMip(current_source_origin, current_destination, source_width,
                                    source_height);
    assert(source_mip.size() >= static_cast<std::size_t>(source_width) * source_height);

    for (std::uint32_t row = 0; row < current_destination.height; ++row) {
      const auto source_row_offset =
          static_cast<std::size_t>(current_source_origin.y + static_cast<std::int32_t>(row)) *
              source_width +
          static_cast<std::size_t>(current_source_origin.x);
      const auto destination_row_offset =
          static_cast<std::size_t>(current_destination.y + static_cast<std::int32_t>(row)) *
              surface.width +
          static_cast<std::size_t>(current_destination.x);

      for (std::uint32_t column = 0; column < current_destination.width; ++column) {
        const auto palette_index = source_mip[source_row_offset + column];
        surface.pixels[destination_row_offset + column] =
            PaletteBgraToOpaqueArgb(source_texture.palette_bgra[palette_index]);
      }
    }

    current_destination = HalveMipFillRegion(current_destination);
    current_source_origin = HalveMipOrigin(current_source_origin);
  }
}

void CompositeMaskedPalettedCharacterComponentMipRegionAcrossMips(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, std::uint32_t source_start_level,
    std::uint32_t destination_start_level) {
  if (source_start_level >= source_texture.mip_count) {
    return;
  }

  AssertDirectCompositeRange(chain, source_texture, source_start_level, destination_start_level);

  auto current_destination = destination_region;

  for (std::uint32_t source_level = source_start_level, destination_level = destination_start_level;
       source_level < source_texture.mip_count; ++source_level, ++destination_level) {
    const auto source_width = MipDimension(source_texture.width, source_level);
    const auto source_height = MipDimension(source_texture.height, source_level);
    const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
    const auto &source_mip = RequireSourceMipPayload(source_texture, source_level);
    auto &surface = chain.mip_levels[destination_level];
    AssertRegionFitsSurface(surface, current_destination);
    assert(current_destination.width <= source_width);
    assert(current_destination.height <= source_height);
    assert(source_mip.size() >= source_pixel_count);
    AssertMaskedMipPayloadSize(source_mip, source_width, source_height, current_destination.width,
                               current_destination.height);

    const auto *indices = source_mip.data();
    const auto *alpha_bits = indices + source_pixel_count;
    const auto alpha_row_stride = static_cast<std::size_t>(source_width >> 3);

    for (std::uint32_t row = 0; row < current_destination.height; ++row) {
      const auto index_row_offset = static_cast<std::size_t>(row) * source_width;
      const auto alpha_row_offset = static_cast<std::size_t>(row) * alpha_row_stride;
      const auto destination_row_offset =
          static_cast<std::size_t>(current_destination.y + static_cast<std::int32_t>(row)) *
              surface.width +
          static_cast<std::size_t>(current_destination.x);

      for (std::uint32_t column = 0; column < current_destination.width; ++column) {
        const auto alpha_byte = alpha_bits[alpha_row_offset + (column >> 3)];
        if ((alpha_byte & (1u << (column & 7))) == 0) {
          continue;
        }

        const auto palette_index = indices[index_row_offset + column];
        surface.pixels[destination_row_offset + column] =
            PaletteBgraToOpaqueArgb(source_texture.palette_bgra[palette_index]);
      }
    }

    current_destination = HalveMipFillRegion(current_destination);
  }
}

void CompositeArgb4PalettedCharacterComponentMipRegionAcrossMips(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, std::uint32_t source_start_level,
    std::uint32_t destination_start_level) {
  if (source_start_level >= source_texture.mip_count) {
    return;
  }

  AssertDirectCompositeRange(chain, source_texture, source_start_level, destination_start_level);

  auto current_destination = destination_region;

  for (std::uint32_t source_level = source_start_level, destination_level = destination_start_level;
       source_level < source_texture.mip_count; ++source_level, ++destination_level) {
    const auto source_width = MipDimension(source_texture.width, source_level);
    const auto source_height = MipDimension(source_texture.height, source_level);
    const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
    const auto &source_mip = RequireSourceMipPayload(source_texture, source_level);
    auto &surface = chain.mip_levels[destination_level];
    AssertRegionFitsSurface(surface, current_destination);
    assert(current_destination.width <= source_width);
    assert(current_destination.height <= source_height);
    assert(source_mip.size() >= source_pixel_count);
    AssertArgb4MipPayloadSize(source_mip, source_width, source_height, current_destination.width,
                              current_destination.height);

    const auto *indices = source_mip.data();
    const auto *alpha_nibbles = indices + source_pixel_count;
    const auto alpha_row_stride = static_cast<std::size_t>(source_width >> 1);

    for (std::uint32_t row = 0; row < current_destination.height; ++row) {
      const auto index_row_offset = static_cast<std::size_t>(row) * source_width;
      const auto alpha_row_offset = static_cast<std::size_t>(row) * alpha_row_stride;
      const auto destination_row_offset =
          static_cast<std::size_t>(current_destination.y + static_cast<std::int32_t>(row)) *
              surface.width +
          static_cast<std::size_t>(current_destination.x);

      for (std::uint32_t column = 0; column < current_destination.width; ++column) {
        const auto alpha_byte = alpha_nibbles[alpha_row_offset + (column >> 1)];
        const auto alpha_nibble = static_cast<std::uint32_t>(
            (column & 1u) == 0 ? (alpha_byte & 0x0Fu) : ((alpha_byte >> 4) & 0x0Fu));
        const auto alpha =
            static_cast<std::uint32_t>(ExpandNibbleToByte(static_cast<std::uint8_t>(alpha_nibble)));
        const auto palette_index = indices[index_row_offset + column];
        const auto source_pixel = PaletteEntryToRgb(source_texture.palette_bgra[palette_index]);

        surface.pixels[destination_row_offset + column] =
            BlendOpaqueArgb(surface.pixels[destination_row_offset + column], source_pixel, alpha);
      }
    }

    current_destination = HalveMipFillRegion(current_destination);
  }
}

void CompositeArgb8PalettedCharacterComponentMipRegionAcrossMips(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, std::uint32_t source_start_level,
    std::uint32_t destination_start_level) {
  if (source_start_level >= source_texture.mip_count) {
    return;
  }

  AssertDirectCompositeRange(chain, source_texture, source_start_level, destination_start_level);

  auto current_destination = destination_region;

  for (std::uint32_t source_level = source_start_level, destination_level = destination_start_level;
       source_level < source_texture.mip_count; ++source_level, ++destination_level) {
    const auto source_width = MipDimension(source_texture.width, source_level);
    const auto source_height = MipDimension(source_texture.height, source_level);
    const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
    const auto &source_mip = RequireSourceMipPayload(source_texture, source_level);
    auto &surface = chain.mip_levels[destination_level];
    AssertRegionFitsSurface(surface, current_destination);
    assert(current_destination.width <= source_width);
    assert(current_destination.height <= source_height);
    assert(source_mip.size() >= source_pixel_count * 2u);

    const auto *indices = source_mip.data();
    const auto *alpha_bytes = indices + source_pixel_count;

    for (std::uint32_t row = 0; row < current_destination.height; ++row) {
      const auto source_row_offset = static_cast<std::size_t>(row) * source_width;
      const auto destination_row_offset =
          static_cast<std::size_t>(current_destination.y + static_cast<std::int32_t>(row)) *
              surface.width +
          static_cast<std::size_t>(current_destination.x);

      for (std::uint32_t column = 0; column < current_destination.width; ++column) {
        const auto alpha = static_cast<std::uint32_t>(alpha_bytes[source_row_offset + column]);
        const auto palette_index = indices[source_row_offset + column];
        const auto source_pixel = PaletteEntryToRgb(source_texture.palette_bgra[palette_index]);

        surface.pixels[destination_row_offset + column] =
            BlendOpaqueArgb(surface.pixels[destination_row_offset + column], source_pixel, alpha);
      }
    }

    current_destination = HalveMipFillRegion(current_destination);
  }
}

void AssertSmallSourceTopMipShape(
    [[maybe_unused]] const CharacterComponentMipChain &chain,
    [[maybe_unused]] const CharacterComponentPalettedTexture &source_texture,
    [[maybe_unused]] CharacterComponentMipFillRegion destination_region) {
  assert(!chain.mip_levels.empty());
  assert(source_texture.mip_count > 0);
  assert(destination_region.width == static_cast<std::uint32_t>(source_texture.width) * 2u);
  assert(destination_region.height == static_cast<std::uint32_t>(source_texture.height) * 2u);
}

void CompositeSmallOpaquePalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, CharacterComponentMipOrigin source_origin) {
  AssertSmallSourceTopMipShape(chain, source_texture, destination_region);

  const auto source_width = static_cast<std::uint32_t>(source_texture.width);
  const auto source_height = static_cast<std::uint32_t>(source_texture.height);
  const auto &source_mip = RequireSourceMipPayload(source_texture, 0);
  const auto *indices = source_mip.data();
  auto &surface = chain.mip_levels[0];
  AssertRegionFitsSurface(surface, destination_region);
  assert(source_mip.size() >= static_cast<std::size_t>(source_width) * source_height);

  for (std::uint32_t row = 0; row < destination_region.height; ++row) {
    const auto destination_row_offset =
        static_cast<std::size_t>(destination_region.y + static_cast<std::int32_t>(row)) *
            surface.width +
        static_cast<std::size_t>(destination_region.x);

    for (std::uint32_t column = 0; column < destination_region.width; ++column) {
      const auto source_pixel =
          UpsamplePaletteSample(source_texture, indices, source_width, source_height, column, row);
      surface.pixels[destination_row_offset + column] = MakeOpaqueArgb(source_pixel);
    }
  }

  if (chain.mip_levels.size() <= 1u) {
    return;
  }

  CompositeOpaquePalettedCharacterComponentMipRegionAcrossMips(
      chain, source_texture, HalveMipFillRegion(destination_region), HalveMipOrigin(source_origin),
      0, 1);
}

void CompositeSmallMaskedPalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region) {
  AssertSmallSourceTopMipShape(chain, source_texture, destination_region);

  const auto source_width = static_cast<std::uint32_t>(source_texture.width);
  const auto source_height = static_cast<std::uint32_t>(source_texture.height);
  const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
  const auto &source_mip = RequireSourceMipPayload(source_texture, 0);
  const auto *indices = source_mip.data();
  const auto *alpha_bits = indices + source_pixel_count;
  const auto alpha_row_stride = static_cast<std::size_t>(source_width >> 3);
  auto &surface = chain.mip_levels[0];
  AssertRegionFitsSurface(surface, destination_region);
  assert(source_mip.size() >= source_pixel_count);
  AssertMaskedMipPayloadSize(source_mip, source_width, source_height, source_width, source_height);

  for (std::uint32_t row = 0; row < destination_region.height; ++row) {
    const auto destination_row_offset =
        static_cast<std::size_t>(destination_region.y + static_cast<std::int32_t>(row)) *
            surface.width +
        static_cast<std::size_t>(destination_region.x);

    for (std::uint32_t column = 0; column < destination_region.width; ++column) {
      const auto source_pixel =
          UpsamplePaletteSample(source_texture, indices, source_width, source_height, column, row);
      const auto alpha = static_cast<std::uint32_t>(UpsampleSourceScalar(
          column, row, source_width, source_height,
          [&](std::uint32_t source_x, std::uint32_t source_y) {
            const auto alpha_byte =
                alpha_bits[static_cast<std::size_t>(source_y) * alpha_row_stride + (source_x >> 3)];
            return static_cast<std::uint8_t>((alpha_byte & (1u << (source_x & 7))) != 0u ? 0xFFu
                                                                                         : 0u);
          }));

      surface.pixels[destination_row_offset + column] =
          BlendOpaqueArgb(surface.pixels[destination_row_offset + column], source_pixel, alpha);
    }
  }

  if (chain.mip_levels.size() <= 1u) {
    return;
  }

  CompositeMaskedPalettedCharacterComponentMipRegionAcrossMips(
      chain, source_texture, HalveMipFillRegion(destination_region), 0, 1);
}

void CompositeSmallArgb4PalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region) {
  AssertSmallSourceTopMipShape(chain, source_texture, destination_region);

  const auto source_width = static_cast<std::uint32_t>(source_texture.width);
  const auto source_height = static_cast<std::uint32_t>(source_texture.height);
  const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
  const auto &source_mip = RequireSourceMipPayload(source_texture, 0);
  const auto *indices = source_mip.data();
  const auto *alpha_nibbles = indices + source_pixel_count;
  const auto alpha_row_stride = static_cast<std::size_t>(source_width >> 1);
  auto &surface = chain.mip_levels[0];
  AssertRegionFitsSurface(surface, destination_region);
  assert(source_mip.size() >= source_pixel_count);
  AssertArgb4MipPayloadSize(source_mip, source_width, source_height, source_width, source_height);

  for (std::uint32_t row = 0; row < destination_region.height; ++row) {
    const auto destination_row_offset =
        static_cast<std::size_t>(destination_region.y + static_cast<std::int32_t>(row)) *
            surface.width +
        static_cast<std::size_t>(destination_region.x);

    for (std::uint32_t column = 0; column < destination_region.width; ++column) {
      const auto source_pixel =
          UpsamplePaletteSample(source_texture, indices, source_width, source_height, column, row);
      const auto alpha = static_cast<std::uint32_t>(UpsampleSourceScalar(
          column, row, source_width, source_height,
          [&](std::uint32_t source_x, std::uint32_t source_y) {
            const auto alpha_byte =
                alpha_nibbles[static_cast<std::size_t>(source_y) * alpha_row_stride +
                              (source_x >> 1)];
            const auto alpha_nibble = static_cast<std::uint8_t>(
                (source_x & 1u) == 0 ? (alpha_byte & 0x0Fu) : ((alpha_byte >> 4) & 0x0Fu));
            return ExpandNibbleToByte(alpha_nibble);
          }));

      surface.pixels[destination_row_offset + column] =
          BlendOpaqueArgb(surface.pixels[destination_row_offset + column], source_pixel, alpha);
    }
  }

  if (chain.mip_levels.size() <= 1u) {
    return;
  }

  CompositeArgb4PalettedCharacterComponentMipRegionAcrossMips(
      chain, source_texture, HalveMipFillRegion(destination_region), 0, 1);
}

void CompositeSmallArgb8PalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region) {
  AssertSmallSourceTopMipShape(chain, source_texture, destination_region);

  const auto source_width = static_cast<std::uint32_t>(source_texture.width);
  const auto source_height = static_cast<std::uint32_t>(source_texture.height);
  const auto source_pixel_count = static_cast<std::size_t>(source_width) * source_height;
  const auto &source_mip = RequireSourceMipPayload(source_texture, 0);
  const auto *indices = source_mip.data();
  const auto *alpha_bytes = indices + source_pixel_count;
  auto &surface = chain.mip_levels[0];
  AssertRegionFitsSurface(surface, destination_region);
  assert(source_mip.size() >= source_pixel_count * 2u);

  for (std::uint32_t row = 0; row < destination_region.height; ++row) {
    const auto destination_row_offset =
        static_cast<std::size_t>(destination_region.y + static_cast<std::int32_t>(row)) *
            surface.width +
        static_cast<std::size_t>(destination_region.x);

    for (std::uint32_t column = 0; column < destination_region.width; ++column) {
      const auto source_pixel =
          UpsamplePaletteSample(source_texture, indices, source_width, source_height, column, row);
      const auto alpha = static_cast<std::uint32_t>(UpsampleSourceScalar(
          column, row, source_width, source_height,
          [&](std::uint32_t source_x, std::uint32_t source_y) {
            return alpha_bytes[static_cast<std::size_t>(source_y) * source_width + source_x];
          }));

      surface.pixels[destination_row_offset + column] =
          BlendOpaqueArgb(surface.pixels[destination_row_offset + column], source_pixel, alpha);
    }
  }

  if (chain.mip_levels.size() <= 1u) {
    return;
  }

  CompositeArgb8PalettedCharacterComponentMipRegionAcrossMips(
      chain, source_texture, HalveMipFillRegion(destination_region), 0, 1);
}

[[nodiscard]] std::int32_t
ParseCharacterComponentTextureLevel(const openwow::ui::game::CVarSystem &cvars) {
  return static_cast<std::int32_t>(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(cvars.GetCVar("componentTextureLevel")));
}

[[nodiscard]] bool ParseCharacterComponentToggle(const openwow::ui::game::CVarSystem &cvars,
                                                 const char *name) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(cvars.GetCVar(name)) != 0u;
}

void ResetCompositeFlushLog(CharacterModelCompositeFlushState &flush) {
  flush.processed_dirty_mask = 0;
  flush.processed_regions.clear();
  flush.blitted_regions.clear();
  flush.standard_passes.clear();
  flush.extra_passes.clear();
  flush.full_texture_blit = false;
  flush.special_atlas_flush = false;
}

void RecordCompositeRegionPasses(CharacterModelRefreshState &model, std::uint8_t region_index) {
  const auto &region = model.composite_regions[region_index];
  if (region.standard_texture && region.standard_texture->source_key != 0) {
    model.composite_flush.standard_passes.emplace_back(region_index,
                                                       region.standard_texture->source_key);
  }

  for (const auto &extra_texture : region.extra_textures) {
    if (!extra_texture.texture || extra_texture.texture->source_key == 0) {
      continue;
    }

    model.composite_flush.extra_passes.emplace_back(extra_texture.component_index,
                                                    extra_texture.texture->source_key);
  }
}

void RecordDirtyRegionBlits(CharacterModelRefreshState &model) {
  for (std::uint8_t region = 0; region < kCharacterComponentStandardTextureCount; ++region) {
    if ((model.dirty_region_mask & (1u << region)) == 0) {
      continue;
    }
    model.composite_flush.blitted_regions.push_back(region);
  }
}

[[nodiscard]] std::shared_ptr<CharacterComponentRequest>
AcquireCharacterComponentRequestForQueue(CharacterComponentWorkerAllocations *allocations) {
  std::shared_ptr<CharacterComponentRequest> request;
  if (allocations != nullptr && !allocations->reusable_requests.empty()) {
    request = allocations->reusable_requests.front();
    allocations->reusable_requests.pop_front();
  } else {
    request = std::make_shared<CharacterComponentRequest>();
    if (allocations != nullptr) {
      RegisterCharacterComponentRequestAllocation(*allocations, request);
    }
  }

  ResetCharacterComponentRequestForQueue(*request);
  return request;
}

void PushCharacterComponentRequestToHead(
    std::deque<std::shared_ptr<CharacterComponentRequest>> &queue,
    std::shared_ptr<CharacterComponentRequest> request) {
  queue.push_front(std::move(request));
}

void ResetCharacterComponentMipChainStorage(CharacterComponentMipChain &chain) {
  chain.chain_id = 0;
  chain.mip_levels.clear();
  chain.bc1_mip_levels.clear();
  chain.standard_passes.clear();
  chain.extra_passes.clear();
  chain.special_finalize_called = false;
  chain.special_finalize_source_token = 0;
}

void ResetCharacterComponentMipChainForReuse(CharacterComponentMipChain &chain) {
  chain.bc1_mip_levels.clear();
  chain.standard_passes.clear();
  chain.extra_passes.clear();
  chain.special_finalize_called = false;
  chain.special_finalize_source_token = 0;
}

void ReleaseCharacterComponentRequestTextures(CharacterComponentRequest &request) {
  for (auto &standard_texture : request.standard_texture_handles) {
    if (standard_texture) {
      standard_texture->Release();
      standard_texture.reset();
    }
  }

  for (auto &extra_texture : request.extra_texture_handles) {
    if (extra_texture) {
      extra_texture->Release();
      extra_texture.reset();
    }
  }

  request.standard_texture_ids.fill(0);
  request.extra_texture_indices.clear();
  request.extra_texture_ids.clear();
  request.extra_texture_handles.clear();
}

[[nodiscard]] bool HasTrackedMipChain(const std::vector<CharacterComponentMipChain *> &seen_chains,
                                      const CharacterComponentMipChain *chain) {
  return std::find(seen_chains.begin(), seen_chains.end(), chain) != seen_chains.end();
}

void ResetTrackedMipChain(CharacterComponentMipChain &chain,
                          std::vector<CharacterComponentMipChain *> &seen_chains) {
  if (HasTrackedMipChain(seen_chains, &chain)) {
    return;
  }

  seen_chains.push_back(&chain);
  ResetCharacterComponentMipChainStorage(chain);
}

void ResetTrackedMipChainList(std::deque<std::shared_ptr<CharacterComponentMipChain>> &chains,
                              std::size_t &freed_count,
                              std::vector<CharacterComponentMipChain *> &seen_chains) {
  for (const auto &chain : chains) {
    if (!chain) {
      continue;
    }

    ResetTrackedMipChain(*chain, seen_chains);
    ++freed_count;
  }

  chains.clear();
}

void ResetCharacterComponentRequestAllocation(
    CharacterComponentRequest &request, std::vector<CharacterComponentMipChain *> &seen_chains) {
  if (request.output_chain) {
    ResetTrackedMipChain(*request.output_chain, seen_chains);
  }

  ReleaseCharacterComponentRequestTextures(request);
  request.flags = 0;
  request.request_type = 0;
  request.output_chain.reset();
  request.source_token = 0;
}

}

const CharacterComponentBaseSectionEntry *
CharacterComponentBaseSectionTable::FindEntry(int race, int gender, int section, int subsection,
                                              int choice) const {
  const auto it = entries.find(std::make_tuple(race, gender, section, subsection, choice));
  if (it == entries.end()) {
    return nullptr;
  }

  return &it->second;
}

const std::string *CharacterComponentBaseSectionTable::FindTexturePath(int race, int gender,
                                                                       int choice) const {
  if (const auto *entry =
          FindEntry(race, gender, static_cast<int>(kCharacterComponentSectionLookupBaseSection),
                    static_cast<int>(kCharacterComponentSectionLookupBaseType), choice);
      entry != nullptr && !entry->texture_names[1].empty()) {
    return &entry->texture_names[1];
  }

  const auto it = texture_paths.find(std::make_tuple(race, gender, choice));
  if (it == texture_paths.end()) {
    return nullptr;
  }

  return &it->second;
}

namespace {

[[nodiscard]] bool
LoadCharacterModelTextureUnitFromPath(const std::string &texture_path, int texture_unit,
                                      CharacterComponentTextureLoadState &load_state,
                                      CharacterModelTextureReplacementState &replacement_state,
                                      std::string &scratch_path) {
  if (texture_path.empty()) {
    return false;
  }

  scratch_path = texture_path;
  load_state.requested_path = scratch_path;
  if (!load_state.load_texture || !load_state.release_texture) {
    load_state.last_texture_handle = 0;
    return false;
  }

  ++load_state.load_calls;
  const std::uintptr_t texture_handle =
      load_state.load_texture(scratch_path.c_str(), load_state.status);
  load_state.last_texture_handle = texture_handle;
  if (texture_handle == 0) {
    return false;
  }

  ++replacement_state.replace_calls;
  replacement_state.replaced_unit = texture_unit;
  replacement_state.replaced_texture_handle = texture_handle;
  load_state.release_texture(texture_handle);
  ++load_state.release_calls;
  return true;
}

}

bool LoadTextureUnit8FromBaseComponentSection(const CharacterComponentBaseSectionTable &table,
                                              int race, int gender, int choice,
                                              CharacterComponentTextureLoadState &load_state,
                                              CharacterComponentTextureUnit8State &unit8_state,
                                              std::string &scratch_path) {
  const std::string *texture_path = table.FindTexturePath(race, gender, choice);
  if (texture_path == nullptr) {
    return false;
  }
  return LoadCharacterModelTextureUnitFromPath(*texture_path, 8, load_state, unit8_state,
                                               scratch_path);
}

CharacterComponentBackendConfig
ResolveCharacterComponentStartupSelection(CharacterComponentStartupSelection selection) {
  std::int32_t requested_texture_level = selection.requested_texture_level;
  if (selection.processor_count < 2) {
    selection.component_thread = false;
  }

  if (!selection.component_thread) {
    selection.component_compress = false;
    requested_texture_level = ClampTextureLevel<std::int32_t>(requested_texture_level, 7, 8);
  } else {
    requested_texture_level = ClampTextureLevel<std::int32_t>(requested_texture_level, 7, 9);
  }

  return InitializeCharacterComponentBackend(
      selection.texture_format, static_cast<std::uint32_t>(requested_texture_level),
      selection.component_thread, selection.component_compress);
}

CharacterComponentBackendConfig
InitializeCharacterComponentBackend(int texture_format, std::uint32_t requested_texture_level,
                                    bool worker_thread_enabled, bool compression_enabled) {
  CharacterComponentBackendConfig config;
  config.texture_format = texture_format;
  config.worker_thread_enabled = worker_thread_enabled;
  config.compression_enabled = compression_enabled;

  config.selected_texture_level = ClampTextureLevel<std::uint32_t>(requested_texture_level, 6, 9);
  if (!config.compression_enabled && config.selected_texture_level > 8) {
    config.selected_texture_level = 8;
  }

  config.composite_texture_edge = 1u << config.selected_texture_level;
  return config;
}

std::uint32_t ResolveCharacterComponentLookupRowCount(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &char_sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        &facial_hair_styles) {
  std::uint32_t max_race_id = 0;

  for (const auto &entry : char_sections.entries()) {
    max_race_id = std::max(max_race_id, entry.race_id);
  }

  for (const auto &entry : facial_hair_styles.entries()) {
    max_race_id = std::max(max_race_id, entry.race_id);
  }

  return (max_race_id == 0u) ? 0u : 2u * max_race_id + 2u;
}

CharacterComponentBaseSectionTable BuildCharacterComponentBaseSectionTable(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &char_sections,
    const std::uint32_t lookup_row_count,
    std::vector<CharacterComponentBaseSectionLookupRowState> *lookup_row_storage) {
  CharacterComponentBaseSectionTable table;

  if (lookup_row_storage != nullptr) {
    lookup_row_storage->assign(static_cast<std::size_t>(lookup_row_count), {});
  }

  std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, std::uint32_t>
      max_subsection_by_row_section;

  for (const auto &entry : char_sections.entries()) {
    if (entry.base_section >= kCharacterComponentBaseSectionCount) {
      continue;
    }

    const auto row_index = CharacterComponentLookupRowIndex(entry.race_id, entry.sex_id);
    if (row_index >= lookup_row_count) {
      continue;
    }

    CharacterComponentBaseSectionEntry section_entry;
    section_entry.flags = entry.flags;
    for (std::size_t index = 0; index < section_entry.texture_names.size(); ++index) {
      section_entry.texture_names[index] = std::string(entry.texture_name[index]);
    }

    table
        .entries[std::make_tuple(static_cast<int>(entry.race_id), static_cast<int>(entry.sex_id),
                                 static_cast<int>(entry.base_section), static_cast<int>(entry.type),
                                 static_cast<int>(entry.variation))] = section_entry;

    if (entry.base_section == kCharacterComponentSectionLookupBaseSection &&
        entry.type == kCharacterComponentSectionLookupBaseType) {
      table.texture_paths[std::make_tuple(
          static_cast<int>(entry.race_id), static_cast<int>(entry.sex_id),
          static_cast<int>(entry.variation))] = section_entry.texture_names[1];
    }

    const auto row_section_key = std::make_tuple(entry.race_id, entry.sex_id, entry.base_section);
    auto &max_subsection = max_subsection_by_row_section[row_section_key];
    max_subsection = std::max(max_subsection, entry.type);
  }

  if (lookup_row_storage == nullptr) {
    return table;
  }

  for (const auto &[row_section_key, max_subsection] : max_subsection_by_row_section) {
    const auto [race, gender, section] = row_section_key;
    const auto row_index = CharacterComponentLookupRowIndex(race, gender);
    if (row_index >= lookup_row_count || section >= kCharacterComponentBaseSectionCount) {
      continue;
    }

    (*lookup_row_storage)[row_index].sections[section].entry_arrays.resize(
        static_cast<std::size_t>(max_subsection) + 1u);
  }

  return table;
}

std::vector<std::uint32_t> BuildCharacterComponentFacialHairStyleCounts(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        &facial_hair_styles,
    const std::uint32_t lookup_row_count) {
  std::vector<std::uint32_t> counts(static_cast<std::size_t>(lookup_row_count), 0u);

  for (const auto &entry : facial_hair_styles.entries()) {
    const auto row_index = CharacterComponentLookupRowIndex(entry.race_id, entry.sex_id);
    if (row_index >= lookup_row_count) {
      continue;
    }

    ++counts[row_index];
  }

  return counts;
}

CharacterComponentWorkerMipPoolState
PrimeCharacterComponentWorkerMipPools(const CharacterComponentBackendConfig &config) {
  CharacterComponentWorkerMipPoolState state;

  const bool use_split_compressed_pools =
      config.worker_thread_enabled && config.compression_enabled;
  const std::size_t general_pool_count = use_split_compressed_pools
                                             ? kCharacterComponentCompressedGeneralMipChainCount
                                             : kCharacterComponentInitialGeneralMipChainCount;
  const std::size_t special_pool_count =
      use_split_compressed_pools ? kCharacterComponentCompressedSpecialMipChainCount : 0u;

  CharacterComponentWorkerMipChainSeed general_seed;
  general_seed.allocation_format = kCharacterComponentGeneralMipChainFormat;
  general_seed.texture_edge = config.composite_texture_edge;
  state.general_free_mip_chains.assign(general_pool_count, general_seed);

  CharacterComponentWorkerMipChainSeed special_seed;
  special_seed.allocation_format = kCharacterComponentSpecialMipChainFormat;
  special_seed.texture_edge = config.composite_texture_edge;
  state.special_free_mip_chains.assign(special_pool_count, special_seed);

  return state;
}

CharacterComponentWorkerBackendState
InitializeCharacterComponentWorkerBackend(const CharacterComponentBackendConfig &config,
                                          std::uintptr_t evt_context_token) {
  CharacterComponentWorkerBackendState state;
  if (!config.worker_thread_enabled) {
    return state;
  }

  state.active = true;
  state.texture_assemblers_installed.fill(true);
  state.mip_pools = PrimeCharacterComponentWorkerMipPools(config);
  state.wake_event_reset_calls = 1;
  state.evt_context_captured = true;
  state.evt_context_token = evt_context_token;
  state.worker_thread_started = true;
  state.worker_thread_name = kCharacterComponentWorkerThreadName;
  state.worker_thread_create_calls = 1;
  return state;
}

CharacterComponentBackendRuntimeState RegisterCharacterComponentCVarsAndInitializeBackend(
    openwow::ui::game::CVarSystem &cvars, std::uint32_t processor_count,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> *char_sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        *facial_hair_styles) {
  using Flags = openwow::ui::game::CVarFlags;

  cvars.RegisterCVar("componentTextureLevel", "8", Flags::Archive,
                     "Number of mip levels used for character component textures");
  cvars.RegisterCVar("componentThread", "1", Flags::Archive,
                     "Multi thread character component processing");
  cvars.RegisterCVar("componentCompress", "1", Flags::Archive,
                     "Character component texture compression");

  CharacterComponentStartupSelection selection;
  selection.texture_format = kCharacterComponentTextureFormat;
  selection.requested_texture_level = ParseCharacterComponentTextureLevel(cvars);
  selection.component_thread = ParseCharacterComponentToggle(cvars, "componentThread");
  selection.component_compress = ParseCharacterComponentToggle(cvars, "componentCompress");
  selection.processor_count = processor_count;

  g_character_component_backend_runtime_state.initialized = true;
  g_character_component_backend_runtime_state.config =
      ResolveCharacterComponentStartupSelection(selection);
  g_character_component_backend_runtime_state.worker_backend =
      InitializeCharacterComponentWorkerBackend(g_character_component_backend_runtime_state.config,
                                                0);
  g_character_component_backend_runtime_state.shutdown_state =
      std::make_shared<CharacterComponentBackendShutdownState>();
  auto &shutdown_state = *g_character_component_backend_runtime_state.shutdown_state;
  shutdown_state.refresh_queue_handler.registered = true;

  std::uint32_t max_race_id = 0u;
  bool have_lookup_source = false;
  if (char_sections != nullptr) {
    for (const auto &entry : char_sections->entries()) {
      max_race_id = std::max(max_race_id, entry.race_id);
      have_lookup_source = true;
    }
  }
  if (facial_hair_styles != nullptr) {
    for (const auto &entry : facial_hair_styles->entries()) {
      max_race_id = std::max(max_race_id, entry.race_id);
      have_lookup_source = true;
    }
  }
  const std::uint32_t lookup_row_count = have_lookup_source ? 2u * max_race_id + 2u : 0u;

  if (char_sections != nullptr) {
    shutdown_state.base_section_table = BuildCharacterComponentBaseSectionTable(
        *char_sections, lookup_row_count, &shutdown_state.base_section_lookup_rows);
  }

  if (facial_hair_styles != nullptr && lookup_row_count > 0u) {
    shutdown_state.facial_hair_style_counts =
        BuildCharacterComponentFacialHairStyleCounts(*facial_hair_styles, lookup_row_count);
    shutdown_state.facial_hair_style_counts_allocated = true;
  }

  return g_character_component_backend_runtime_state;
}

CharacterComponentBackendRuntimeState GetCharacterComponentBackendRuntimeState() {
  return g_character_component_backend_runtime_state;
}

void ResetCharacterComponentBackendRuntimeStateForTests() {
  g_character_component_backend_runtime_state = {};
}

void CharacterComponentTextureHandle::Retain() {
  ++reference_count;
}

void CharacterComponentTextureHandle::Unload() {
  async_load_queued = false;
  pending_stream_object = false;
  async_buffer_size = 0;
  load_complete = false;
  texture_path.clear();
  resolved_texture_path.clear();
  live_row_resolvable = false;
}

void CharacterComponentTextureHandle::Release() {
  ++release_count;
  if (reference_count <= 0) {
    return;
  }

  --reference_count;
  if (reference_count > 0) {
    return;
  }

  ++cleanup_count;
  Unload();
  ++unload_count;
  ++type_handle_release_count;
}

namespace {

struct CharacterComponentTextureMetadata {
  std::uint32_t word0 = 0;
  std::uint32_t word1 = 0x00010000;
};

[[nodiscard]] char FoldAsciiUpper(const char value) {
  return (value >= 'a' && value <= 'z') ? static_cast<char>(value - ('a' - 'A')) : value;
}

[[nodiscard]] bool MatchesAsciiExtension(const std::string_view path, const std::string_view ext) {
  if (path.size() < ext.size()) {
    return false;
  }

  const auto offset = path.size() - ext.size();
  for (std::size_t index = 0; index < ext.size(); ++index) {
    if (FoldAsciiUpper(path[offset + index]) != FoldAsciiUpper(ext[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool ResolveCharacterComponentTextureAlternatePath(const std::string_view path,
                                                                 std::string &alternate_path) {
  if (MatchesAsciiExtension(path, ".TGA")) {
    alternate_path.assign(path.begin(), path.end() - 4);
    alternate_path += ".BLP";
    return true;
  }

  if (MatchesAsciiExtension(path, ".BLP")) {
    alternate_path.assign(path.begin(), path.end() - 4);
    alternate_path += ".TGA";
    return true;
  }

  return false;
}

void RequestCharacterComponentTextureAsyncLoad(CharacterComponentTextureHandle &texture) {
  ++texture.load_async_call_count;
  texture.resolved_texture_path = texture.texture_path;
  texture.async_buffer_size = 0;
  texture.async_open_failed = false;

  ++texture.primary_open_attempt_count;
  bool opened = texture.primary_path_open_succeeds;
  if (!opened) {
    std::string alternate_path;
    if (ResolveCharacterComponentTextureAlternatePath(texture.texture_path, alternate_path)) {
      ++texture.alternate_open_attempt_count;
      texture.resolved_texture_path = std::move(alternate_path);
      opened = texture.alternate_extension_open_succeeds;
    }
  }

  if (!opened) {
    texture.async_open_failed = true;
    texture.async_load_queued = false;
    texture.pending_stream_object = false;
    texture.load_complete = true;
    return;
  }

  texture.async_buffer_size = texture.async_file_size & 0xFFFFFu;
  if (texture.complete_when_async_load_requested) {
    texture.async_load_queued = false;
    texture.pending_stream_object = false;
    texture.load_complete = true;
  } else {
    texture.async_load_queued = true;
    texture.pending_stream_object = true;
    texture.load_complete = false;
  }
}

[[nodiscard]] bool
HasPendingCharacterComponentTextureLoad(const CharacterComponentTextureHandle *texture) {
  return texture != nullptr && !texture->async_open_failed &&
         (texture->load_complete || texture->async_load_queued);
}

[[nodiscard]] bool
EnsureCharacterComponentTextureMetadataReady(CharacterComponentTextureHandle &texture,
                                             CharacterComponentTextureMetadata &metadata,
                                             CharacterComponentTextureRefreshOptions options) {
  if (!texture.load_complete) {
    if (!texture.async_load_queued) {
      RequestCharacterComponentTextureAsyncLoad(texture);
    }

    if (!texture.load_complete && (texture.metadata_word0 & 0xFFFFu) == 0u &&
        texture.pending_stream_object) {
      if (!options.allow_synchronous_wait) {
        if (options.streaming_mode_enabled && texture.can_touch_pending_stream_object) {
          ++texture.touch_pending_stream_object_call_count;
        }
        return false;
      }

      ++texture.sync_wait_call_count;
      texture.metadata_word0 = texture.metadata_word0_after_sync_wait;
      texture.metadata_word1 = texture.metadata_word1_after_sync_wait;
      texture.async_load_queued = false;
      texture.pending_stream_object = false;
      texture.load_complete = true;
    }
  }

  metadata.word0 = texture.metadata_word0;
  metadata.word1 = texture.metadata_word1;
  return true;
}

[[nodiscard]] std::shared_ptr<CharacterComponentTextureHandle>
MaterializeLiveCharacterComponentTexture(
    const std::shared_ptr<CharacterComponentTextureHandle> &queued_texture) {
  if (!queued_texture || !queued_texture->live_row_resolvable) {
    return nullptr;
  }

  auto live_texture = std::make_shared<CharacterComponentTextureHandle>(*queued_texture);
  live_texture->reference_count = 1;
  live_texture->release_count = 0;
  live_texture->cleanup_count = 0;
  live_texture->unload_count = 0;
  live_texture->type_handle_release_count = 0;
  return live_texture;
}

}

namespace {
void ClearPendingCharacterComponentRequest(CharacterModelRefreshState &model);
void ReleaseCharacterBaseSkinRenderTarget(CharacterBaseSkinRenderTargetState &base_skin);
void UnlinkCharacterModelRefreshIfLinked(CharacterModelRefreshState &model);
}

void ShutdownCharacterModelRefreshState(CharacterModelRefreshState &model) {
  const auto release_owned_texture =
      [](std::shared_ptr<CharacterComponentTextureHandle> &texture_handle) {
        if (!texture_handle) {
          return;
        }

        texture_handle->Release();
        texture_handle.reset();
      };

  ClearPendingCharacterComponentRequest(model);
  ReleaseCharacterBaseSkinRenderTarget(model.base_skin_render_target);
  UnlinkCharacterModelRefreshIfLinked(model);

  for (auto &page : model.texture_pages) {
    for (std::size_t slot_index = 0; slot_index < kCharacterComponentCleanupSlotCount;
         ++slot_index) {
      release_owned_texture(page.live_slots[slot_index]);
      page.queued_slots[slot_index].reset();
    }
  }

  for (auto &row : model.component_table) {
    for (auto &texture_handle : row) {
      release_owned_texture(texture_handle);
    }
  }
}

bool QueueCharacterComponentTexture(
    CharacterComponentTexturePageState &page_state, std::size_t page_index, std::size_t slot_index,
    const std::shared_ptr<CharacterComponentTextureHandle> &pending_texture,
    bool explicit_base_texture_mode) {
  if (explicit_base_texture_mode) {
    return false;
  }
  if (!pending_texture || slot_index >= kCharacterComponentPageSlotCount) {
    return false;
  }

  const auto &existing_queued = page_state.queued_slots[slot_index];
  if (existing_queued && existing_queued->source_key == pending_texture->source_key &&
      PageTokensMatch(*existing_queued, *pending_texture, page_index)) {
    return false;
  }

  page_state.loading_slot_mask &= ~(1u << slot_index);

  auto &live_slot = page_state.live_slots[slot_index];
  if (live_slot) {
    live_slot->Release();
    live_slot.reset();
  }

  page_state.queued_slots[slot_index] = pending_texture;
  return true;
}

void ClearQueuedCharacterComponentTexture(CharacterComponentTexturePageState &page_state,
                                          int slot_index) {
  if (slot_index < 0 || slot_index >= static_cast<int>(kCharacterComponentPageSlotCount)) {
    return;
  }

  auto &live_slot = page_state.live_slots[slot_index];
  if (live_slot) {
    live_slot->Release();
    live_slot.reset();
  }

  page_state.queued_slots[slot_index].reset();
  page_state.loading_slot_mask &= ~(1u << static_cast<unsigned>(slot_index));
}

std::string BuildComponentTexturePath(const std::size_t page_index,
                                      const std::string_view texture_name,
                                      const char suffix) {
  if (page_index >= kArmorRegionTextureSectionNames.size() || texture_name.empty()) {
    return {};
  }

  std::string path;
  path.reserve(64);
  path += "Item\\TextureComponents\\";
  path += kArmorRegionTextureSectionNames[page_index];
  path += '\\';
  path += texture_name;
  path += '_';
  path += suffix;
  path += ".blp";
  return path;
}

std::string ResolveComponentTexturePath(const std::size_t page_index,
                                        const std::string_view texture_name,
                                        const std::uint8_t gender,
                                        const ComponentTexturePathResolverFn &can_resolve) {

  std::string path = BuildComponentTexturePath(page_index, texture_name, kUnisexSuffixChar);
  if (path.empty()) {
    return path;
  }

  if (can_resolve && can_resolve(path)) {
    return path;
  }

  if (path.size() >= 5) {
    const char gender_char =
        (gender < kGenderSuffixChar.size()) ? kGenderSuffixChar[gender] : kUnisexSuffixChar;
    path[path.size() - 5] = gender_char;
  }
  return path;
}

std::shared_ptr<CharacterComponentTextureHandle>
CreateLiveComponentTextureRow(const std::size_t page_index,
                              const std::string_view texture_name,
                              const std::uint8_t gender,
                              const ComponentTexturePathResolverFn &can_resolve) {
  std::string resolved_path =
      ResolveComponentTexturePath(page_index, texture_name, gender, can_resolve);
  if (resolved_path.empty()) {
    return nullptr;
  }

  auto handle = std::make_shared<CharacterComponentTextureHandle>();
  handle->texture_path = std::move(resolved_path);
  handle->resolved_texture_path = handle->texture_path;
  return handle;
}

bool QueueSectionComponentTexture(CharacterModelRefreshState &model,
                                  const CharacterComponentBaseSectionTable &table, int race,
                                  int gender, unsigned int sectionType, int textureSlot,
                                  int variation, int color, unsigned int dirtyBit,
                                  const SectionComponentTextureLoaderFn &load_texture) {
  if (sectionType >= kCharacterModelComponentTableRowCount ||
      textureSlot < 0 ||
      static_cast<std::size_t>(textureSlot) >= kCharacterModelComponentTableColumnCount) {
    return false;
  }

  auto &slot = model.component_table[sectionType][textureSlot];
  if (slot) {
    slot->Release();
    slot.reset();
  }

  const auto *entry = table.FindEntry(race, gender, static_cast<int>(sectionType), variation, color);
  if (!entry) {
    return false;
  }

  const std::string &texture_path = entry->texture_names[textureSlot];
  if (!texture_path.empty() && load_texture) {
    auto texture_handle = load_texture(texture_path);
    slot = std::move(texture_handle);
  }

  model.dirty_region_mask |= (1u << dirtyBit);
  if (model.pending_request) {
    model.pending_request->flags &= ~CharacterComponentRequest::kNeedsComposite;
    model.pending_request.reset();
  }
  model.flags &= ~kCharacterModelFlagRefreshReady;

  return true;
}

void EnsureCharacterBaseSkinRenderTarget(CharacterBaseSkinRenderTargetState &base_skin,
                                         int request_type) {
  EnsureCharacterBaseSkinUploadSnapshot(base_skin, request_type);
  if (base_skin.available) {
    return;
  }

  base_skin.available = true;
  ++base_skin.create_calls;
  base_skin.last_requested_data_format = request_type;
  base_skin.last_created_format = (request_type == 6) ? 6 : 2;
  ++base_skin.replace_calls;
  base_skin.replaced_texture_type = 1;
}

namespace {

void ReleaseCharacterBaseSkinRenderTarget(CharacterBaseSkinRenderTargetState &base_skin) {
  if (!base_skin.available) {
    return;
  }

  base_skin.available = false;
  base_skin.general_upload_snapshot.reset();
  base_skin.special_upload_snapshot.reset();
  base_skin.last_upload_source = CharacterBaseSkinUploadSourceKind::None;
  base_skin.last_upload_pitch = 0;
  base_skin.last_upload_data = 0;
  base_skin.last_upload_action = -1;
  base_skin.last_upload_mip_level = -1;
  ++base_skin.release_calls;
}

void ClearPendingCharacterComponentRequest(CharacterModelRefreshState &model) {
  if (!model.pending_request) {
    return;
  }

  model.pending_request->flags &= ~CharacterComponentRequest::kNeedsComposite;
  model.pending_request.reset();
}

void ApplyCharacterModelGeosetVisibilityIfDirty(CharacterModelRefreshState &model) {
  if ((model.flags & kCharacterModelFlagGeosetsDirty) == 0) {
    return;
  }

  ++model.geoset_visibility.apply_calls;
}

void UnlinkCharacterModelRefreshIfLinked(CharacterModelRefreshState &model) {
  if (!model.pending_link.linked) {
    return;
  }

  model.pending_link.linked = false;
  ++model.pending_link.unlink_calls;
}

[[nodiscard]] const CharacterComponentItemDisplayRecordState *
StoredItemDisplayRecord(const CharacterModelRefreshState &model, std::size_t item_slot) {
  if (item_slot >= model.item_display_records.size()) {
    return nullptr;
  }

  return model.item_display_records[item_slot].get();
}

[[nodiscard]] int ResolveCharacterComponentPage0Slot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage0SlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage0SlotByItemSlot[item_slot];
}

[[nodiscard]] int
ResolveCharacterComponentPage1Slot(std::size_t item_slot,
                                   const CharacterComponentItemDisplayRecordState *record) {
  if (item_slot >= kCharacterComponentPage1SlotByItemSlot.size()) {
    return -1;
  }

  auto resolved_slot = kCharacterComponentPage1SlotByItemSlot[item_slot];
  if (record == nullptr || record->geoset_control_1 == 0) {
    return resolved_slot;
  }

  if (item_slot == kCharacterModelChestItemSlot) {
    return kCharacterComponentPage1ChestLayerSlot;
  }

  if (item_slot == kCharacterModelGlovesItemSlot) {
    return kCharacterComponentPage1GlovesLayerSlot;
  }

  return resolved_slot;
}

[[nodiscard]] int ResolveCharacterComponentPage2Slot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage2SlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage2SlotByItemSlot[item_slot];
}

[[nodiscard]] int ResolveCharacterComponentPage3Slot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage3SlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage3SlotByItemSlot[item_slot];
}

[[nodiscard]] int ResolveCharacterComponentPage4Slot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage4SlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage4SlotByItemSlot[item_slot];
}

[[nodiscard]] int ResolveCharacterComponentPage5Slot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage5SlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage5SlotByItemSlot[item_slot];
}

[[nodiscard]] int ResolveCharacterComponentPage6BaseSlot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage6BaseSlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage6BaseSlotByItemSlot[item_slot];
}

[[nodiscard]] int ResolveCharacterComponentPage7Slot(std::size_t item_slot) {
  if (item_slot >= kCharacterComponentPage7SlotByItemSlot.size()) {
    return -1;
  }

  return kCharacterComponentPage7SlotByItemSlot[item_slot];
}

[[nodiscard]] bool
QueueStoredComponentTextureForPage(CharacterModelRefreshState &model, std::size_t page_index,
                                   const CharacterComponentItemDisplayRecordState *record,
                                   int queue_slot) {
  if (record == nullptr || queue_slot < 0 || !record->HasComponentTexture(page_index)) {
    return false;
  }

  return QueueCharacterComponentTexture(
      model.texture_pages[page_index], page_index, static_cast<std::size_t>(queue_slot),
      record->component_textures[page_index], model.explicit_base_texture_mode);
}

void FinalizeCharacterModelQueuedTextureMutation(CharacterModelRefreshState &model,
                                                 std::size_t page_index) {
  model.dirty_region_mask |= (1u << page_index);
  ClearPendingCharacterComponentRequest(model);
  model.flags &= ~kCharacterModelFlagRefreshReady;
}

void UpdateCharacterModelFixedComponentTexturePage(
    CharacterModelRefreshState &model, std::size_t page_index, int queue_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  if (queue_texture) {
    if (!QueueStoredComponentTextureForPage(model, page_index, item_record, queue_slot)) {
      return;
    }
  } else if (queue_slot >= 0) {
    ClearQueuedCharacterComponentTexture(model.texture_pages[page_index], queue_slot);
  }

  if (queue_slot < 0) {
    return;
  }

  FinalizeCharacterModelQueuedTextureMutation(model, page_index);
}

}

bool RefreshCharacterModelComponentTable(CharacterModelRefreshState &model,
                                         CharacterComponentTextureRefreshOptions options) {
  CharacterComponentTextureMetadata metadata;
  for (auto &row : model.component_table) {
    for (auto &texture : row) {
      if (!texture) {
        continue;
      }

      if (!EnsureCharacterComponentTextureMetadataReady(*texture, metadata, options)) {
        return false;
      }
    }
  }

  model.flags &= ~kCharacterModelFlagComponentTableDirty;
  return true;
}

bool RebuildCharacterModelComponentSlots(CharacterModelRefreshState &model,
                                         CharacterComponentTextureRefreshOptions options) {
  bool success = true;
  CharacterComponentTextureMetadata metadata;

  for (std::size_t page_index = 0; page_index < model.texture_pages.size(); ++page_index) {
    if ((model.dirty_region_mask & (1u << page_index)) == 0) {
      continue;
    }

    auto &page = model.texture_pages[page_index];
    for (std::size_t slot_index = 0; slot_index < kCharacterComponentCleanupSlotCount;
         ++slot_index) {
      auto &live_slot = page.live_slots[slot_index];
      const auto &queued_slot = page.queued_slots[slot_index];

      if (!live_slot) {
        if (!queued_slot) {
          continue;
        }

        live_slot = MaterializeLiveCharacterComponentTexture(queued_slot);
        if (!live_slot) {
          continue;
        }
      }

      if (!options.allow_synchronous_wait &&
          !EnsureCharacterComponentTextureMetadataReady(*live_slot, metadata, options)) {
        success = false;
        continue;
      }

      if (options.allow_synchronous_wait) {
        (void)EnsureCharacterComponentTextureMetadataReady(*live_slot, metadata, options);
      }

      const auto bit = 1u << slot_index;
      if (HasPendingCharacterComponentTextureLoad(live_slot.get())) {
        page.loading_slot_mask |= bit;
      } else {
        page.loading_slot_mask &= ~bit;
      }
    }
  }

  return success;
}

void MarkCharacterModelDirty(CharacterModelRefreshState &model, std::uint32_t dirty_mask) {
  model.dirty_region_mask |= dirty_mask;
  ClearPendingCharacterComponentRequest(model);
  model.flags &= ~kCharacterModelFlagRefreshReady;
}

void MarkCharacterModelSkinDirty(CharacterModelRefreshState &model) {
  model.flags |= kCharacterModelFlagSkinDirty;
  ClearPendingCharacterComponentRequest(model);
  model.flags &= ~kCharacterModelFlagRefreshReady;
}

bool InitializeCharacterModelExplicitBaseTextureMode(
    CharacterModelRefreshState &model, const CharacterComponentBaseSectionTable &base_section_table,
    int race, int gender, int choice) {
  model.explicit_base_texture_mode = false;

  if (!LoadCharacterModelTextureUnitFromPath(model.explicit_base_texture.configured_path, 1,
                                             model.explicit_base_texture.texture_type1_load,
                                             model.explicit_base_texture.texture_type1_replacement,
                                             model.explicit_base_texture.scratch_path)) {
    return false;
  }

  (void)LoadTextureUnit8FromBaseComponentSection(
      base_section_table, race, gender, choice, model.explicit_base_texture.texture_unit8_load,
      model.explicit_base_texture.texture_unit8_replacement,
      model.explicit_base_texture.unit8_scratch_path);

  model.explicit_base_texture_mode = true;
  model.flags |= kCharacterModelFlagSkinDirty | kCharacterModelFlagGeosetsDirty;
  ClearPendingCharacterComponentRequest(model);
  model.flags &= ~kCharacterModelFlagRefreshReady;
  return true;
}

void UpdateCharacterModelComponentTexturePage0(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  UpdateCharacterModelFixedComponentTexturePage(model, kCharacterComponentPage0Index,
                                                ResolveCharacterComponentPage0Slot(item_slot),
                                                item_record, queue_texture);
}

void UpdateCharacterModelComponentTexturePage1(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  UpdateCharacterModelFixedComponentTexturePage(
      model, kCharacterComponentPage1Index,
      ResolveCharacterComponentPage1Slot(item_slot, item_record), item_record, queue_texture);
}

void UpdateCharacterModelComponentTexturePage2(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  UpdateCharacterModelFixedComponentTexturePage(model, kCharacterComponentPage2Index,
                                                ResolveCharacterComponentPage2Slot(item_slot),
                                                item_record, queue_texture);
}

void UpdateCharacterModelComponentTexturePage3(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  UpdateCharacterModelFixedComponentTexturePage(model, kCharacterComponentPage3Index,
                                                ResolveCharacterComponentPage3Slot(item_slot),
                                                item_record, queue_texture);
}

void UpdateCharacterModelComponentTexturePage4(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  UpdateCharacterModelFixedComponentTexturePage(model, kCharacterComponentPage4Index,
                                                ResolveCharacterComponentPage4Slot(item_slot),
                                                item_record, queue_texture);
}

void UpdateCharacterModelComponentTexturePage5(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  UpdateCharacterModelFixedComponentTexturePage(model, kCharacterComponentPage5Index,
                                                ResolveCharacterComponentPage5Slot(item_slot),
                                                item_record, queue_texture);
}

void UpdateCharacterModelComponentTexturePage6(
    CharacterModelRefreshState &model, std::size_t item_slot, int default_slot_index,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  auto resolved_slot = default_slot_index;
  const auto *chest_record = StoredItemDisplayRecord(model, kCharacterModelChestItemSlot);

  if (item_record != nullptr && item_record->geoset_control_3 != 0) {
    if (item_slot == kCharacterModelChestItemSlot) {
      resolved_slot = kCharacterComponentPage6ChestLayerSlot;
      model.flags |= kCharacterModelFlagGeosetsDirty;
    } else if (item_slot == kCharacterModelWaistItemSlot) {
      resolved_slot = (chest_record != nullptr && chest_record->geoset_control_3 != 0)
                          ? kCharacterComponentPage6WaistPrimaryLayerSlot
                          : kCharacterComponentPage6WaistFallbackLayerSlot;
      model.flags |= kCharacterModelFlagGeosetsDirty;
    }
  } else if (item_slot == kCharacterModelLegsItemSlot && item_record != nullptr &&
             item_record->geoset_control_1 != 0) {
    resolved_slot = kCharacterComponentPage6WaistPrimaryLayerSlot;
    model.flags |= kCharacterModelFlagGeosetsDirty;
  }

  if (queue_texture) {
    if (!QueueStoredComponentTextureForPage(model, kCharacterComponentPage6Index, item_record,
                                            resolved_slot)) {
      return;
    }
  } else if (resolved_slot >= 0) {
    ClearQueuedCharacterComponentTexture(model.texture_pages[kCharacterComponentPage6Index],
                                         resolved_slot);
  }

  if (!queue_texture) {
    if (item_slot == kCharacterModelChestItemSlot) {
      const auto *waist_record = StoredItemDisplayRecord(model, kCharacterModelWaistItemSlot);
      if (waist_record != nullptr && waist_record->geoset_control_3 != 0) {
        ClearQueuedCharacterComponentTexture(model.texture_pages[kCharacterComponentPage6Index],
                                             kCharacterComponentPage6WaistPrimaryLayerSlot);
        (void)QueueStoredComponentTextureForPage(model, kCharacterComponentPage6Index, waist_record,
                                                 kCharacterComponentPage6WaistFallbackLayerSlot);
      }

      const auto *legs_record = StoredItemDisplayRecord(model, kCharacterModelLegsItemSlot);
      if (legs_record != nullptr && legs_record->geoset_control_1 != 0) {
        (void)QueueStoredComponentTextureForPage(model, kCharacterComponentPage6Index, legs_record,
                                                 kCharacterComponentPage6WaistPrimaryLayerSlot);
      }
    } else if (item_slot == kCharacterModelWaistItemSlot) {
      const auto *waist_record = StoredItemDisplayRecord(model, kCharacterModelWaistItemSlot);
      if (waist_record != nullptr && waist_record->geoset_control_3 != 0) {
        (void)QueueStoredComponentTextureForPage(model, kCharacterComponentPage6Index, waist_record,
                                                 kCharacterComponentPage6WaistFallbackLayerSlot);
      }
    }
  }

  if (resolved_slot < 0) {
    return;
  }

  FinalizeCharacterModelQueuedTextureMutation(model, kCharacterComponentPage6Index);
}

void UpdateCharacterModelComponentTexturePage7(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture) {
  auto queue_slot = ResolveCharacterComponentPage7Slot(item_slot);

  if ((model.flags & kCharacterModelFlagGuildTabardOverride) == 0 && queue_texture) {
    if (!QueueStoredComponentTextureForPage(model, kCharacterComponentPage7Index, item_record,
                                            queue_slot)) {
      return;
    }
  } else if (queue_slot >= 0) {
    ClearQueuedCharacterComponentTexture(model.texture_pages[kCharacterComponentPage7Index],
                                         queue_slot);
  }

  if (queue_slot < 0) {
    return;
  }

  FinalizeCharacterModelQueuedTextureMutation(model, kCharacterComponentPage7Index);
}

namespace {

[[nodiscard]] bool IsCharacterModelSpecialItemRefreshCurrent(
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops,
    const CharacterModelSpecialItemRefreshTarget target) {
  return special_refresh_ops.is_current && special_refresh_ops.is_current(target);
}

void ClearCharacterModelSpecialItemRefreshTarget(
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops,
    const CharacterModelSpecialItemRefreshTarget target) {
  if (!special_refresh_ops.clear_loaded) {
    return;
  }

  special_refresh_ops.clear_loaded(target);
}

void LoadCharacterModelSpecialItemRefreshTarget(
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops,
    const CharacterModelSpecialItemRefreshTarget target,
    const std::uint32_t special_refresh_token) {
  if (!special_refresh_ops.load) {
    return;
  }

  special_refresh_ops.load(target, special_refresh_token);
}

void DispatchCharacterModelStoredItemRecordPages(
    CharacterModelRefreshState &model, const std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record) {
  UpdateCharacterModelComponentTexturePage0(model, item_slot, item_record, true);
  UpdateCharacterModelComponentTexturePage1(model, item_slot, item_record, true);
  UpdateCharacterModelComponentTexturePage2(model, item_slot, item_record, true);
  UpdateCharacterModelComponentTexturePage3(model, item_slot, item_record, true);
  UpdateCharacterModelComponentTexturePage4(model, item_slot, item_record, true);
  UpdateCharacterModelComponentTexturePage5(model, item_slot, item_record, true);
  UpdateCharacterModelComponentTexturePage6(
      model, item_slot, ResolveCharacterComponentPage6BaseSlot(item_slot), item_record, true);
  UpdateCharacterModelComponentTexturePage7(model, item_slot, item_record, true);
}

}

bool SetCharacterModelItemDisplayRecordAndRefresh(
    CharacterModelRefreshState &model, const std::size_t item_slot,
    const std::shared_ptr<CharacterComponentItemDisplayRecordState> &item_record,
    const std::uint32_t display_flags, const std::uint32_t special_refresh_token,
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops) {
  if (!item_record || item_slot >= model.item_display_records.size()) {
    return false;
  }

  model.flags |= kCharacterModelFlagGeosetsDirty;
  model.item_display_records[item_slot] = item_record;

  switch (item_slot) {
  case 0:
    if (!IsCharacterModelSpecialItemRefreshCurrent(special_refresh_ops,
                                                   CharacterModelSpecialItemRefreshTarget::Head)) {
      ClearCharacterModelSpecialItemRefreshTarget(special_refresh_ops,
                                                  CharacterModelSpecialItemRefreshTarget::Head);
      LoadCharacterModelSpecialItemRefreshTarget(
          special_refresh_ops, CharacterModelSpecialItemRefreshTarget::Head, special_refresh_token);
    }
    return true;
  case 1:
    if (!IsCharacterModelSpecialItemRefreshCurrent(
            special_refresh_ops, CharacterModelSpecialItemRefreshTarget::Shoulders)) {
      ClearCharacterModelSpecialItemRefreshTarget(
          special_refresh_ops, CharacterModelSpecialItemRefreshTarget::Shoulders);
      LoadCharacterModelSpecialItemRefreshTarget(special_refresh_ops,
                                                 CharacterModelSpecialItemRefreshTarget::Shoulders,
                                                 special_refresh_token);
    }
    return true;
  case 3:
    model.display_behavior_flags =
        (model.display_behavior_flags & ~kCharacterModelDisplayBehaviorFlag0x20) |
        ((display_flags & 0x4u) != 0u ? kCharacterModelDisplayBehaviorFlag0x20 : 0u);
    break;
  case 10:
    LoadCharacterModelSpecialItemRefreshTarget(
        special_refresh_ops, CharacterModelSpecialItemRefreshTarget::Cape, special_refresh_token);
    return true;
  case 11:
    if (!IsCharacterModelSpecialItemRefreshCurrent(
            special_refresh_ops, CharacterModelSpecialItemRefreshTarget::Quiver)) {
      ClearCharacterModelSpecialItemRefreshTarget(special_refresh_ops,
                                                  CharacterModelSpecialItemRefreshTarget::Quiver);
      LoadCharacterModelSpecialItemRefreshTarget(special_refresh_ops,
                                                 CharacterModelSpecialItemRefreshTarget::Quiver,
                                                 special_refresh_token);
    }
    return true;
  default:
    break;
  }

  if (!model.explicit_base_texture_mode) {
    DispatchCharacterModelStoredItemRecordPages(model, item_slot, item_record.get());
  }

  return true;
}

bool ResolveCharacterModelItemDisplayRecordAndRefresh(
    CharacterModelRefreshState &model, const std::size_t item_slot, const std::uint32_t display_id,
    const CharacterModelItemDisplayLookupFn &lookup_display_info,
    const CharacterModelItemDisplayRecordFactory &record_factory,
    const std::uint32_t special_refresh_token,
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops) {
  if (display_id == 0 || !lookup_display_info || !record_factory) {
    return false;
  }

  const auto *display_info = lookup_display_info(display_id);
  if (display_info == nullptr) {
    return false;
  }

  auto item_record = record_factory(*display_info);
  if (!item_record) {
    return false;
  }

  return SetCharacterModelItemDisplayRecordAndRefresh(model, item_slot, item_record,
                                                      display_info->flags, special_refresh_token,
                                                      special_refresh_ops);
}

namespace {

constexpr int kEquipSlotToModelSlot[] = {

       0,     -1,      1,          2,       3,       4,       5,      6,      7,

       8,       -1,       -1,       -1,           -1,          10,

       -1,          -1,         -1,         9
};
constexpr std::uint32_t kMaxEquipSlot = 18;

}

bool SetCharacterModelEquipmentSlotDisplayId(
    CharacterModelRefreshState &model, const std::uint32_t equipment_slot,
    const std::uint32_t display_id,
    const CharacterModelItemDisplayLookupFn &lookup_display_info,
    const CharacterModelItemDisplayRecordFactory &record_factory,
    const std::uint32_t special_refresh_token,
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops) {
  if (equipment_slot > kMaxEquipSlot) {
    return false;
  }

  const int model_slot = kEquipSlotToModelSlot[equipment_slot];
  if (model_slot < 0) {
    return false;
  }

  return ResolveCharacterModelItemDisplayRecordAndRefresh(
      model, static_cast<std::size_t>(model_slot), display_id,
      lookup_display_info, record_factory, special_refresh_token,
      special_refresh_ops);
}

void QueueCharacterModelRefreshAtHead(CharacterModelRefreshQueueState &queue,
                                      CharacterModelRefreshState &model) {
  if (model.pending_link.linked) {
    return;
  }

  queue.pending_models.push_front(&model);
  model.pending_link.linked = true;
}

void HandleCharacterModelInitRefreshGate(CharacterModelRefreshQueueState &queue,
                                         CharacterModelRefreshState &model) {
  if (model.explicit_base_texture_mode) {
    ApplyCharacterModelGeosetVisibilityIfDirty(model);
    return;
  }

  if (model.dirty_region_mask != 0 || (model.flags & kCharacterModelFlagSkinDirty) != 0) {
    QueueCharacterModelRefreshAtHead(queue, model);
    return;
  }

  ApplyCharacterModelGeosetVisibilityIfDirty(model);
}

void ReleaseLiveCharacterComponentTexturesForQueuedSlots(
    CharacterComponentTexturePages &texture_pages) {
  for (auto &page : texture_pages) {
    for (std::size_t slot_index = 0; slot_index < kCharacterComponentCleanupSlotCount;
         ++slot_index) {
      if (!page.queued_slots[slot_index] || !page.live_slots[slot_index]) {
        continue;
      }

      page.live_slots[slot_index]->Release();
      page.live_slots[slot_index].reset();
    }
  }
}

void FlushCharacterModelCompositeTexture(CharacterModelRefreshState &model,
                                         bool process_dirty_handlers) {
  ResetCompositeFlushLog(model.composite_flush);

  for (std::uint8_t region = 0; region < kCharacterComponentStandardTextureCount; ++region) {
    const std::uint32_t dirty_bit = 1u << region;
    if ((model.dirty_region_mask & dirty_bit) == 0) {
      continue;
    }

    if (process_dirty_handlers) {
      model.composite_flush.processed_dirty_mask |= dirty_bit;
      model.composite_flush.processed_regions.push_back(region);
      RecordCompositeRegionPasses(model, region);
    }

    if ((model.flags & kCharacterModelFlagSkinDirty) == 0 && model.request_type != 6) {
      model.composite_flush.blitted_regions.push_back(region);
    }
  }

  if ((model.flags & kCharacterModelFlagSkinDirty) == 0 && model.request_type != 6) {
    return;
  }

  if (process_dirty_handlers && model.request_type == 6) {
    model.composite_flush.special_atlas_flush = true;
  }

  if ((model.flags & kCharacterModelFlagSkinDirty) != 0) {
    model.composite_flush.full_texture_blit = true;
    return;
  }

  RecordDirtyRegionBlits(model);
}

inline constexpr std::uint32_t kCharSectionsFlagScalpComposite = 0x8u;

void PopulateScalpCompositeRegion(
    CharacterModelRefreshState &model,
    const CharacterComponentBaseSectionTable &base_section_table,
    int race, int gender, int skin_color,
    std::uint8_t region_index,
    const std::shared_ptr<CharacterComponentTextureHandle> &standard_texture,
    const std::array<std::shared_ptr<CharacterComponentTextureHandle>, 3> &extra_textures) {
  if (region_index >= model.composite_regions.size()) {
    return;
  }

  auto &region = model.composite_regions[region_index];
  region.standard_texture.reset();
  region.extra_textures.clear();

  const auto *entry = base_section_table.FindEntry(race, gender, 0, 0, skin_color);
  if (entry != nullptr && (entry->flags & kCharSectionsFlagScalpComposite) != 0) {
    if (standard_texture) {
      region.standard_texture = standard_texture;
    }
  }

  for (const auto &extra : extra_textures) {
    if (extra) {
      CharacterComponentCompositeExtraTexture extra_entry;
      extra_entry.component_index = region_index;
      extra_entry.texture = extra;
      region.extra_textures.push_back(std::move(extra_entry));
    }
  }
}

void FullRefreshCharacterModelFromBaseSkinCallback(CharacterModelRefreshRuntimeState &runtime,
                                                   CharacterModelRefreshState &model) {
  if (runtime.finalize_refresh_in_progress) {
    return;
  }

  runtime.finalize_refresh_in_progress = true;

  CharacterComponentTextureRefreshOptions refresh_options;
  refresh_options.allow_synchronous_wait = true;

  model.flags |= kCharacterModelFlagSkinDirty;
  ClearPendingCharacterComponentRequest(model);
  model.flags &= ~kCharacterModelFlagRefreshReady;
  model.dirty_region_mask = 0xFFFFFFFFu;

  (void)RefreshCharacterModelComponentTable(model, refresh_options);
  (void)RebuildCharacterModelComponentSlots(model, refresh_options);
  FlushCharacterModelCompositeTexture(model, true);

  model.flags &= ~kCharacterModelFlagSkinDirty;
  model.dirty_region_mask = 0;
  ReleaseLiveCharacterComponentTexturesForQueuedSlots(model.texture_pages);
  UnlinkCharacterModelRefreshIfLinked(model);

  runtime.finalize_refresh_in_progress = false;
}

void CharacterBaseSkinRenderTargetUploadCallback(CharacterModelRefreshRuntimeState &runtime,
                                                 CharacterModelRefreshState &model,
                                                 const int action, const std::uint32_t width,
                                                 const std::uint32_t ,
                                                 const int , const int mip_level,
                                                 std::uint32_t *pitch_out, const void **data_out) {
  model.base_skin_render_target.last_upload_action = action;
  model.base_skin_render_target.last_upload_mip_level = mip_level;

  switch (action) {
  case 0:
    FullRefreshCharacterModelFromBaseSkinCallback(runtime, model);
    return;
  case 1: {
    EnsureCharacterBaseSkinUploadSnapshot(model.base_skin_render_target, model.request_type);

    const auto selection = SelectCharacterBaseSkinUploadSource(model);
    const void *const data =
        ResolveCharacterBaseSkinUploadDataPointer(selection.chain, model.request_type, mip_level);
    std::uint32_t pitch = width * kCharacterBaseSkinArgbBytesPerPixel;
    if (model.request_type == 6) {
      pitch = std::max(kCharacterBaseSkinBc1BlockBytes,
                       kCharacterBaseSkinBc1BlockBytes * (width >> 2u));
    }

    if (pitch_out) {
      *pitch_out = pitch;
    }
    if (data_out) {
      *data_out = data;
    }

    model.base_skin_render_target.last_upload_source = selection.source;
    model.base_skin_render_target.last_upload_pitch = pitch;
    model.base_skin_render_target.last_upload_data = reinterpret_cast<std::uintptr_t>(data);
    return;
  }
  case 3:
    if (!model.request_type_2_upload_mode) {
      MarkCharacterModelSkinDirty(model);
      MarkCharacterModelDirty(model, 0xFFFFFFFFu);
    }
    return;
  default:
    return;
  }
}

void FinalizeCharacterModelRefresh(CharacterModelRefreshRuntimeState &runtime,
                                   CharacterModelRefreshState &model) {
  runtime.finalize_refresh_in_progress = true;

  ApplyCharacterModelGeosetVisibilityIfDirty(model);
  EnsureCharacterBaseSkinRenderTarget(model.base_skin_render_target, model.request_type);
  FlushCharacterModelCompositeTexture(model, true);

  model.flags &= ~kCharacterModelFlagSkinDirty;
  model.dirty_region_mask = 0;
  ReleaseLiveCharacterComponentTexturesForQueuedSlots(model.texture_pages);
  UnlinkCharacterModelRefreshIfLinked(model);

  runtime.finalize_refresh_in_progress = false;
}

void FinalizeQueuedCharacterModelRefresh(CharacterModelRefreshRuntimeState &runtime,
                                         CharacterModelRefreshState &model) {
  runtime.finalize_refresh_in_progress = true;

  ApplyCharacterModelGeosetVisibilityIfDirty(model);
  EnsureCharacterBaseSkinRenderTarget(model.base_skin_render_target, model.request_type);
  FlushCharacterModelCompositeTexture(model, false);

  if (model.pending_request && model.pending_request->output_chain) {
    RememberCharacterBaseSkinUploadSnapshot(model.base_skin_render_target, model.request_type,
                                            model.pending_request->output_chain);
  }

  ClearPendingCharacterComponentRequest(model);

  model.flags &= ~kCharacterModelFlagSkinDirty;
  model.dirty_region_mask = 0;
  ReleaseLiveCharacterComponentTexturesForQueuedSlots(model.texture_pages);

  runtime.finalize_refresh_in_progress = false;
}

void ProcessCharacterModelRefreshQueue(CharacterModelRefreshQueueState &queue,
                                       CharacterModelRefreshRuntimeState &runtime,
                                       const CharacterModelRefreshQueueOperations &operations,
                                       bool render_enabled, bool worker_thread_enabled) {
  assert(operations.refresh_component_table);
  assert(operations.rebuild_component_slots);
  if (worker_thread_enabled) {
    assert(operations.queue_component_request);
    assert(operations.drain_completed_requests);
  }

  std::size_t index = 0;
  while (index < queue.pending_models.size()) {
    CharacterModelRefreshState *model = queue.pending_models[index];
    if (model == nullptr) {
      queue.pending_models.erase(queue.pending_models.begin() + static_cast<std::ptrdiff_t>(index));
      continue;
    }

    if (model->pending_request) {
      if (!render_enabled ||
          (model->pending_request->flags & CharacterComponentRequest::kCompleted) == 0) {
        ++index;
        continue;
      }

      FinalizeQueuedCharacterModelRefresh(runtime, *model);
      UnlinkCharacterModelRefreshIfLinked(*model);

      queue.pending_models.erase(queue.pending_models.begin() + static_cast<std::ptrdiff_t>(index));
      continue;
    }

    if (!operations.refresh_component_table(*model) ||
        !operations.rebuild_component_slots(*model)) {
      ++index;
      continue;
    }

    model->flags |= kCharacterModelFlagRefreshReady;
    if (worker_thread_enabled) {
      operations.queue_component_request(*model);
      ++index;
      continue;
    }

    if (render_enabled) {
      FinalizeCharacterModelRefresh(runtime, *model);
      queue.pending_models.erase(queue.pending_models.begin() + static_cast<std::ptrdiff_t>(index));
      continue;
    }

    ++index;
  }

  if (worker_thread_enabled) {
    operations.drain_completed_requests(0);
  }
}

void ResetCharacterComponentRequestForQueue(CharacterComponentRequest &request) {
  request.flags = (request.flags & ~CharacterComponentRequest::kCompleted) |
                  CharacterComponentRequest::kNeedsComposite;
  request.output_chain.reset();
  request.extra_texture_indices.clear();
  request.extra_texture_ids.clear();
  request.extra_texture_handles.clear();
}

bool AppendCharacterComponentExtraTexture(CharacterComponentRequest &request,
                                          std::uint8_t component_index, int texture_id) {
  const auto extra_count =
      std::min(std::min(request.extra_texture_indices.size(), request.extra_texture_ids.size()),
               request.extra_texture_handles.size());
  request.extra_texture_indices.resize(extra_count);
  request.extra_texture_ids.resize(extra_count);
  request.extra_texture_handles.resize(extra_count);
  if (extra_count >= kCharacterComponentMaxExtraTextureCount) {
    return false;
  }

  request.extra_texture_ids.push_back(texture_id);
  request.extra_texture_indices.push_back(component_index);
  request.extra_texture_handles.push_back(nullptr);
  return true;
}

void AssembleCharacterComponentRequestRegion(CharacterModelRefreshState &model,
                                             std::uint8_t region_index,
                                             CharacterComponentRequest &request) {
  if (region_index >= model.composite_regions.size()) {
    return;
  }

  const auto &region = model.composite_regions[region_index];
  if (region.standard_texture) {
    region.standard_texture->Retain();
    request.standard_texture_handles[region_index] = region.standard_texture;
    request.standard_texture_ids[region_index] = region.standard_texture->source_key;
  } else {
    request.standard_texture_handles[region_index].reset();
    request.standard_texture_ids[region_index] = 0;
  }

  for (const auto &extra_texture : region.extra_textures) {
    if (!AppendCharacterComponentExtraTexture(
            request, extra_texture.component_index,
            extra_texture.texture ? extra_texture.texture->source_key : 0)) {
      break;
    }

    if (!extra_texture.texture) {
      continue;
    }

    extra_texture.texture->Retain();
    request.extra_texture_handles.back() = extra_texture.texture;
  }
}

void RegisterCharacterComponentRequestAllocation(
    CharacterComponentWorkerAllocations &allocations,
    const std::shared_ptr<CharacterComponentRequest> &request) {
  if (!request) {
    return;
  }

  const auto it =
      std::find(allocations.owned_requests.begin(), allocations.owned_requests.end(), request);
  if (it != allocations.owned_requests.end()) {
    return;
  }

  allocations.owned_requests.push_back(request);
}

void QueueCharacterModelComponentRequest(
    CharacterModelRefreshState &model, std::uint32_t component_mask,
    const CharacterComponentAssemblerDispatchTable &dispatch_table,
    CharacterComponentWorkerQueues &queues, CharacterComponentWorkerWakeState &wake_state,
    CharacterComponentWorkerAllocations *allocations) {
  auto request = AcquireCharacterComponentRequestForQueue(allocations);
  request->request_type = model.request_type;
  model.pending_request = request;

  for (std::uint32_t bit_index = 0; bit_index < kCharacterComponentStandardTextureCount;
       ++bit_index) {
    if ((component_mask & (1u << bit_index)) == 0) {
      continue;
    }

    const auto handler = dispatch_table.handlers[bit_index];
    if (handler != nullptr) {
      handler(model, *request);
    }
  }

  PushCharacterComponentRequestToHead(queues.pending_requests, request);
  ++wake_state.set_event_calls;
}

std::size_t
DrainCompletedCharacterComponentRequests(CharacterComponentWorkerQueues &queues,
                                         CharacterComponentWorkerAllocations &allocations,
                                         bool force_all) {
  std::size_t retired_count = 0;
  std::deque<std::shared_ptr<CharacterComponentRequest>> retained_requests;

  while (!queues.completed_requests.empty()) {
    auto request = queues.completed_requests.front();
    queues.completed_requests.pop_front();
    if (!request) {
      continue;
    }

    if (!force_all && (request->flags & CharacterComponentRequest::kNeedsComposite) != 0) {
      retained_requests.push_back(std::move(request));
      continue;
    }

    ReleaseCharacterComponentRequestTextures(*request);
    if (request->output_chain) {
      ResetCharacterComponentMipChainForReuse(*request->output_chain);
      auto chain = std::move(request->output_chain);
      auto &free_list = (request->request_type == 6) ? queues.special_free_mip_chains
                                                     : queues.general_free_mip_chains;
      free_list.push_front(std::move(chain));
    }

    allocations.reusable_requests.push_front(std::move(request));
    ++retired_count;
  }

  queues.completed_requests = std::move(retained_requests);
  return retired_count;
}

CharacterComponentWorkerTeardownSummary
FreeCharacterComponentWorkerMipPoolsAndRequests(CharacterComponentWorkerQueues &queues,
                                                CharacterComponentWorkerAllocations &allocations) {
  CharacterComponentWorkerTeardownSummary summary;
  std::vector<CharacterComponentMipChain *> seen_chains;
  seen_chains.reserve(queues.general_free_mip_chains.size() +
                      queues.special_free_mip_chains.size());

  ResetTrackedMipChainList(queues.general_free_mip_chains, summary.freed_general_mip_chains,
                           seen_chains);
  ResetTrackedMipChainList(queues.special_free_mip_chains, summary.freed_special_mip_chains,
                           seen_chains);

  for (const auto &request : allocations.owned_requests) {
    if (!request) {
      continue;
    }

    ResetCharacterComponentRequestAllocation(*request, seen_chains);
    ++summary.freed_request_allocations;
  }

  allocations.owned_requests.clear();
  allocations.reusable_requests.clear();
  return summary;
}

CharacterComponentBackendShutdownSummary
ShutdownCharacterComponentBackend(CharacterComponentBackendRuntimeState &state) {
  CharacterComponentBackendShutdownSummary summary;

  auto &shutdown_state = state.shutdown_state;
  if (!shutdown_state) {
    state.worker_backend = {};
    state.config = {};
    state.initialized = false;
    return summary;
  }

  if (shutdown_state->active_model) {
    ShutdownCharacterModelRefreshState(*shutdown_state->active_model);
    summary.model_shutdown = true;
    if (shutdown_state->type_handle_storage.allocated) {
      ++shutdown_state->type_handle_storage.release_calls;
      shutdown_state->type_handle_storage.last_released_handle =
          shutdown_state->active_model_type_handle;
    }

    shutdown_state->active_model.reset();
    shutdown_state->active_model_type_handle = 0;
  }

  if (state.worker_backend.active) {
    summary.worker_backend_shutdown = true;
    summary.worker_shutdown_signal_calls = 1;
    summary.worker_shutdown_wait_calls = 1;

    summary.flushed_pending_requests = shutdown_state->worker_queues.pending_requests.size();
    while (!shutdown_state->worker_queues.pending_requests.empty()) {
      shutdown_state->worker_allocations.reusable_requests.push_back(
          std::move(shutdown_state->worker_queues.pending_requests.front()));
      shutdown_state->worker_queues.pending_requests.pop_front();
    }
    summary.drained_completed_requests = DrainCompletedCharacterComponentRequests(
        shutdown_state->worker_queues, shutdown_state->worker_allocations, true);
    summary.worker_teardown = FreeCharacterComponentWorkerMipPoolsAndRequests(
        shutdown_state->worker_queues, shutdown_state->worker_allocations);
  }

  for (auto &texture : shutdown_state->composite_textures) {
    if (!texture.allocated) {
      continue;
    }

    texture.allocated = false;
    ++texture.release_calls;
    ++summary.released_composite_textures;
  }

  for (auto &row : shutdown_state->base_section_lookup_rows) {
    for (auto &section : row.sections) {
      if (section.entry_arrays.empty()) {
        continue;
      }

      section.entry_arrays.clear();
      ++section.release_calls;
      ++summary.released_section_lookup_entry_arrays;
    }
  }

  if (!shutdown_state->base_section_lookup_rows.empty()) {
    shutdown_state->base_section_lookup_rows.clear();
    shutdown_state->base_section_table.entries.clear();
    shutdown_state->base_section_table.texture_paths.clear();
    ++shutdown_state->base_section_lookup_storage_release_calls;
    summary.released_section_lookup_storage = true;
  }

  if (shutdown_state->facial_hair_style_counts_allocated) {
    shutdown_state->facial_hair_style_counts.clear();
    shutdown_state->facial_hair_style_counts_allocated = false;
    ++shutdown_state->facial_hair_style_counts_release_calls;
    summary.released_facial_hair_style_counts = true;
  }

  if (shutdown_state->hair_geoset_storage_allocated) {
    shutdown_state->hair_geoset_storage_allocated = false;
    ++shutdown_state->hair_geoset_storage_release_calls;
    summary.released_hair_geosets = true;
  }

  if (shutdown_state->type_handle_storage.allocated) {
    shutdown_state->type_handle_storage.allocated = false;
    shutdown_state->type_handle_storage.type_index = 0;
    ++shutdown_state->type_handle_storage.free_calls;
    summary.released_type_handle_storage = true;
  }

  if (shutdown_state->refresh_queue_handler.registered) {
    shutdown_state->refresh_queue_handler.registered = false;
    ++shutdown_state->refresh_queue_handler.unregister_calls;
    summary.refresh_queue_handler_unregistered = true;
  }

  state.worker_backend = {};
  state.config = {};
  state.initialized = false;
  return summary;
}

CharacterComponentBackendShutdownSummary ShutdownCharacterComponentBackendRuntimeState() {
  return ShutdownCharacterComponentBackend(g_character_component_backend_runtime_state);
}

std::shared_ptr<CharacterComponentMipChain> AcquireFreeMipChainForRequestType(
    std::deque<std::shared_ptr<CharacterComponentMipChain>> &general_free_list,
    std::deque<std::shared_ptr<CharacterComponentMipChain>> &special_free_list, int request_type) {
  auto &free_list = (request_type == 6) ? special_free_list : general_free_list;
  if (free_list.empty()) {
    return nullptr;
  }

  auto acquired = free_list.front();
  free_list.pop_front();
  return acquired;
}

std::uint32_t
FillMissingPaletteCharacterComponentMipRegion(CharacterComponentMipChain &chain,
                                              CharacterComponentMipFillRegion destination_region,
                                              std::uint32_t start_level, std::uint8_t mip_count) {
  constexpr std::uint32_t kOpaqueGreen = 0xFF00FF00u;

  if (start_level >= mip_count) {
    return start_level;
  }

  assert(chain.mip_levels.size() >= mip_count);

  std::uint32_t current_width = destination_region.width;
  std::uint32_t current_height = destination_region.height;
  std::int32_t current_x = destination_region.x;
  std::int32_t current_y = destination_region.y;

  for (std::uint32_t level = start_level; level < mip_count; ++level) {
    auto &surface = chain.mip_levels[level];
    assert(current_x >= 0);
    assert(current_y >= 0);
    assert(current_width <= surface.width);
    assert(current_height <= surface.height);
    assert(static_cast<std::uint32_t>(current_x) <= surface.width - current_width);
    assert(static_cast<std::uint32_t>(current_y) <= surface.height - current_height);
    assert(surface.pixels.size() == static_cast<std::size_t>(surface.width) * surface.height);

    for (std::uint32_t row = 0; row < current_height; ++row) {
      const std::size_t row_offset =
          static_cast<std::size_t>(current_y + static_cast<std::int32_t>(row)) * surface.width +
          static_cast<std::size_t>(current_x);
      std::fill_n(surface.pixels.begin() + row_offset, current_width, kOpaqueGreen);
    }

    current_x = ArithmeticShiftRightOne(current_x);
    current_y = ArithmeticShiftRightOne(current_y);
    current_width = HalveDimensionClampOne(current_width);
    current_height = HalveDimensionClampOne(current_height);
  }

  return mip_count;
}

std::uint32_t CompositePalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, CharacterComponentMipOrigin source_origin,
    std::uint32_t start_level) {
  if (start_level >= source_texture.mip_count) {
    return start_level;
  }

  if (!source_texture.has_palette) {
    const auto destination_mip_count =
        static_cast<std::uint8_t>(source_texture.mip_count - start_level);
    (void)FillMissingPaletteCharacterComponentMipRegion(
        chain, destination_region, 0, destination_mip_count);
    return source_texture.mip_count;
  }

  assert(chain.mip_levels.size() >=
         static_cast<std::size_t>(source_texture.mip_count - start_level));

  switch (source_texture.alpha_depth) {
  case 0:
    CompositeOpaquePalettedCharacterComponentMipRegionAcrossMips(
        chain, source_texture, destination_region, source_origin, start_level, 0);
    break;
  case 1:
    CompositeMaskedPalettedCharacterComponentMipRegionAcrossMips(
        chain, source_texture, destination_region, start_level, 0);
    break;
  case 4:
    CompositeArgb4PalettedCharacterComponentMipRegionAcrossMips(chain, source_texture,
                                                                destination_region, start_level, 0);
    break;
  case 8:
    CompositeArgb8PalettedCharacterComponentMipRegionAcrossMips(chain, source_texture,
                                                                destination_region, start_level, 0);
    break;
  default:
    assert(false && "Unsupported paletted character component alpha depth");
    return start_level;
  }

  return source_texture.mip_count;
}

bool CompositeSmallPalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, CharacterComponentMipOrigin source_origin) {
  if (!source_texture.has_palette || source_texture.mip_count == 0) {
    return false;
  }

  assert(chain.mip_levels.size() >= static_cast<std::size_t>(source_texture.mip_count) + 1u);

  switch (source_texture.alpha_depth) {
  case 0:
    CompositeSmallOpaquePalettedCharacterComponentMipRegion(chain, source_texture,
                                                            destination_region, source_origin);
    return true;
  case 1:
    CompositeSmallMaskedPalettedCharacterComponentMipRegion(chain, source_texture,
                                                            destination_region);
    return true;
  case 4:
    CompositeSmallArgb4PalettedCharacterComponentMipRegion(chain, source_texture,
                                                           destination_region);
    return true;
  case 8:
    CompositeSmallArgb8PalettedCharacterComponentMipRegion(chain, source_texture,
                                                           destination_region);
    return true;
  default:
    assert(false && "Unsupported small paletted character component alpha depth");
    return false;
  }
}

bool CompositeExtraComponentTextureMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region) {
  if (source_texture.mip_count == 0) {
    return false;
  }

  const CharacterComponentMipOrigin source_origin{0, 0};

  if (source_texture.width >= destination_region.width ||
      source_texture.height >= destination_region.height) {
    std::uint32_t start_level = 0;
    auto w = static_cast<std::uint32_t>(source_texture.width);
    while (w != destination_region.width && w > 0) {
      w >>= 1;
      ++start_level;
    }
    (void)CompositePalettedCharacterComponentMipRegion(chain, source_texture, destination_region,
                                                       source_origin, start_level);
    return true;
  }

  return CompositeSmallPalettedCharacterComponentMipRegion(chain, source_texture,
                                                           destination_region, source_origin);
}

bool CompositeStandardComponentTextureMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, std::uint32_t atlas_edge) {
  if (source_texture.mip_count == 0) {
    return false;
  }

  const CharacterComponentMipOrigin source_origin{destination_region.x, destination_region.y};

  if (source_texture.width >= atlas_edge || source_texture.height >= atlas_edge) {
    std::uint32_t start_level = 0;
    auto w = static_cast<std::uint32_t>(source_texture.width);
    while (w != atlas_edge && w > 0) {
      w >>= 1;
      ++start_level;
    }
    (void)CompositePalettedCharacterComponentMipRegion(chain, source_texture, destination_region,
                                                       source_origin, start_level);
    return true;
  }

  return CompositeSmallPalettedCharacterComponentMipRegion(chain, source_texture,
                                                           destination_region, source_origin);
}

void CompositeComponentRequest(CharacterComponentRequest &request,
                               CharacterComponentMipChain *special_destination) {
  CharacterComponentMipChain *target =
      (request.request_type == 6) ? special_destination : request.output_chain.get();
  if (target == nullptr) {
    return;
  }

  for (std::size_t i = 0; i < request.standard_texture_ids.size(); ++i) {
    if (request.standard_texture_ids[i] != 0) {
      target->standard_passes.emplace_back(i, request.standard_texture_ids[i]);
    }
  }

  const auto extra_count =
      std::min(std::min(request.extra_texture_indices.size(), request.extra_texture_ids.size()),
               kCharacterComponentMaxExtraTextureCount);
  for (std::size_t i = 0; i < extra_count; ++i) {
    if (request.extra_texture_ids[i] == 0) {
      continue;
    }
    target->extra_passes.emplace_back(request.extra_texture_indices[i],
                                      request.extra_texture_ids[i]);
  }

  if (request.request_type == 6) {
    target->special_finalize_called = true;
    target->special_finalize_source_token = request.source_token;
    if (request.output_chain) {
      FinalizeCharacterComponentSpecialMipChainToBc1(*target, *request.output_chain);
    }
  }
}

CharacterComponentWorkerStep
ProcessOneCharacterComponentRequest(CharacterComponentWorkerQueues &queues,
                                    CharacterComponentMipChain *special_destination) {
  if (queues.pending_requests.empty()) {
    return CharacterComponentWorkerStep::Idle;
  }

  auto request = queues.pending_requests.front();
  queues.pending_requests.pop_front();
  if (!request) {
    return CharacterComponentWorkerStep::Idle;
  }

  if ((request->flags & CharacterComponentRequest::kNeedsComposite) == 0) {
    request->flags |= CharacterComponentRequest::kCompleted;
    PushCharacterComponentRequestToHead(queues.completed_requests, std::move(request));
    return CharacterComponentWorkerStep::CompletedWithoutComposite;
  }

  auto mip_chain = AcquireFreeMipChainForRequestType(
      queues.general_free_mip_chains, queues.special_free_mip_chains, request->request_type);
  if (!mip_chain) {
    PushCharacterComponentRequestToHead(queues.pending_requests, std::move(request));
    return CharacterComponentWorkerStep::RequeuedForMipChain;
  }

  ResetCharacterComponentMipChainForReuse(*mip_chain);
  request->output_chain = std::move(mip_chain);
  CompositeComponentRequest(*request, special_destination);
  request->flags |= CharacterComponentRequest::kCompleted;
  PushCharacterComponentRequestToHead(queues.completed_requests, std::move(request));
  return CharacterComponentWorkerStep::CompletedComposite;
}

}

namespace openwow::render {

void ComponentTexture_RegisterCVars() {
  auto &detector = openwow::core::OsSystemInfoDetector::Instance();
  detector.Init();
  const auto *dbc_loader = openwow::data::GetBoundDbcTableRegistryLoader();
  const auto *char_sections = dbc_loader != nullptr ? &dbc_loader->char_sections() : nullptr;
  const auto *facial_hair_styles =
      dbc_loader != nullptr ? &dbc_loader->character_facial_hair_styles() : nullptr;

  (void)openwow::game::RegisterCharacterComponentCVarsAndInitializeBackend(
      openwow::ui::game::CVarSystem::Instance(), detector.GetInfo().processorCount, char_sections,
      facial_hair_styles);
}

void ComponentTexture_Shutdown() {
  (void)openwow::game::ShutdownCharacterComponentBackendRuntimeState();
}

}
