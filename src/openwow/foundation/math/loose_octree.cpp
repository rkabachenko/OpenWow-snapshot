
#include "openwow/foundation/math/loose_octree.h"

#include <algorithm>
#include <cstring>

namespace openwow::math {

uint32_t LooseOctreeNode::QPos(uint32_t axis) const {

    switch (axis) {
        case 0: return qx;
        case 1: return qy;
        case 2: return qz;
        default: return qx;
    }
}

uint32_t CLooseOctree::NextPow2(uint32_t v) {
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v & ~(v >> 1);
}

void CLooseOctree::MergeAABB(const LooseOctreeAABB& a,
                              const LooseOctreeAABB& b,
                              LooseOctreeAABB& dst) {
    dst.minX = (b.minX < a.minX) ? b.minX : a.minX;
    dst.maxX = (b.maxX > a.maxX) ? b.maxX : a.maxX;
    dst.minY = (b.minY < a.minY) ? b.minY : a.minY;
    dst.maxY = (b.maxY > a.maxY) ? b.maxY : a.maxY;
    dst.minZ = (b.minZ < a.minZ) ? b.minZ : a.minZ;
    dst.maxZ = (b.maxZ > a.maxZ) ? b.maxZ : a.maxZ;
}

void CLooseOctree::ComputeTreeBounds(LooseOctreeAABB& out) const {
    if (!root_) {
        out = {0, 0, 0, 0, 0, 0};
        return;
    }
    out = root_->bounds;
    for (auto* n = root_->next; n; n = n->next)
        MergeAABB(*reinterpret_cast<const LooseOctreeAABB*>(&n->bounds),
                  out, out);
}

void CLooseOctree::UpdateBounds(LooseOctreeNode* node) {
    for (auto* cur = node; cur; cur = cur->parent) {
        if (cur->flags & LooseOctreeFlags::kInternal) {

            continue;
        }

        MergeAABB(cur->left->bounds, cur->right->bounds, cur->bounds);

        for (auto* lf = cur->next; lf; lf = lf->next)
            MergeAABB(lf->bounds, cur->bounds, cur->bounds);

        cur->flags |= 0x08;

        if (cur->left->flags & LooseOctreeFlags::kInternal) {
            for (auto* lf = cur->left->next; lf; lf = lf->next)
                MergeAABB(lf->bounds, cur->bounds, cur->bounds);
        }

        if (cur->right->flags & LooseOctreeFlags::kInternal) {
            for (auto* lf = cur->right->next; lf; lf = lf->next)
                MergeAABB(lf->bounds, cur->bounds, cur->bounds);
        }
    }
}

LooseOctreeNode* CLooseOctree::AllocBranch() {
    auto* node = freeList_;
    if (!node) return nullptr;
    freeList_ = node->next;
    if (freeList_)
        freeList_->parent = nullptr;
    node->next = nullptr;
    return node;
}

void CLooseOctree::FreeBranch(LooseOctreeNode* node) {
    node->flags &= ~0x0Bu;
    node->next   = nullptr;
    node->parent = nullptr;
    node->left   = nullptr;
    node->right  = nullptr;

    node->next   = freeList_;
    freeList_    = node;
    if (node->next)
        node->next->parent = node;
    node->flags |= LooseOctreeFlags::kInFreeList;
    node->parent = nullptr;
}

void CLooseOctree::InsertLeafChain(LooseOctreeNode* target,
                                    LooseOctreeNode* leaf) {
    auto* cur = target;

    if (!(target->flags & LooseOctreeFlags::kInternal)) {
        if (!target->next) {
            target->next = leaf;
            leaf->flags |= LooseOctreeFlags::kHasParentLink;
            leaf->parent = target;
            return;
        }
        cur = target->next;
    }

    while (leaf->level > cur->level && cur->next)
        cur = cur->next;

    if (cur->next || leaf->level <= cur->level) {

        auto* prev = cur->parent;
        if (prev) {
            if (prev->next == cur) {
                prev->next = leaf;
                if ((cur->flags & LooseOctreeFlags::kInternal) &&
                    (prev->flags & LooseOctreeFlags::kInternal))
                    leaf->flags |= LooseOctreeFlags::kHasParentLink;
            } else if (prev->left == cur) {
                prev->left = leaf;
            } else {
                prev->right = leaf;
            }
        } else {
            root_ = leaf;
        }
        leaf->parent = cur->parent;
        leaf->next   = cur;
        cur->flags  |= LooseOctreeFlags::kHasParentLink;
        cur->parent  = leaf;
    } else {

        cur->next = leaf;
        leaf->flags |= LooseOctreeFlags::kHasParentLink;
        leaf->parent = cur;
    }
}

void CLooseOctree::InsertInternal(LooseOctreeNode* branch,
                                   LooseOctreeNode* leaf) {
    while (true) {
        uint32_t splitBit = 0;
        int      splitAxis = 0;

        auto findSplit = [&](uint32_t diff, int axis, uint32_t mask) {
            uint32_t msb = NextPow2(diff & mask);
            if (msb > splitBit && msb > leaf->level && msb > branch->level) {
                splitBit  = msb;
                splitAxis = axis;
            }
        };

        if (branch->flags & LooseOctreeFlags::kInternal) {

            findSplit(branch->qx ^ leaf->qx, 0, ~0u);
            findSplit(branch->qy ^ leaf->qy, 1, ~0u);
            findSplit(branch->qz ^ leaf->qz, 2, ~0u);
        } else {

            uint32_t levelMask = ~(branch->level - 1);
            findSplit(levelMask & (branch->qx ^ leaf->qx), 0, ~0u);

            uint32_t msbY = NextPow2(levelMask & (branch->qy ^ leaf->qy));
            if (msbY > splitBit && msbY > leaf->level) {
                uint32_t brLevel = branch->level;
                if (msbY > brLevel ||
                    (msbY == brLevel && (branch->flags & LooseOctreeFlags::kAxisMask) > 1)) {
                    splitBit  = msbY;
                    splitAxis = 1;
                }
            }

            uint32_t msbZ = NextPow2(levelMask & (branch->qz ^ leaf->qz));
            if (msbZ > splitBit && msbZ > leaf->level) {
                uint32_t brLevel = branch->level;
                if (msbZ > brLevel ||
                    (msbZ == brLevel && (branch->flags & LooseOctreeFlags::kAxisMask) == 3)) {
                    splitBit  = msbZ;
                    splitAxis = 2;
                }
            }
        }

        if (splitBit == 0) {
            if (leaf->level >= branch->level) {
                InsertLeafChain(branch, leaf);
                UpdateBounds(branch);
                return;
            }
            if (branch->flags & LooseOctreeFlags::kInternal) {
                auto* oldParent = branch->parent;
                InsertLeafChain(branch, leaf);
                UpdateBounds(oldParent);
                return;
            }

            uint32_t axis = branch->flags & LooseOctreeFlags::kAxisMask;
            if (leaf->QPos(axis) < branch->QPos(axis))
                branch = branch->right;
            else
                branch = branch->left;
            continue;
        }

        auto* newBranch = AllocBranch();
        if (!newBranch) return;

        newBranch->next  = nullptr;
        newBranch->flags = (newBranch->flags & ~LooseOctreeFlags::kInFreeList) | splitAxis;
        newBranch->level = splitBit;

        if (splitBit & leaf->QPos(splitAxis)) {
            newBranch->right = branch;
            newBranch->left  = leaf;
        } else {
            newBranch->left  = branch;
            newBranch->right = leaf;
        }

        newBranch->parent = branch->parent;
        newBranch->left->parent  = newBranch;
        newBranch->right->parent = newBranch;

        if (auto* gp = newBranch->parent) {
            if (gp->right == branch)
                gp->right = newBranch;
            else
                gp->left = newBranch;
        } else {
            root_ = newBranch;
        }

        uint32_t mask = ~(splitBit - 1);
        switch (splitAxis) {
            case 0:
                newBranch->qx = splitBit | (mask & leaf->qx);
                newBranch->qy = splitBit | (mask & leaf->qy);
                newBranch->qz = splitBit | (mask & leaf->qz);
                break;
            case 1:
                newBranch->qx = (splitBit >> 1) | (leaf->qx & ~((splitBit >> 1) - 1));
                newBranch->qy = splitBit | (mask & leaf->qy);
                newBranch->qz = splitBit | (mask & leaf->qz);
                break;
            case 2: {
                uint32_t half = splitBit >> 1;
                uint32_t hMask = ~(half - 1);
                newBranch->qx = half | (hMask & leaf->qx);
                newBranch->qy = half | (hMask & leaf->qy);
                newBranch->qz = splitBit | (leaf->qz & mask);
                break;
            }
        }

        UpdateBounds(newBranch);

        auto* chain = branch->next;
        branch->next = nullptr;
        if (chain) {
            for (auto* cur = chain; cur; ) {
                auto* nx = cur->next;
                cur->flags  &= ~LooseOctreeFlags::kHasParentLink;
                cur->parent  = nullptr;
                cur->next    = nullptr;
                if (newBranch->parent)
                    InsertInternal(newBranch->parent, cur);
                else
                    InsertInternal(root_, cur);
                cur = nx;
            }
        }
        return;
    }
}

void CLooseOctree::InsertNode(LooseOctreeNode* node) {
    if (node->flags & LooseOctreeFlags::kInTree)
        return;

    node->flags |= (LooseOctreeFlags::kInTree | LooseOctreeFlags::kInternal);

    float dx = node->bounds.maxX - node->bounds.minX;
    float dy = node->bounds.maxY - node->bounds.minY;
    float dz = node->bounds.maxZ - node->bounds.minZ;
    float maxExtent = dx;
    if (dy > maxExtent) maxExtent = dy;
    if (dz > maxExtent) maxExtent = dz;

    uint32_t qSize = static_cast<uint32_t>(maxExtent * invSize_ * kQuantScale);
    node->level = NextPow2(qSize);

    float cx = (node->bounds.minX + node->bounds.maxX) * 0.5f;
    float cy = (node->bounds.minY + node->bounds.maxY) * 0.5f;
    float cz = (node->bounds.minZ + node->bounds.maxZ) * 0.5f;

    node->qx = static_cast<uint32_t>((cx - centerX_) * invSize_ * kQuantScale + kQuantScale);
    node->qy = static_cast<uint32_t>((cy - centerY_) * invSize_ * kQuantScale + kQuantScale);
    node->qz = static_cast<uint32_t>((cz - centerZ_) * invSize_ * kQuantScale + kQuantScale);

    if (root_)
        InsertInternal(root_, node);
    else
        root_ = node;
}

void CLooseOctree::CollapseBranch(LooseOctreeNode* branchParent,
                                   LooseOctreeNode* remaining,
                                   LooseOctreeNode* removedNode) {

    auto* grandparent = branchParent->parent;
    LooseOctreeNode* anchorForReinsert = nullptr;

    if (grandparent) {
        if (grandparent->left == branchParent)
            grandparent->left = remaining;
        else
            grandparent->right = remaining;
        remaining->parent = grandparent;
        anchorForReinsert = grandparent;
    } else {
        root_ = remaining;
        remaining->parent = nullptr;
        anchorForReinsert = root_;
    }

    auto* chain = branchParent->next;
    if (chain) {
        auto* cur = chain;
        do {
            auto* nx = cur->next;
            cur->flags  &= ~LooseOctreeFlags::kHasParentLink;
            cur->next    = nullptr;
            cur->parent  = nullptr;
            InsertInternal(anchorForReinsert, cur);
            cur = nx;
        } while (cur);
    }

    if (branchParent->parent)
        UpdateBounds(branchParent->parent);

    branchParent->flags  &= ~0x0Bu;
    branchParent->next    = nullptr;
    branchParent->parent  = nullptr;
    branchParent->left    = nullptr;
    branchParent->right   = nullptr;

    branchParent->next    = freeList_;
    freeList_             = branchParent;
    if (branchParent->next)
        branchParent->next->parent = branchParent;
    branchParent->flags  |= LooseOctreeFlags::kInFreeList;
    branchParent->parent  = nullptr;

    removedNode->flags &= ~(LooseOctreeFlags::kInTree | LooseOctreeFlags::kInFreeList);
    removedNode->parent = nullptr;
    removedNode->next   = nullptr;
}

void CLooseOctree::RemoveNode(LooseOctreeNode* node) {
    if (!(node->flags & LooseOctreeFlags::kInTree))
        return;

    auto* par = node->parent;

    if (!par) {
        auto* successor = node->next;

        root_ = successor;

        if (successor) {
            successor->parent = nullptr;

            successor->flags &= ~LooseOctreeFlags::kHasParentLink;

            node->flags &= ~(LooseOctreeFlags::kInTree | LooseOctreeFlags::kInFreeList);
            node->next   = nullptr;
            return;

        }

        node->flags &= ~(LooseOctreeFlags::kInTree | LooseOctreeFlags::kInFreeList);
        node->next   = nullptr;
        return;
    }

    auto* nodeNext = node->next;

    if (par->next == node) {
        par->next = nodeNext;
        if (nodeNext)
            nodeNext->parent = node->parent;

        goto unlinked;
    }

    if (par->left == node) {
        par->left = nodeNext;
        if (nodeNext) {
            nodeNext->flags &= ~LooseOctreeFlags::kHasParentLink;

            nodeNext->parent = node->parent;

            goto unlinked;
        }

        {
            auto* remaining = par->right;

            if (!remaining)
                goto unlinked;
            CollapseBranch(par, remaining, node);
            return;
        }
    }

    {
        par->right = nodeNext;
        if (nodeNext) {
            nodeNext->flags &= ~LooseOctreeFlags::kHasParentLink;

            nodeNext->parent = node->parent;

            goto unlinked;
        }

        {
            auto* remaining = par->left;

            if (!remaining)
                goto unlinked;
            CollapseBranch(par, remaining, node);
            return;
        }
    }

unlinked:

    if (node->parent && !(node->parent->flags & LooseOctreeFlags::kInternal))
        UpdateBounds(node->parent);

    node->parent = nullptr;
    node->flags &= ~(LooseOctreeFlags::kInTree | LooseOctreeFlags::kInFreeList);
    node->next   = nullptr;
}

void CLooseOctree::UpdateNode(LooseOctreeNode* node) {
    if (!(node->flags & LooseOctreeFlags::kInTree)) {
        InsertNode(node);
        return;
    }

    float dx = node->bounds.maxX - node->bounds.minX;
    float dy = node->bounds.maxY - node->bounds.minY;
    float dz = node->bounds.maxZ - node->bounds.minZ;
    float maxExt = dx;
    if (dy > maxExt) maxExt = dy;
    if (dz > maxExt) maxExt = dz;

    uint32_t newSize = static_cast<uint32_t>(maxExt * invSize_ * kQuantScale);
    uint32_t newLevel = NextPow2(newSize);

    float cx = (node->bounds.minX + node->bounds.maxX) * 0.5f;
    float cy = (node->bounds.minY + node->bounds.maxY) * 0.5f;
    float cz = (node->bounds.minZ + node->bounds.maxZ) * 0.5f;

    uint32_t newQx = static_cast<uint32_t>((cx - centerX_) * invSize_ * kQuantScale + kQuantScale);
    uint32_t newQy = static_cast<uint32_t>((cy - centerY_) * invSize_ * kQuantScale + kQuantScale);
    uint32_t newQz = static_cast<uint32_t>((cz - centerZ_) * invSize_ * kQuantScale + kQuantScale);

    uint32_t levelMask = ~(node->level - 1);
    if (newLevel == node->level &&
        (levelMask & newQx) == (levelMask & node->qx) &&
        (levelMask & newQy) == (levelMask & node->qy) &&
        (levelMask & newQz) == (levelMask & node->qz)) {

        UpdateBounds(node);
        return;
    }

    RemoveNode(node);
    InsertNode(node);
}

}
