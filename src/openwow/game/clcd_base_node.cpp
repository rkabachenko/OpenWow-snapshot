
#include "clcd_base_node.h"

#include <cstring>

namespace openwow::game {

static void CLCDBase_Construct(std::uint32_t* self) noexcept {

    self[1]  = 0;
    self[2]  = 0;
    self[3]  = 0;
    self[4]  = 0;
    self[5]  = 1;
    self[6]  = 0;
    self[7]  = 0;
    self[8]  = 0;
    self[9]  = 0;
    self[10] = 0;
    self[11] = 1;
    self[12] = 4;
    self[13] = 0;
    self[14] = 0x00FFFFFF;
}

void CLCDBASE_NODE_Construct(std::uint32_t* self) noexcept {

    CLCDBase_Construct(self);

    self[15] = 0;
    self[16] = 0;
    auto sentinel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&self[17]));
    self[17] = sentinel;
    self[18] = sentinel | 1u;
}

void CLCDBASE_NODE_Destruct(std::uint32_t* self) noexcept {

    self[16] = 0;
    auto sentinel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&self[17]));
    self[17] = sentinel;
    self[18] = sentinel | 1u;
}

}
