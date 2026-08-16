$input v_texcoord0, v_normal, v_color0, v_viewDist, v_envTexcoord, v_texcoord1, v_color1

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "wmo_params.sh"

SAMPLER2D(s_diffuse, 0);

void main()
{
    vec4 tex = texture2D(s_diffuse, v_texcoord0);

    vec3 color = (2.0 * v_color0.rgb * tex.rgb)
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
