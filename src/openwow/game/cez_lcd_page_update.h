
#pragma once

#include <cstdint>

namespace openwow::game {

struct CEzLcdPageBitmapRequest {
    std::int32_t data0{0};
    std::int32_t data1{0};
    std::int32_t data2{0};
    std::int32_t data3{0};
    std::int32_t handle{-1};
};

static_assert(sizeof(CEzLcdPageBitmapRequest) == 20,
              "CEzLcdPageBitmapRequest must be 20 bytes (5 DWORDs)");

int CEzLcdPage_SetActiveContent(void* page, std::int32_t content_data,
                                int activate);

int CEzLcdPage_FlushPendingBitmap(void* page);

int CEzLcdPage_ApplyBitmapUpdate(void* page,
                                  const CEzLcdPageBitmapRequest* request);

bool CEzLcdPage_IsActive(const void* page);

bool CEzLcdPage_HasPendingBitmap(const void* page);

void CEzLcdPage_ReleaseCurrent(void* page);

void CEzLcdPage_SetGfxObject(void* page, void* gfx_object);

void CEzLcdPage_SetDeviceType(void* page, std::int32_t device_type);

void CEzLcdPage_Destroy(void* page);

std::int32_t CEzLcdPage_GetDisplayHandle(const void* page);

}
