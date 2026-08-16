
#include <bgfx_shader.sh>
#include "world_fog.sh"

SAMPLER2D(s_m2Tex0,  0);
SAMPLER2D(s_m2Tex1, 1);

uniform vec4 u_m2FragmentParams[5];
#define u_combinerMode   u_m2FragmentParams[0]
#define u_m2AlphaRef     u_m2FragmentParams[1]
#define u_materialFlags  u_m2FragmentParams[2]
#define u_fogParams      u_m2FragmentParams[3]
#define u_fogColor       u_m2FragmentParams[4]

#if defined(OPENWOW_M2_FS_TEXTURE_COUNT) || defined(OPENWOW_M2_FS_OP1) || \
    defined(OPENWOW_M2_FS_OP2) || defined(OPENWOW_M2_FS_SPECIAL)
#  if !defined(OPENWOW_M2_FS_TEXTURE_COUNT) || !defined(OPENWOW_M2_FS_OP1) || \
      !defined(OPENWOW_M2_FS_OP2) || !defined(OPENWOW_M2_FS_SPECIAL)
#    error M2 fragment permutation requires all four combiner defines
#  endif
#  define OPENWOW_M2_FS_SPECIALIZED 1
#else
#  define OPENWOW_M2_FS_SPECIALIZED 0
#endif

vec3 combineM2Color(int op, vec3 currentRgb, vec3 textureRgb,
                    float currentAlpha, float textureAlpha)
{

    if (op == 2) {

        return mix(textureRgb, currentRgb, currentAlpha);
    } else if (op == 3 || op == 7) {
        return currentRgb + textureRgb;
    } else if (op == 4 || op == 6) {
        return currentRgb * textureRgb * 2.0;
    } else if (op == 5) {
        return mix(currentRgb, textureRgb, currentAlpha);
    }
    return currentRgb * textureRgb;
}

float combineM2Alpha(int op, float currentAlpha, float textureAlpha)
{

    if (op == 1) {
        return currentAlpha * textureAlpha;
    } else if (op == 3) {
        return currentAlpha + textureAlpha;
    } else if (op == 4) {
        return currentAlpha * textureAlpha * 2.0;
    }
    return currentAlpha;
}

vec4 combineM2Special(int variant, vec4 vertexColor, vec4 tex0, vec4 tex1)
{
    vec4 base = vec4(vertexColor.rgb * tex0.rgb, vertexColor.a);

    if (variant == 1) {
        return vec4(base.rgb * tex1.rgb * 2.0, base.a * tex0.a);
    } else if (variant == 2) {
        return vec4(base.rgb + tex1.rgb * tex1.a, base.a);
    } else if (variant == 3) {
        return vec4(base.rgb + tex1.rgb * tex1.a, base.a * tex0.a);
    }

    return base;
}

void main()
{

    vec4 vc = v_color0;

    vec4 result;
#if OPENWOW_M2_FS_SPECIALIZED

#  if OPENWOW_M2_FS_SPECIAL > 0
    vec4 tex0 = texture2D(s_m2Tex0,  v_texcoord0);
    vec4 tex1 = texture2D(s_m2Tex1, v_texcoord1);
    result = combineM2Special(OPENWOW_M2_FS_SPECIAL, vc, tex0, tex1);
#  elif OPENWOW_M2_FS_TEXTURE_COUNT <= 0
    result = vc;
#  elif OPENWOW_M2_FS_TEXTURE_COUNT <= 1

    vec4 tex0 = texture2D(s_m2Tex0,  v_texcoord0);
    result = vec4(combineM2Color(OPENWOW_M2_FS_OP1, vc.rgb, tex0.rgb, vc.a, tex0.a),
                  combineM2Alpha(OPENWOW_M2_FS_OP1, vc.a, tex0.a));
#  else

    vec4 tex0 = texture2D(s_m2Tex0,  v_texcoord0);
    vec4 tex1 = texture2D(s_m2Tex1, v_texcoord1);

    vec4 layer0 = vec4(combineM2Color(OPENWOW_M2_FS_OP1, vc.rgb, tex0.rgb, vc.a, tex0.a),
                       combineM2Alpha(OPENWOW_M2_FS_OP1, vc.a, tex0.a));

    result = vec4(combineM2Color(OPENWOW_M2_FS_OP2, layer0.rgb, tex1.rgb, layer0.a, tex1.a),
                  combineM2Alpha(OPENWOW_M2_FS_OP2, layer0.a, tex1.a));
#  endif
#else
    int op1 = int(u_combinerMode.x + 0.5);
    int op2 = int(u_combinerMode.y + 0.5);
    int tc  = int(u_combinerMode.z + 0.5);
    int specialVariant = int(u_combinerMode.w + 0.5);

    vec4 tex0 = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tex1 = vec4(1.0, 1.0, 1.0, 1.0);
    if (tc > 0 || specialVariant > 0) {
        tex0 = texture2D(s_m2Tex0,  v_texcoord0);
    }
    if (tc > 1 || specialVariant > 0) {
        tex1 = texture2D(s_m2Tex1, v_texcoord1);
    }

    if (tc <= 0 && specialVariant <= 0) {
        result = vc;
    } else if (specialVariant > 0) {
        result = combineM2Special(specialVariant, vc, tex0, tex1);
    } else if (tc <= 1) {

        result = vec4(combineM2Color(op1, vc.rgb, tex0.rgb, vc.a, tex0.a),
                      combineM2Alpha(op1, vc.a, tex0.a));
    } else {

        vec4 layer0 = vec4(combineM2Color(op1, vc.rgb, tex0.rgb, vc.a, tex0.a),
                           combineM2Alpha(op1, vc.a, tex0.a));

        result = vec4(combineM2Color(op2, layer0.rgb, tex1.rgb, layer0.a, tex1.a),
                      combineM2Alpha(op2, layer0.a, tex1.a));
    }
#endif

    if (result.a < u_m2AlphaRef.x) {
        discard;
    }

    if (u_materialFlags.y < 0.5) {
        float fogFactor = openwowLinearFogVisibility(u_fogParams, v_viewDist);
        result.rgb = mix(u_fogColor.rgb, result.rgb, fogFactor);
    }

    gl_FragColor = clamp(result, 0.0, 1.0);
}
