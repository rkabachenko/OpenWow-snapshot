
#ifndef OPENWOW_M2_VERTEX_INSTANCED_SH
#define OPENWOW_M2_VERTEX_INSTANCED_SH

#define M2_MAIN_PROLOGUE \
    mat4 instanceModel = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
#define M2_INSTANCE_COLOR i_data4
#define M2_MODEL_TO_CLIP(p)  mul(u_viewProj, mul(instanceModel, p))
#define M2_MODEL_TO_WORLD(p) mul(instanceModel, p)
#define M2_MODEL_TO_VIEW(p)  mul(u_view, mul(instanceModel, p))

#include "vs_m2_main.sh"

#endif
