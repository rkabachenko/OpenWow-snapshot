$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_celestialParams;

void main()
{
    vec4 result;

    if (u_celestialParams.x > 0.5)
    {

        vec2 uv = v_texcoord0 * 2.0 - 1.0;
        float dist = dot(uv, uv);

        float alpha = smoothstep(1.0, 0.2, dist) * v_color0.a;

        float phase = u_celestialParams.y;
        float cutX = (phase - 0.5) * 2.0;

        float phaseCut = 1.0;
        if (phase < 0.5)
        {

            if (uv.x > cutX) phaseCut = 0.0;
        }
        else
        {

            if (uv.x < cutX) phaseCut = 0.0;
        }

        float terminator = 1.0 - smoothstep(0.0, 0.05, abs(uv.x - cutX));
        phaseCut = max(phaseCut, terminator * 0.15);

        alpha *= phaseCut;

        result = vec4(v_color0.rgb, alpha);
    }
    else
    {

        result = texture2D(s_texColor, v_texcoord0) * v_color0;
    }

    if (result.a < 1.0/255.0) discard;
    gl_FragColor = result;
}
