
#ifndef OPENWOW_M2_VERTEX_PERDRAW_SH
#define OPENWOW_M2_VERTEX_PERDRAW_SH

uniform mat4 u_m2WorldMtx;

#define M2_MAIN_PROLOGUE
#define M2_MODEL_TO_CLIP(p)  mul(u_modelViewProj, p)
#define M2_MODEL_TO_WORLD(p) mul(u_m2WorldMtx, p)
#define M2_MODEL_TO_VIEW(p)  mul(u_modelView, p)

#include "vs_m2_main.sh"

#endif
