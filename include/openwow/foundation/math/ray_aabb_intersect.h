#pragma once

#include <cmath>

namespace openwow::math {

inline bool RayAABBIntersect(const float origin[3],
                             const float dir[3],
                             const float boxMin[3],
                             const float boxMax[3],
                             float* outT,
                             float outPoint[3]) {

    enum Quadrant : unsigned char { RIGHT = 0, LEFT = 1, MIDDLE = 2 };

    Quadrant quadrant[3];
    float    candidatePlane[3];
    bool     inside = true;

    for (int i = 0; i < 3; ++i) {
        if (boxMin[i] > origin[i]) {

            quadrant[i]       = LEFT;
            candidatePlane[i] = boxMin[i];
            inside            = false;
        } else if (boxMax[i] < origin[i]) {

            quadrant[i]       = RIGHT;
            candidatePlane[i] = boxMax[i];
            inside            = false;
        } else {

            quadrant[i] = MIDDLE;
        }
    }

    if (inside) {
        *outT       = 0.0f;
        outPoint[0] = origin[0];
        outPoint[1] = origin[1];
        outPoint[2] = origin[2];
        return true;
    }

    float maxT[3];
    for (int i = 0; i < 3; ++i) {
        if (quadrant[i] != MIDDLE && dir[i] != 0.0f) {
            maxT[i] = (candidatePlane[i] - origin[i]) / dir[i];
        } else {
            maxT[i] = -1.0f;
        }
    }

    int whichPlane = 0;
    if (maxT[1] > maxT[0])
        whichPlane = 1;
    if (maxT[2] > maxT[whichPlane])
        whichPlane = 2;

    if (maxT[whichPlane] < 0.0f)
        return false;

    *outT = maxT[whichPlane];
    for (int i = 0; i < 3; ++i) {
        if (whichPlane == i) {
            outPoint[i] = candidatePlane[i];
        } else {
            outPoint[i] = origin[i] + dir[i] * maxT[whichPlane];
            if (outPoint[i] < boxMin[i] || outPoint[i] > boxMax[i])
                return false;
        }
    }

    return true;
}

inline bool RayIntersectAABB(const float ray[6],
                             const float aabb[6],
                             float* outT,
                             float outPoint[3]) {
    float localT     = 0.0f;
    float localPt[3] = {0.0f, 0.0f, 0.0f};

    float* tPtr  = outT     ? outT     : &localT;
    float* ptPtr = outPoint ? outPoint : localPt;

    return RayAABBIntersect(ray, ray + 3, aabb, aabb + 3, tPtr, ptPtr);
}

}
