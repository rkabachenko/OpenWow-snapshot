
#ifndef OPENWOW_LIQUID_PARAMS_SH
#define OPENWOW_LIQUID_PARAMS_SH

#ifndef OPENWOW_LIQUID_PROCEDURAL
#define OPENWOW_LIQUID_PROCEDURAL 0
#endif

#if BGFX_SHADER_TYPE_VERTEX

#if OPENWOW_LIQUID_PROCEDURAL

uniform vec4 u_liquidProcVsParams[13];

#define u_liquidWavePhases                u_liquidProcVsParams[0]
#define u_liquidWaveInverseFalloff        u_liquidProcVsParams[1]
#define liquidWaveRecord(i)               u_liquidProcVsParams[2 + (i)]
#define u_liquidWaveInverseSpatialPeriod  u_liquidProcVsParams[5]
#define u_liquidWaveAmplitudes            u_liquidProcVsParams[6]

#define liquidProceduralUvTransform(i)    u_liquidProcVsParams[7 + (i)]

#else

uniform vec4 u_liquidVsParams[16];

#define u_liquidAmbient       u_liquidVsParams[0]
#define u_liquidDiffuse       u_liquidVsParams[1]

#define u_liquidSpecular      u_liquidVsParams[2]
#define u_liquidLightDir      u_liquidVsParams[3]
#define u_liquidCamera        u_liquidVsParams[4]

#define u_liquidUvTransform0  u_liquidVsParams[5]

#define u_liquidUvTransform1  u_liquidVsParams[6]

#define liquidPointLightPosition(i)     u_liquidVsParams[7  + (i)]
#define liquidPointLightDiffuse(i)      u_liquidVsParams[10 + (i)]
#define liquidPointLightAttenuation(i)  u_liquidVsParams[13 + (i)]

#endif

#endif

#if BGFX_SHADER_TYPE_FRAGMENT

#if OPENWOW_LIQUID_PROCEDURAL

uniform vec4 u_liquidProcFsParams[7];

#define u_liquidFogParams      u_liquidProcFsParams[0]

#define u_liquidFogColor       u_liquidProcFsParams[1]

#define u_liquidSpecular       u_liquidProcFsParams[2]

#define u_liquidLightRay       u_liquidProcFsParams[3]
#define u_liquidCamera         u_liquidProcFsParams[4]

#define u_liquidMaterialParams(i)  u_liquidProcFsParams[5 + (i)]

#else

uniform vec4 u_liquidFsParams[2];

#define u_liquidFogParams  u_liquidFsParams[0]

#define u_liquidFogColor   u_liquidFsParams[1]

#endif

#endif

#endif
