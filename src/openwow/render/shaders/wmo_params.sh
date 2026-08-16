
#ifndef OPENWOW_WMO_PARAMS_SH
#define OPENWOW_WMO_PARAMS_SH

#if BGFX_SHADER_TYPE_VERTEX

uniform vec4 u_wmoVsParams[9];

#define u_wmoWorldCol0      u_wmoVsParams[0]
#define u_wmoWorldCol1      u_wmoVsParams[1]
#define u_wmoWorldCol2      u_wmoVsParams[2]
#define u_wmoSunDir         u_wmoVsParams[3]
#define u_wmoLightAmbient   u_wmoVsParams[4]
#define u_wmoLightDiffuse   u_wmoVsParams[5]
#define u_wmoEmissiveColor  u_wmoVsParams[6]

#define u_wmoMaterialParams u_wmoVsParams[7]

#define u_wmoExtraParams    u_wmoVsParams[8]

#define wmoWorldNormalMtx() \
    mtxFromCols3(u_wmoWorldCol0.xyz, u_wmoWorldCol1.xyz, u_wmoWorldCol2.xyz)

#endif

#if BGFX_SHADER_TYPE_FRAGMENT

uniform vec4 u_wmoFsParams[6];

#define u_wmoGroupColor     u_wmoFsParams[0]
#define u_wmoFogParams      u_wmoFsParams[1]
#define u_wmoFogColor       u_wmoFsParams[2]
#define u_wmoSunDir         u_wmoFsParams[3]

#define u_wmoMaterialParams u_wmoFsParams[4]

#define u_wmoExtraParams    u_wmoFsParams[5]

#endif

#endif
