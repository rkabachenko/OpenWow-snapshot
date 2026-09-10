#ifndef OPENWOW_WORLD_SHADOW_SH
#define OPENWOW_WORLD_SHADOW_SH

SAMPLER2DSHADOW(s_worldShadow0, 5);
SAMPLER2DSHADOW(s_worldShadow1, 6);
SAMPLER2DSHADOW(s_worldShadow2, 7);
SAMPLER2DSHADOW(s_worldShadow3, 8);

uniform mat4 u_worldShadowMtx[4];
uniform vec4 u_worldShadowParams[3];

vec4 openwowShadowCoordinate(mat4 shadowMatrix, vec3 worldPosition)
{
    vec4 shadowPosition = mul(shadowMatrix, vec4(worldPosition, 1.0));
    vec3 projected = shadowPosition.xyz / shadowPosition.w;
    bool inside = !any(greaterThan(projected, vec3_splat(1.0)))
               && !any(lessThan(projected, vec3_splat(0.0)));
    return vec4(projected, inside ? 1.0 : 0.0);
}

float openwowSampleWorldShadow(vec3 worldPosition)
{
    if (u_worldShadowParams[0].z < 0.5) {
        return 1.0;
    }

    int productCount = int(u_worldShadowParams[0].x + 0.5);
    int firstProduct = int(u_worldShadowParams[0].y + 0.5);
    vec4 coordinate = openwowShadowCoordinate(
        u_worldShadowMtx[0], worldPosition);
    if (firstProduct <= 0 && productCount > 0 && coordinate.w > 0.5) {
        return shadow2D(s_worldShadow0,
                        vec3(coordinate.xy, coordinate.z - u_worldShadowParams[1].x));
    }
    coordinate = openwowShadowCoordinate(u_worldShadowMtx[1], worldPosition);
    if (firstProduct <= 1 && productCount > 1 && coordinate.w > 0.5) {
        return shadow2D(s_worldShadow1,
                        vec3(coordinate.xy, coordinate.z - u_worldShadowParams[1].y));
    }
    coordinate = openwowShadowCoordinate(u_worldShadowMtx[2], worldPosition);
    if (firstProduct <= 2 && productCount > 2 && coordinate.w > 0.5) {
        return shadow2D(s_worldShadow2,
                        vec3(coordinate.xy, coordinate.z - u_worldShadowParams[1].z));
    }
    coordinate = openwowShadowCoordinate(u_worldShadowMtx[3], worldPosition);
    if (firstProduct <= 3 && productCount > 3 && coordinate.w > 0.5) {
        return shadow2D(s_worldShadow3,
                        vec3(coordinate.xy, coordinate.z - u_worldShadowParams[1].w));
    }
    return 1.0;
}

vec3 openwowApplyWorldShadow(vec3 color, vec3 worldPosition)
{
    float visibility = openwowSampleWorldShadow(worldPosition);
    vec3 modulation = mix(u_worldShadowParams[2].rgb,
                          vec3_splat(1.0), visibility);
    return color * modulation;
}

#endif
