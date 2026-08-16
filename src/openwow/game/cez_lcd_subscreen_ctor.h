
#pragma once

#include <cstdint>
#include <cstddef>

namespace openwow::game {

static constexpr std::size_t kCEzLcdSubscreenSize      = 280;
static constexpr int         kCEzLcdSubscreenDwordCount = 70;

static constexpr int kSubscreenFieldOwner     = 47;
static constexpr int kSubscreenFieldDataABase = 48;
static constexpr int kSubscreenFieldDataBBase = 59;
static constexpr int kSubscreenZeroCount      = 11;

void CEzLcdSubscreen_Construct(std::uint32_t* self,
                                std::uint32_t owner) noexcept;

}
