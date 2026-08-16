
#pragma once

#include <cstdint>

namespace openwow::game {

static constexpr std::uint32_t kCLCDBaseDefaultBackgroundColor = 0x00FFFFFF;
static constexpr int kCLCDBaseDefaultAlignment = 1;
static constexpr int kCLCDBaseDefaultVisibleMode = 4;
static constexpr int kCLCDBaseDefaultShowFlag = 1;

static constexpr std::size_t kCLCDBaseSize = 60;

inline std::uint32_t CLCDBase_SetBackgroundColor(std::uint32_t* self,
                                                  std::uint32_t color) noexcept {
    self[14] = color;
    return color;
}

inline std::uint32_t CLCDBase_SetAlignment(std::uint32_t* self,
                                            std::uint32_t align) noexcept {
    self[11] = align;
    return align;
}

inline std::uint32_t CLCDBase_SetVisible(std::uint32_t* self,
                                          std::uint32_t mode) noexcept {
    self[12] = mode;
    return mode;
}

inline std::uint32_t CLCDBase_SetPosition(std::uint32_t* self,
                                           std::uint32_t x,
                                           std::uint32_t y) noexcept {
    self[9] = x;
    self[10] = y;
    return x;
}

inline std::uint32_t CLCDBase_SetLogicalOrigin(std::uint32_t* self,
                                                std::uint32_t x,
                                                std::uint32_t y) noexcept {
    self[7] = x;
    self[8] = y;
    return x;
}

inline std::uint32_t CLCDBase_SetShow(std::uint32_t* self,
                                       std::uint32_t flag) noexcept {
    self[5] = flag;
    return flag;
}

}
