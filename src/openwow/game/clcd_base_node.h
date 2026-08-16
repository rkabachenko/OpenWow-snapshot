
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::game {

static constexpr std::size_t kCLCDBaseNodeSize = 76;

static constexpr std::size_t kChildListNodeSize = 12;

void CLCDBASE_NODE_Construct(std::uint32_t* self) noexcept;

void CLCDBASE_NODE_Destruct(std::uint32_t* self) noexcept;

}
