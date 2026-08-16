$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_nameplateFont, 0);

uniform vec4 u_nameplateParams;

void main()
{
    float mode = u_nameplateParams.x;

    if (mode < 0.5)
    {

        vec4 tex = texture2D(s_nameplateFont, v_texcoord0);

        float alpha = tex.a * v_color0.a;
        if (alpha < 0.05) discard;
        gl_FragColor = vec4(v_color0.rgb, alpha);
    }
    else
    {

        float fill    = u_nameplateParams.y;
        float border  = u_nameplateParams.z;
        float u       = v_texcoord0.x;

        if (v_texcoord0.x < border || v_texcoord0.x > 1.0 - border ||
            v_texcoord0.y < border || v_texcoord0.y > 1.0 - border)
        {
            gl_FragColor = vec4(0.0, 0.0, 0.0, 0.9);
            return;
        }

        if (u <= fill)
        {
            gl_FragColor = v_color0;
        }
        else
        {
            gl_FragColor = vec4(0.15, 0.15, 0.15, 0.8);
        }
    }
}
