
#include "storm_comp.h"

#include <limits>

namespace openwow::core {

SCompPool& SCompPool::Instance() {
    static SCompPool inst;
    return inst;
}

void* SCompPool::Alloc(uint32_t elem_size, uint32_t count, void* context) {
    if (elem_size != 0 &&
        count > std::numeric_limits<uint32_t>::max() / elem_size) {
        return nullptr;
    }
    const uint32_t unaligned_size = elem_size * count;
    if (unaligned_size > std::numeric_limits<uint32_t>::max() - 3u) {
        return nullptr;
    }
    const uint32_t size = (unaligned_size + 3u) & ~uint32_t{3u};

    if (context) {
        auto* ctx = static_cast<SCompArenaContext*>(context);
        const uintptr_t base = reinterpret_cast<uintptr_t>(ctx->base);
        const uintptr_t current = reinterpret_cast<uintptr_t>(ctx->current);
        if (ctx->base != nullptr && ctx->current != nullptr &&
            current >= base && current - base <= ctx->total_size) {
            const uint32_t remaining =
                ctx->total_size - static_cast<uint32_t>(current - base);

            if (size < remaining) {
                void* result = ctx->current;
                ctx->current += size;
                return result;
            }
        }
    }

    return std::malloc(size);
}

void SCompPool::Free(void* ptr, void* context) {
    if (context && ptr) {
        auto* ctx = static_cast<SCompArenaContext*>(context);
        auto addr = reinterpret_cast<uintptr_t>(ptr);
        auto base = reinterpret_cast<uintptr_t>(ctx->base);
        if (addr >= base && addr - base < ctx->total_size) {
            return;
        }
    }
    std::free(ptr);
}

int SCompPool::Shutdown() {
    if (pool_buffer_) {
        std::free(pool_buffer_);
        pool_buffer_ = nullptr;
    }
    return 1;
}

}
