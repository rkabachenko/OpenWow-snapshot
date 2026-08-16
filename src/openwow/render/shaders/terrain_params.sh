
#ifndef OPENWOW_TERRAIN_PARAMS_SH
#define OPENWOW_TERRAIN_PARAMS_SH

#if BGFX_SHADER_TYPE_VERTEX

uniform vec4 u_terrainVsParams[14];

#define u_terrainSunDir          u_terrainVsParams[0]
#define u_terrainLightAmbient    u_terrainVsParams[1]
#define u_terrainLightDiffuse    u_terrainVsParams[2]

#define u_terrainParams          u_terrainVsParams[3]

#define u_terrainPointLightCount u_terrainVsParams[4]
#define terrainPointLightPosition(i)    u_terrainVsParams[5  + (i)]
#define terrainPointLightDiffuse(i)     u_terrainVsParams[8  + (i)]
#define terrainPointLightAttenuation(i) u_terrainVsParams[11 + (i)]

#endif

#if BGFX_SHADER_TYPE_FRAGMENT

uniform vec4 u_terrainFsParams[4];

#define u_terrainFogParams  u_terrainFsParams[0]

#define u_terrainFogColor   u_terrainFsParams[1]

#define u_terrainShadowMod  u_terrainFsParams[2]

#define u_terrainColor      u_terrainFsParams[3]

#endif

#endif
