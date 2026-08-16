$input v_texcoord0, v_normal, v_color0, v_viewDist, v_envTexcoord, v_texcoord1, v_color1

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "wmo_params.sh"

SAMPLER2D(s_diffuse, 0);
SAMPLER2D(s_envMap, 1);

void main()
{
    vec4 base = texture2D(s_diffuse, v_texcoord0);
    vec4 overlay = texture2D(s_envMap, v_texcoord1);

    vec3 composite = mix(base.rgb, overlay.rgb, v_color0.a);
    vec3 color = (2.0 * v_color0.rgb * composite + v_color1.rgb)
               * u_wmoGroupColor.rgb;
    float alpha = v_color0.a * u_wmoGroupColor.a;

    if (u_wmoExtraParams.w > 0.5 && alpha < (128.0 / 255.0)) {
        discard;
    }
    if (u_wmoMaterialParams.w < 0.5) {
        float fogFactor = openwowLinearFogVisibility(u_wmoFogParams, v_viewDist);
        color = mix(u_wmoFogColor.rgb, color, fogFactor);
    }
    gl_FragColor = vec4(color, alpha);
}
