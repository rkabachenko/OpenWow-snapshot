
uniform vec4 u_m2VertexParams[10];
#define u_uvTransform      u_m2VertexParams[0]
#define u_uvTransformRow1  u_m2VertexParams[1]
#define u_uvTransform1     u_m2VertexParams[2]
#define u_uvTransform1Row1 u_m2VertexParams[3]
#define u_texGenFlags      u_m2VertexParams[4]
#define u_materialColor    u_m2VertexParams[5]
#define u_materialFlags    u_m2VertexParams[6]
#define u_emissiveColor    u_m2VertexParams[7]
#define u_m2LightCount     u_m2VertexParams[8]
#define u_m2LightAmbient   u_m2VertexParams[9]

uniform vec4 u_m2Lights[12];
#define u_m2LightPosRange(i)    u_m2Lights[(i) * 3 + 0]
#define u_m2LightAttenuation(i) u_m2Lights[(i) * 3 + 1]
#define u_m2LightColor(i)       u_m2Lights[(i) * 3 + 2]

#ifndef OPENWOW_M2_BONE_PALETTE_SIZE
#define OPENWOW_M2_BONE_PALETTE_SIZE 256
#endif

#if OPENWOW_M2_BONE_PALETTE_SIZE == 8
uniform vec4 u_boneColumns8[24];
#define OPENWOW_M2_BONE_COLUMNS u_boneColumns8
#elif OPENWOW_M2_BONE_PALETTE_SIZE == 16
uniform vec4 u_boneColumns16[48];
#define OPENWOW_M2_BONE_COLUMNS u_boneColumns16
#elif OPENWOW_M2_BONE_PALETTE_SIZE == 32
uniform vec4 u_boneColumns32[96];
#define OPENWOW_M2_BONE_COLUMNS u_boneColumns32
#elif OPENWOW_M2_BONE_PALETTE_SIZE == 64
uniform vec4 u_boneColumns64[192];
#define OPENWOW_M2_BONE_COLUMNS u_boneColumns64
#elif OPENWOW_M2_BONE_PALETTE_SIZE == 128
uniform vec4 u_boneColumns128[384];
#define OPENWOW_M2_BONE_COLUMNS u_boneColumns128
#elif OPENWOW_M2_BONE_PALETTE_SIZE == 256
uniform vec4 u_boneColumns256[768];
#define OPENWOW_M2_BONE_COLUMNS u_boneColumns256
#else
#error Unsupported OPENWOW_M2_BONE_PALETTE_SIZE
#endif

#if !defined(OPENWOW_M2_VS_LIGHTING)
#define OPENWOW_M2_VS_LIGHTING_ENABLED 1
#define OPENWOW_M2_VS_LIGHTING_GATE if (u_materialFlags.x < 0.5)
#elif OPENWOW_M2_VS_LIGHTING == 1
#define OPENWOW_M2_VS_LIGHTING_ENABLED 1
#define OPENWOW_M2_VS_LIGHTING_GATE
#elif OPENWOW_M2_VS_LIGHTING == 0
#define OPENWOW_M2_VS_LIGHTING_ENABLED 0
#else
#error Unsupported OPENWOW_M2_VS_LIGHTING
#endif

#if !defined(OPENWOW_M2_VS_TEXGEN_ENV)
#define OPENWOW_M2_VS_TEXGEN_ENV_ENABLED 1
#elif OPENWOW_M2_VS_TEXGEN_ENV == 1
#define OPENWOW_M2_VS_TEXGEN_ENV_ENABLED 1
#elif OPENWOW_M2_VS_TEXGEN_ENV == 0
#define OPENWOW_M2_VS_TEXGEN_ENV_ENABLED 0
#else
#error Unsupported OPENWOW_M2_VS_TEXGEN_ENV
#endif

vec3 safeNormalizeM2(vec3 value) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 0.00000023841858
        ? value * (1.0 / sqrt(lengthSquared))
        : vec3_splat(0.0);
}

void main()
{
    M2_MAIN_PROLOGUE

    vec4 localPos = vec4(a_position, 1.0);
    vec3 localNrm = a_normal;

    float totalWeight = a_weight.x + a_weight.y + a_weight.z + a_weight.w;
    if (totalWeight > 0.0) {

        ivec4 idx = ivec4(a_indices * 255.0 + vec4_splat(0.5));
        vec4 column0 = OPENWOW_M2_BONE_COLUMNS[idx.x * 3] * a_weight.x +
                       OPENWOW_M2_BONE_COLUMNS[idx.y * 3] * a_weight.y +
                       OPENWOW_M2_BONE_COLUMNS[idx.z * 3] * a_weight.z +
                       OPENWOW_M2_BONE_COLUMNS[idx.w * 3] * a_weight.w;
        vec4 column1 = OPENWOW_M2_BONE_COLUMNS[idx.x * 3 + 1] * a_weight.x +
                       OPENWOW_M2_BONE_COLUMNS[idx.y * 3 + 1] * a_weight.y +
                       OPENWOW_M2_BONE_COLUMNS[idx.z * 3 + 1] * a_weight.z +
                       OPENWOW_M2_BONE_COLUMNS[idx.w * 3 + 1] * a_weight.w;
        vec4 column2 = OPENWOW_M2_BONE_COLUMNS[idx.x * 3 + 2] * a_weight.x +
                       OPENWOW_M2_BONE_COLUMNS[idx.y * 3 + 2] * a_weight.y +
                       OPENWOW_M2_BONE_COLUMNS[idx.z * 3 + 2] * a_weight.z +
                       OPENWOW_M2_BONE_COLUMNS[idx.w * 3 + 2] * a_weight.w;
        localPos = vec4(dot(localPos, column0), dot(localPos, column1),
                        dot(localPos, column2), 1.0);
        vec4 localNormal4 = vec4(localNrm, 0.0);
        localNrm = vec3(dot(localNormal4, column0), dot(localNormal4, column1),
                        dot(localNormal4, column2));
    }

    gl_Position = M2_MODEL_TO_CLIP(localPos);

#if OPENWOW_M2_VS_LIGHTING_ENABLED
    vec3 worldNormal = safeNormalizeM2(M2_MODEL_TO_WORLD(vec4(localNrm, 0.0)).xyz);
    vec3 worldPosition = M2_MODEL_TO_WORLD(localPos).xyz;
#endif

#if OPENWOW_M2_VS_TEXGEN_ENV_ENABLED
    vec3 viewNormal = safeNormalizeM2(M2_MODEL_TO_VIEW(vec4(localNrm, 0.0)).xyz);
#endif

    if (u_texGenFlags.x < 0.5) {
        vec3 uv = vec3(a_texcoord0, 1.0);
        v_texcoord0 = vec2(dot(uv, u_uvTransform.xyz),
                           dot(uv, u_uvTransformRow1.xyz));
    } else if (u_texGenFlags.x < 1.5) {
        vec3 uv = vec3(a_texcoord1, 1.0);
        v_texcoord0 = vec2(dot(uv, u_uvTransform.xyz),
                           dot(uv, u_uvTransformRow1.xyz));
    }
#if OPENWOW_M2_VS_TEXGEN_ENV_ENABLED
    else {

        vec3 uv = vec3(viewNormal.xy * 0.5 + vec2(0.5, 0.5), 1.0);
        v_texcoord0 = vec2(dot(uv, u_uvTransform.xyz),
                           dot(uv, u_uvTransformRow1.xyz));
    }
#endif

    if (u_texGenFlags.y < 0.5) {
        vec3 uv = vec3(a_texcoord0, 1.0);
        v_texcoord1 = vec2(dot(uv, u_uvTransform1.xyz),
                           dot(uv, u_uvTransform1Row1.xyz));
    } else if (u_texGenFlags.y < 1.5) {
        vec3 uv = vec3(a_texcoord1, 1.0);
        v_texcoord1 = vec2(dot(uv, u_uvTransform1.xyz),
                           dot(uv, u_uvTransform1Row1.xyz));
    }
#if OPENWOW_M2_VS_TEXGEN_ENV_ENABLED
    else {
        vec3 uv = vec3(viewNormal.xy * 0.5 + vec2(0.5, 0.5), 1.0);
        v_texcoord1 = vec2(dot(uv, u_uvTransform1.xyz),
                           dot(uv, u_uvTransform1Row1.xyz));
    }
#endif

#ifndef M2_INSTANCE_COLOR
#define M2_INSTANCE_COLOR vec4(1.0, 1.0, 1.0, 1.0)
#endif
    vec4 materialVertex = a_color0 * u_materialColor * M2_INSTANCE_COLOR;
#if OPENWOW_M2_VS_LIGHTING_ENABLED
    OPENWOW_M2_VS_LIGHTING_GATE
    {
        vec3 lighting = u_m2LightAmbient.rgb;
        int lightCount = int(u_m2LightCount.x + 0.5);
        for (int index = 0; index < 4; ++index) {
            if (index >= lightCount) break;
            vec4 lightColor = u_m2LightColor(index);
            float strength;
            if (lightColor.a > 0.5) {
                vec3 toLight = u_m2LightPosRange(index).xyz - worldPosition;
                float distanceSquared = dot(toLight, toLight);
                float distance = sqrt(max(distanceSquared, 0.0));
                vec3 attenuation = u_m2LightAttenuation(index).xyz;
                float denominator = attenuation.x + attenuation.y * distance
                                  + attenuation.z * distanceSquared;
                strength = clamp(dot(worldNormal, safeNormalizeM2(toLight)), 0.0, 1.0)
                         / max(denominator, 0.0001);
            } else {
                strength = clamp(dot(worldNormal,
                    safeNormalizeM2(u_m2LightPosRange(index).xyz)), 0.0, 1.0);
            }
            lighting += lightColor.rgb * strength;
        }
        materialVertex.rgb *= clamp(lighting, 0.0, 1.0);
    }
#endif
    materialVertex.rgb = clamp(materialVertex.rgb + u_emissiveColor.rgb,
                               0.0, 1.0);
    v_color0 = materialVertex;

    v_viewDist = openwowWorldFogDepth(M2_MODEL_TO_VIEW(localPos).xyz);

}
