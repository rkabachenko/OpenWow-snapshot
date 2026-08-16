#pragma once

#include <cstdint>
#include <span>

namespace openwow::ui {

struct CRect {
    float top = 0.0f;
    float left = 0.0f;
    float bottom = 0.0f;
    float right = 0.0f;
};

struct BFSNode {
    BFSNode* next = nullptr;
    BFSNode* prev = nullptr;
    float rect_top = 0.0f;
    float rect_left = 0.0f;
    float rect_bottom = 0.0f;
    float rect_right = 0.0f;
    int32_t category = -1;
};

struct AnchorGrid {
    uint32_t capacity = 0;
    uint32_t count = 0;
    CRect* rects = nullptr;
    uint32_t grow_size = 0;
};

int ComputeGridCellCount(const CRect* rect);

void* CRect_NTempest_Realloc(void* array_obj, uint32_t new_capacity);

bool FindOverlapShift(int grid_index, const CRect* test_rect,
                      int direction, float* out_amount);

void ShiftRectUp(CRect* out, int grid_index, const CRect* in_rect);

void ShiftRectLeft(CRect* out, int grid_index, const CRect* in_rect);

void ShiftRectRight(CRect* out, int grid_index, const CRect* in_rect);

void ShiftRectDown(CRect* out, int grid_index, const CRect* in_rect);

void ClearAnchorGrids();

void ClearAnchorGrid(int grid_index);

void SetAnchorGridRects(int grid_index, std::span<const CRect> rects);

CRect* AppendAnchorGridRect(int grid_index);

void DestroyAnchorResolverState();

BFSNode* AllocBFSNode();

void ResolveAnchorChain(CRect* out, int grid_index, const CRect* input);
void ResolveAnchorChain(CRect* out, int grid_index, const CRect* input,
                        float viewport_width, float viewport_height);

}
