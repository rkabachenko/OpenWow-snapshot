
#pragma once

#include <cstdint>

namespace openwow::game {

constexpr int kPageNodeFieldStartTime = 19;
constexpr int kPageNodeFieldElapsed   = 20;
constexpr int kPageNodeFieldDuration  = 21;

bool CEzLcdPageNode_IsShowTimeExpired(const void* node) noexcept;

std::uint32_t CEzLcdPageNode_StartShowTimer(void* node,
                                             std::uint32_t duration_ms) noexcept;

int CEzLcdPageNode_UpdateElapsed(void* node,
                                  std::uint32_t current_time) noexcept;

void CEzLcdPageNode_Destruct(void* node) noexcept;

void* CEzLcdPageNode_ScalarDeletingDtor(void* node,
                                         bool free_memory) noexcept;

}
