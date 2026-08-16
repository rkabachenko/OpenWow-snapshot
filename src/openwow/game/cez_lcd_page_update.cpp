
#include "openwow/game/cez_lcd_page_update.h"

#include "openwow/core/storm_string.h"

#include <cstdint>
#include <cstring>

namespace openwow::game {

namespace {

int lgLcd_ReadSoftButtons([[maybe_unused]] const void* data) {

    return 1062;
}

int lgLcd_UpdateBitmap([[maybe_unused]] std::int32_t handle) {
    return 1062;
}

int lgLcd_SetPriority([[maybe_unused]] std::int32_t handle,
                       [[maybe_unused]] std::int32_t priority) {
    return 1062;
}

struct PageFields {
    std::int32_t* AsIntArray() {
        return reinterpret_cast<std::int32_t*>(this);
    }
    const std::int32_t* AsIntArray() const {
        return reinterpret_cast<const std::int32_t*>(this);
    }
};

inline std::int32_t& PageField(void* page, int index) {
    return reinterpret_cast<std::int32_t*>(page)[index];
}

inline std::int32_t PageField(const void* page, int index) {
    return reinterpret_cast<const std::int32_t*>(page)[index];
}

using VtableEntry = void*;

inline VtableEntry* GetVtable(void* page) {
    return *reinterpret_cast<VtableEntry**>(page);
}

inline int CallVtable29_Update(void* page, std::uint32_t tick_count) {
    using Fn = int(*)(void*, std::uint32_t);
    auto fn = reinterpret_cast<Fn>(GetVtable(page)[29]);
    return fn(page, tick_count);
}

inline void CallVtable33_Deactivate(void* page, std::int32_t arg) {
    using Fn = void(*)(void*, std::int32_t);
    auto fn = reinterpret_cast<Fn>(GetVtable(page)[33]);
    fn(page, arg);
}

inline int CallVtable38_OnContentActivated(void* page,
                                            std::int32_t content_data) {
    using Fn = int(*)(void*, std::int32_t);
    auto fn = reinterpret_cast<Fn>(GetVtable(page)[38]);
    return fn(page, content_data);
}

inline void CallVtable41_OnBitmapReleased(void* page, std::int32_t handle) {
    using Fn = void(*)(void*, std::int32_t);
    auto fn = reinterpret_cast<Fn>(GetVtable(page)[41]);
    fn(page, handle);
}

inline void CallVtable42_OnBitmapActivated(void* page, std::int32_t handle) {
    using Fn = void(*)(void*, std::int32_t);
    auto fn = reinterpret_cast<Fn>(GetVtable(page)[42]);
    fn(page, handle);
}

constexpr int kFieldActiveContent   = 19;
constexpr int kFieldDeviceType      = 20;
constexpr int kFieldCurrentHandle   = 21;
constexpr int kFieldHandle           = kFieldCurrentHandle;
constexpr int kFieldDisplayPriority = 22;
constexpr int kFieldStateFlags      = 23;
constexpr int kFieldGfxObject       = 25;
constexpr int kFieldPendingData0    = 26;
constexpr int kFieldPendingData1    = 27;
constexpr int kFieldPendingData2    = 28;
constexpr int kFieldPendingData3    = 29;
constexpr int kFieldPendingData4    = 30;

constexpr std::int32_t kInvalidHandle = -1;

}

int CEzLcdPage_SetActiveContent(void* page, std::int32_t content_data,
                                int activate) {
    if (activate) {
        std::int32_t handle   = PageField(page, kFieldCurrentHandle);
        std::int32_t priority = PageField(page, kFieldDisplayPriority);
        PageField(page, kFieldActiveContent) = content_data;

        if (handle != kInvalidHandle) {
            lgLcd_SetPriority(handle, priority);
        }

        return CallVtable38_OnContentActivated(page, content_data);
    } else {
        auto* content_obj = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(
                static_cast<std::uint32_t>(content_data)));
        CallVtable33_Deactivate(content_obj, 0);

        constexpr std::uint32_t kStubTickCount = 0;
        return CallVtable29_Update(page, kStubTickCount);
    }
}

int CEzLcdPage_FlushPendingBitmap(void* page) {
    if (PageField(page, kFieldPendingData1) != 0) {
        auto* request = reinterpret_cast<const CEzLcdPageBitmapRequest*>(
            &PageField(page, kFieldPendingData0));
        return CEzLcdPage_ApplyBitmapUpdate(page, request);
    }
    return 0;
}

int CEzLcdPage_ApplyBitmapUpdate(void* page,
                                  const CEzLcdPageBitmapRequest* request) {
    if (PageField(page, kFieldCurrentHandle) != kInvalidHandle) {
        CallVtable41_OnBitmapReleased(page,
                                       PageField(page, kFieldCurrentHandle));
        lgLcd_UpdateBitmap(PageField(page, kFieldCurrentHandle));
        PageField(page, kFieldCurrentHandle) = kInvalidHandle;
    }

    if (lgLcd_ReadSoftButtons(request) != 0) {
        return 0;
    }

    std::int32_t new_handle = request->handle;
    std::int32_t priority = PageField(page, kFieldDisplayPriority);

    PageField(page, kFieldCurrentHandle) = new_handle;
    PageField(page, kFieldStateFlags) = 0;

    if (new_handle != kInvalidHandle) {
        lgLcd_SetPriority(new_handle, priority);
    }

    PageField(page, kFieldPendingData0) = request->data0;
    PageField(page, kFieldPendingData1) = request->data1;
    PageField(page, kFieldPendingData2) = request->data2;
    PageField(page, kFieldPendingData3) = request->data3;
    PageField(page, kFieldPendingData4) = request->handle;

    CallVtable42_OnBitmapActivated(page,
                                    PageField(page, kFieldCurrentHandle));

    return 1;
}

bool CEzLcdPage_IsActive(const void* page) {
    return PageField(page, kFieldCurrentHandle) != kInvalidHandle;
}

bool CEzLcdPage_HasPendingBitmap(const void* page) {
    return PageField(page, kFieldPendingData1) != 0;
}

void CEzLcdPage_ReleaseCurrent(void* page) {
    if (PageField(page, kFieldCurrentHandle) != kInvalidHandle) {
        CallVtable41_OnBitmapReleased(page,
                                       PageField(page, kFieldCurrentHandle));
        lgLcd_UpdateBitmap(PageField(page, kFieldCurrentHandle));
        PageField(page, kFieldCurrentHandle) = kInvalidHandle;
    }
}

void CEzLcdPage_SetGfxObject(void* page, void* gfx_object) {
    reinterpret_cast<void**>(page)[kFieldGfxObject] = gfx_object;
}

void CEzLcdPage_SetDeviceType(void* page, std::int32_t device_type) {
    PageField(page, kFieldDeviceType) = device_type;
}

void CEzLcdPage_Destroy(void* page) {
    if (!page) {
        return;
    }

    auto* dwords = static_cast<std::uint32_t*>(page);

    const auto handle = static_cast<std::int32_t>(dwords[kFieldHandle]);
    if (handle != -1) {

        dwords[kFieldHandle] = static_cast<std::uint32_t>(-1);
    }

    core::SMemFree(page, "delete", -1, 0);
}

std::int32_t CEzLcdPage_GetDisplayHandle(const void* page) {
    return PageField(page, kFieldCurrentHandle);
}

}
