$input a_position, a_normal, a_texcoord0, a_texcoord1, a_color0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_texcoord0, v_texcoord1, v_color0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "m2_vertex_instanced.sh"
