
#ifndef OPENWOW_DISTANT_TERRAIN_PARAMS_SH
#define OPENWOW_DISTANT_TERRAIN_PARAMS_SH

#if BGFX_SHADER_TYPE_FRAGMENT

uniform vec4 u_distantTerrainFsParams[2];

#define u_distantTerrainFogColor  u_distantTerrainFsParams[0]

#define u_distantTerrainFogParams u_distantTerrainFsParams[1]

#endif

#endif
