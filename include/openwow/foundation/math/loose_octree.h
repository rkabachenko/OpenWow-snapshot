#pragma once

#include <cstdint>
#include <cmath>

namespace openwow::math {

namespace LooseOctreeFlags {
    constexpr uint32_t kAxisMask       = 0x03;
    constexpr uint32_t kInternal       = 0x04;
    constexpr uint32_t kHasParentLink  = 0x10;
    constexpr uint32_t kInFreeList     = 0x20;
    constexpr uint32_t kInTree         = 0x40;
    constexpr uint32_t kPendingRemove  = 0x400;
}

struct LooseOctreeAABB {
    float minX, maxX;
    float minY, maxY;
    float minZ, maxZ;
};

struct LooseOctreeNode {
    LooseOctreeAABB bounds;
    uint32_t  flags;
    uint32_t  level;
    uint32_t  qx;
    uint32_t  qy;
    uint32_t  qz;
    LooseOctreeNode* parent;
    LooseOctreeNode* left;
    LooseOctreeNode* right;
    LooseOctreeNode* next;

    [[nodiscard]] bool IsInTree()    const { return (flags & LooseOctreeFlags::kInTree) != 0; }
    [[nodiscard]] bool IsInternal()  const { return (flags & LooseOctreeFlags::kInternal) != 0; }
    [[nodiscard]] bool IsInFreeList()const { return (flags & LooseOctreeFlags::kInFreeList) != 0; }
    [[nodiscard]] uint32_t Axis()    const { return flags & LooseOctreeFlags::kAxisMask; }
    [[nodiscard]] uint32_t QPos(uint32_t axis) const;
};

class CLooseOctree {
public:
    CLooseOctree() = default;

    void InsertNode(LooseOctreeNode* node);

    void RemoveNode(LooseOctreeNode* node);

    void UpdateNode(LooseOctreeNode* node);

    static void MergeAABB(const LooseOctreeAABB& a,
                          const LooseOctreeAABB& b,
                          LooseOctreeAABB& dst);

    void ComputeTreeBounds(LooseOctreeAABB& outBounds) const;

    [[nodiscard]] LooseOctreeNode* Root()     const { return root_; }
    [[nodiscard]] LooseOctreeNode* FreeList() const { return freeList_; }

    float CenterX()  const { return centerX_; }
    float CenterY()  const { return centerY_; }
    float CenterZ()  const { return centerZ_; }
    float InvSize()  const { return invSize_; }

    void SetCenter(float x, float y, float z) { centerX_ = x; centerY_ = y; centerZ_ = z; }
    void SetInvSize(float s) { invSize_ = s; }

private:

    static constexpr float kQuantScale = 1073741800.0f;

    static uint32_t NextPow2(uint32_t v);

    void InsertInternal(LooseOctreeNode* branch, LooseOctreeNode* leaf);

    static void UpdateBounds(LooseOctreeNode* node);

    void InsertLeafChain(LooseOctreeNode* target, LooseOctreeNode* leaf);

    LooseOctreeNode* AllocBranch();

    void FreeBranch(LooseOctreeNode* node);

    void CollapseBranch(LooseOctreeNode* parentNode,
                        LooseOctreeNode* remaining,
                        LooseOctreeNode* removedNode);

    LooseOctreeNode* root_     = nullptr;
    float            centerX_  = 0.0f;
    float            centerY_  = 0.0f;
    float            centerZ_  = 0.0f;
    float            invSize_  = 1.0f;
    LooseOctreeNode* freeList_ = nullptr;
};

}
