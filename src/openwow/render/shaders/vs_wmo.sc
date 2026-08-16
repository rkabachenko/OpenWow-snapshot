$input a_position, a_normal, a_texcoord0, a_color0, a_texcoord1, a_color1
$output v_texcoord0, v_normal, v_color0, v_viewDist, v_envTexcoord, v_texcoord1, v_color1

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "wmo_params.sh"

void main()
{
    vec4 localPosition = vec4(a_position, 1.0);
    vec4 viewPosition = mul(u_modelView, localPosition);
    vec3 worldNormal = normalize(mul(wmoWorldNormalMtx(), a_normal));
    vec3 viewNormal = normalize(mul(u_modelView, vec4(a_normal, 0.0)).xyz);

    gl_Position = mul(u_modelViewProj, localPosition);
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_color1 = a_color1;
    v_normal = worldNormal;
    v_viewDist = openwowWorldFogDepth(viewPosition.xyz);

    vec3 incident = normalize(viewPosition.xyz);
    v_envTexcoord = reflect(incident, viewNormal).xy;

    bool passthroughColor = u_wmoMaterialParams.y > 0.5
                         || u_wmoMaterialParams.x > 0.5;
    if (passthroughColor) {

        v_color0 = a_color0;
    } else {
        float ndl = clamp(dot(worldNormal, normalize(u_wmoSunDir.xyz)), 0.0, 1.0);
        vec3 lighting = clamp(u_wmoLightAmbient.rgb
                            + u_wmoLightDiffuse.rgb * ndl, 0.0, 1.0);

        vec3 litColor;
        if (u_wmoExtraParams.x > 0.5) {

            litColor = lighting * (127.0 / 255.0) + a_color0.rgb;
        } else {

            litColor = a_color0.rgb * lighting;
        }
        v_color0 = vec4(clamp(litColor + u_wmoEmissiveColor.rgb, 0.0, 1.0),
                        a_color0.a);
    }
}
