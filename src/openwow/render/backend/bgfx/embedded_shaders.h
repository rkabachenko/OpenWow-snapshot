
#pragma once

#include <bgfx/embedded_shader.h>

#define _OPENWOW_INCLUDE_SHADER_BINS(_name)  \
    _Pragma("once")

#include "vs_ui.sc.glsl.bin.h"
#include "vs_ui.sc.essl.bin.h"
#include "vs_ui.sc.spv.bin.h"
#include "vs_ui.sc.mtl.bin.h"
#include "fs_ui.sc.glsl.bin.h"
#include "fs_ui.sc.essl.bin.h"
#include "fs_ui.sc.spv.bin.h"
#include "fs_ui.sc.mtl.bin.h"
#include "fs_ui_material.sc.glsl.bin.h"
#include "fs_ui_material.sc.essl.bin.h"
#include "fs_ui_material.sc.spv.bin.h"
#include "fs_ui_material.sc.mtl.bin.h"
#include "fs_world_text.sc.glsl.bin.h"
#include "fs_world_text.sc.essl.bin.h"
#include "fs_world_text.sc.spv.bin.h"
#include "fs_world_text.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_ui.sc.dx11.bin.h"
#include "fs_ui.sc.dx11.bin.h"
#include "fs_ui_material.sc.dx11.bin.h"
#include "fs_world_text.sc.dx11.bin.h"
#endif

#include "vs_m2_bones_8.sc.glsl.bin.h"
#include "vs_m2_bones_8.sc.essl.bin.h"
#include "vs_m2_bones_8.sc.spv.bin.h"
#include "vs_m2_bones_8.sc.mtl.bin.h"
#include "vs_m2_bones_8_lit_env.sc.glsl.bin.h"
#include "vs_m2_bones_8_lit_env.sc.essl.bin.h"
#include "vs_m2_bones_8_lit_env.sc.spv.bin.h"
#include "vs_m2_bones_8_lit_env.sc.mtl.bin.h"
#include "vs_m2_bones_8_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_8_lit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_8_lit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_8_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_8_unlit_env.sc.glsl.bin.h"
#include "vs_m2_bones_8_unlit_env.sc.essl.bin.h"
#include "vs_m2_bones_8_unlit_env.sc.spv.bin.h"
#include "vs_m2_bones_8_unlit_env.sc.mtl.bin.h"
#include "vs_m2_bones_8_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_8_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_8_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_8_unlit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_16.sc.glsl.bin.h"
#include "vs_m2_bones_16.sc.essl.bin.h"
#include "vs_m2_bones_16.sc.spv.bin.h"
#include "vs_m2_bones_16.sc.mtl.bin.h"
#include "vs_m2_bones_16_lit_env.sc.glsl.bin.h"
#include "vs_m2_bones_16_lit_env.sc.essl.bin.h"
#include "vs_m2_bones_16_lit_env.sc.spv.bin.h"
#include "vs_m2_bones_16_lit_env.sc.mtl.bin.h"
#include "vs_m2_bones_16_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_16_lit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_16_lit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_16_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_16_unlit_env.sc.glsl.bin.h"
#include "vs_m2_bones_16_unlit_env.sc.essl.bin.h"
#include "vs_m2_bones_16_unlit_env.sc.spv.bin.h"
#include "vs_m2_bones_16_unlit_env.sc.mtl.bin.h"
#include "vs_m2_bones_16_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_16_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_16_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_16_unlit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_32.sc.glsl.bin.h"
#include "vs_m2_bones_32.sc.essl.bin.h"
#include "vs_m2_bones_32.sc.spv.bin.h"
#include "vs_m2_bones_32.sc.mtl.bin.h"
#include "vs_m2_bones_32_lit_env.sc.glsl.bin.h"
#include "vs_m2_bones_32_lit_env.sc.essl.bin.h"
#include "vs_m2_bones_32_lit_env.sc.spv.bin.h"
#include "vs_m2_bones_32_lit_env.sc.mtl.bin.h"
#include "vs_m2_bones_32_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_32_lit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_32_lit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_32_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_32_unlit_env.sc.glsl.bin.h"
#include "vs_m2_bones_32_unlit_env.sc.essl.bin.h"
#include "vs_m2_bones_32_unlit_env.sc.spv.bin.h"
#include "vs_m2_bones_32_unlit_env.sc.mtl.bin.h"
#include "vs_m2_bones_32_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_32_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_32_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_32_unlit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_64.sc.glsl.bin.h"
#include "vs_m2_bones_64.sc.essl.bin.h"
#include "vs_m2_bones_64.sc.spv.bin.h"
#include "vs_m2_bones_64.sc.mtl.bin.h"
#include "vs_m2_bones_64_lit_env.sc.glsl.bin.h"
#include "vs_m2_bones_64_lit_env.sc.essl.bin.h"
#include "vs_m2_bones_64_lit_env.sc.spv.bin.h"
#include "vs_m2_bones_64_lit_env.sc.mtl.bin.h"
#include "vs_m2_bones_64_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_64_lit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_64_lit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_64_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_64_unlit_env.sc.glsl.bin.h"
#include "vs_m2_bones_64_unlit_env.sc.essl.bin.h"
#include "vs_m2_bones_64_unlit_env.sc.spv.bin.h"
#include "vs_m2_bones_64_unlit_env.sc.mtl.bin.h"
#include "vs_m2_bones_64_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_64_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_64_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_64_unlit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_128.sc.glsl.bin.h"
#include "vs_m2_bones_128.sc.essl.bin.h"
#include "vs_m2_bones_128.sc.spv.bin.h"
#include "vs_m2_bones_128.sc.mtl.bin.h"
#include "vs_m2_bones_128_lit_env.sc.glsl.bin.h"
#include "vs_m2_bones_128_lit_env.sc.essl.bin.h"
#include "vs_m2_bones_128_lit_env.sc.spv.bin.h"
#include "vs_m2_bones_128_lit_env.sc.mtl.bin.h"
#include "vs_m2_bones_128_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_128_lit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_128_lit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_128_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_128_unlit_env.sc.glsl.bin.h"
#include "vs_m2_bones_128_unlit_env.sc.essl.bin.h"
#include "vs_m2_bones_128_unlit_env.sc.spv.bin.h"
#include "vs_m2_bones_128_unlit_env.sc.mtl.bin.h"
#include "vs_m2_bones_128_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_128_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_128_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_128_unlit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_256.sc.glsl.bin.h"
#include "vs_m2_bones_256.sc.essl.bin.h"
#include "vs_m2_bones_256.sc.spv.bin.h"
#include "vs_m2_bones_256.sc.mtl.bin.h"
#include "vs_m2_bones_256_lit_env.sc.glsl.bin.h"
#include "vs_m2_bones_256_lit_env.sc.essl.bin.h"
#include "vs_m2_bones_256_lit_env.sc.spv.bin.h"
#include "vs_m2_bones_256_lit_env.sc.mtl.bin.h"
#include "vs_m2_bones_256_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_256_lit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_256_lit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_256_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_bones_256_unlit_env.sc.glsl.bin.h"
#include "vs_m2_bones_256_unlit_env.sc.essl.bin.h"
#include "vs_m2_bones_256_unlit_env.sc.spv.bin.h"
#include "vs_m2_bones_256_unlit_env.sc.mtl.bin.h"
#include "vs_m2_bones_256_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_bones_256_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_bones_256_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_bones_256_unlit_noenv.sc.mtl.bin.h"
#include "vs_m2_instanced.sc.glsl.bin.h"
#include "vs_m2_instanced.sc.essl.bin.h"
#include "vs_m2_instanced.sc.spv.bin.h"
#include "vs_m2_instanced.sc.mtl.bin.h"
#include "vs_m2_instanced_lit_env.sc.glsl.bin.h"
#include "vs_m2_instanced_lit_env.sc.essl.bin.h"
#include "vs_m2_instanced_lit_env.sc.spv.bin.h"
#include "vs_m2_instanced_lit_env.sc.mtl.bin.h"
#include "vs_m2_instanced_lit_noenv.sc.glsl.bin.h"
#include "vs_m2_instanced_lit_noenv.sc.essl.bin.h"
#include "vs_m2_instanced_lit_noenv.sc.spv.bin.h"
#include "vs_m2_instanced_lit_noenv.sc.mtl.bin.h"
#include "vs_m2_instanced_unlit_env.sc.glsl.bin.h"
#include "vs_m2_instanced_unlit_env.sc.essl.bin.h"
#include "vs_m2_instanced_unlit_env.sc.spv.bin.h"
#include "vs_m2_instanced_unlit_env.sc.mtl.bin.h"
#include "vs_m2_instanced_unlit_noenv.sc.glsl.bin.h"
#include "vs_m2_instanced_unlit_noenv.sc.essl.bin.h"
#include "vs_m2_instanced_unlit_noenv.sc.spv.bin.h"
#include "vs_m2_instanced_unlit_noenv.sc.mtl.bin.h"
#include "fs_m2.sc.glsl.bin.h"
#include "fs_m2.sc.essl.bin.h"
#include "fs_m2.sc.spv.bin.h"
#include "fs_m2.sc.mtl.bin.h"
#include "fs_m2_t1_op0.sc.glsl.bin.h"
#include "fs_m2_t1_op0.sc.essl.bin.h"
#include "fs_m2_t1_op0.sc.spv.bin.h"
#include "fs_m2_t1_op0.sc.mtl.bin.h"
#include "fs_m2_t1_op1.sc.glsl.bin.h"
#include "fs_m2_t1_op1.sc.essl.bin.h"
#include "fs_m2_t1_op1.sc.spv.bin.h"
#include "fs_m2_t1_op1.sc.mtl.bin.h"
#include "fs_m2_t2_op0_op6.sc.glsl.bin.h"
#include "fs_m2_t2_op0_op6.sc.essl.bin.h"
#include "fs_m2_t2_op0_op6.sc.spv.bin.h"
#include "fs_m2_t2_op0_op6.sc.mtl.bin.h"
#include "fs_m2_t2_op1_op0.sc.glsl.bin.h"
#include "fs_m2_t2_op1_op0.sc.essl.bin.h"
#include "fs_m2_t2_op1_op0.sc.spv.bin.h"
#include "fs_m2_t2_op1_op0.sc.mtl.bin.h"
#include "fs_m2_t2_op1_op1.sc.glsl.bin.h"
#include "fs_m2_t2_op1_op1.sc.essl.bin.h"
#include "fs_m2_t2_op1_op1.sc.spv.bin.h"
#include "fs_m2_t2_op1_op1.sc.mtl.bin.h"
#include "fs_m2_t2_op1_op4.sc.glsl.bin.h"
#include "fs_m2_t2_op1_op4.sc.essl.bin.h"
#include "fs_m2_t2_op1_op4.sc.spv.bin.h"
#include "fs_m2_t2_op1_op4.sc.mtl.bin.h"
#include "fs_m2_special1.sc.glsl.bin.h"
#include "fs_m2_special1.sc.essl.bin.h"
#include "fs_m2_special1.sc.spv.bin.h"
#include "fs_m2_special1.sc.mtl.bin.h"
#include "fs_m2_special2.sc.glsl.bin.h"
#include "fs_m2_special2.sc.essl.bin.h"
#include "fs_m2_special2.sc.spv.bin.h"
#include "fs_m2_special2.sc.mtl.bin.h"
#include "fs_m2_special3.sc.glsl.bin.h"
#include "fs_m2_special3.sc.essl.bin.h"
#include "fs_m2_special3.sc.spv.bin.h"
#include "fs_m2_special3.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_m2_bones_8.sc.dx11.bin.h"
#include "vs_m2_bones_8_lit_env.sc.dx11.bin.h"
#include "vs_m2_bones_8_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_8_unlit_env.sc.dx11.bin.h"
#include "vs_m2_bones_8_unlit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_16.sc.dx11.bin.h"
#include "vs_m2_bones_16_lit_env.sc.dx11.bin.h"
#include "vs_m2_bones_16_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_16_unlit_env.sc.dx11.bin.h"
#include "vs_m2_bones_16_unlit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_32.sc.dx11.bin.h"
#include "vs_m2_bones_32_lit_env.sc.dx11.bin.h"
#include "vs_m2_bones_32_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_32_unlit_env.sc.dx11.bin.h"
#include "vs_m2_bones_32_unlit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_64.sc.dx11.bin.h"
#include "vs_m2_bones_64_lit_env.sc.dx11.bin.h"
#include "vs_m2_bones_64_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_64_unlit_env.sc.dx11.bin.h"
#include "vs_m2_bones_64_unlit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_128.sc.dx11.bin.h"
#include "vs_m2_bones_128_lit_env.sc.dx11.bin.h"
#include "vs_m2_bones_128_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_128_unlit_env.sc.dx11.bin.h"
#include "vs_m2_bones_128_unlit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_256.sc.dx11.bin.h"
#include "vs_m2_bones_256_lit_env.sc.dx11.bin.h"
#include "vs_m2_bones_256_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_bones_256_unlit_env.sc.dx11.bin.h"
#include "vs_m2_bones_256_unlit_noenv.sc.dx11.bin.h"
#include "vs_m2_instanced.sc.dx11.bin.h"
#include "vs_m2_instanced_lit_env.sc.dx11.bin.h"
#include "vs_m2_instanced_lit_noenv.sc.dx11.bin.h"
#include "vs_m2_instanced_unlit_env.sc.dx11.bin.h"
#include "vs_m2_instanced_unlit_noenv.sc.dx11.bin.h"
#include "fs_m2.sc.dx11.bin.h"
#include "fs_m2_t1_op0.sc.dx11.bin.h"
#include "fs_m2_t1_op1.sc.dx11.bin.h"
#include "fs_m2_t2_op0_op6.sc.dx11.bin.h"
#include "fs_m2_t2_op1_op0.sc.dx11.bin.h"
#include "fs_m2_t2_op1_op1.sc.dx11.bin.h"
#include "fs_m2_t2_op1_op4.sc.dx11.bin.h"
#include "fs_m2_special1.sc.dx11.bin.h"
#include "fs_m2_special2.sc.dx11.bin.h"
#include "fs_m2_special3.sc.dx11.bin.h"
#endif

#include "vs_particle.sc.glsl.bin.h"
#include "vs_particle.sc.essl.bin.h"
#include "vs_particle.sc.spv.bin.h"
#include "vs_particle.sc.mtl.bin.h"
#include "fs_particle.sc.glsl.bin.h"
#include "fs_particle.sc.essl.bin.h"
#include "fs_particle.sc.spv.bin.h"
#include "fs_particle.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_particle.sc.dx11.bin.h"
#include "fs_particle.sc.dx11.bin.h"
#endif

#include "vs_sky.sc.glsl.bin.h"
#include "vs_sky.sc.essl.bin.h"
#include "vs_sky.sc.spv.bin.h"
#include "vs_sky.sc.mtl.bin.h"
#include "fs_sky.sc.glsl.bin.h"
#include "fs_sky.sc.essl.bin.h"
#include "fs_sky.sc.spv.bin.h"
#include "fs_sky.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_sky.sc.dx11.bin.h"
#include "fs_sky.sc.dx11.bin.h"
#endif

#include "vs_terrain.sc.glsl.bin.h"
#include "vs_terrain.sc.essl.bin.h"
#include "vs_terrain.sc.spv.bin.h"
#include "vs_terrain.sc.mtl.bin.h"
#include "fs_terrain.sc.glsl.bin.h"
#include "fs_terrain.sc.essl.bin.h"
#include "fs_terrain.sc.spv.bin.h"
#include "fs_terrain.sc.mtl.bin.h"
#include "vs_distant_terrain.sc.glsl.bin.h"
#include "vs_distant_terrain.sc.essl.bin.h"
#include "vs_distant_terrain.sc.spv.bin.h"
#include "vs_distant_terrain.sc.mtl.bin.h"
#include "fs_distant_terrain.sc.glsl.bin.h"
#include "fs_distant_terrain.sc.essl.bin.h"
#include "fs_distant_terrain.sc.spv.bin.h"
#include "fs_distant_terrain.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_terrain.sc.dx11.bin.h"
#include "fs_terrain.sc.dx11.bin.h"
#include "vs_distant_terrain.sc.dx11.bin.h"
#include "fs_distant_terrain.sc.dx11.bin.h"
#endif

#include "vs_terrain_splat.sc.glsl.bin.h"
#include "vs_terrain_splat.sc.essl.bin.h"
#include "vs_terrain_splat.sc.spv.bin.h"
#include "vs_terrain_splat.sc.mtl.bin.h"
#include "fs_terrain_splat.sc.glsl.bin.h"
#include "fs_terrain_splat.sc.essl.bin.h"
#include "fs_terrain_splat.sc.spv.bin.h"
#include "fs_terrain_splat.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_terrain_splat.sc.dx11.bin.h"
#include "fs_terrain_splat.sc.dx11.bin.h"
#endif

#include "vs_terrain_splat_array.sc.glsl.bin.h"
#include "vs_terrain_splat_array.sc.essl.bin.h"
#include "vs_terrain_splat_array.sc.spv.bin.h"
#include "vs_terrain_splat_array.sc.mtl.bin.h"
#include "fs_terrain_splat_array.sc.glsl.bin.h"
#include "fs_terrain_splat_array.sc.essl.bin.h"
#include "fs_terrain_splat_array.sc.spv.bin.h"
#include "fs_terrain_splat_array.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_terrain_splat_array.sc.dx11.bin.h"
#include "fs_terrain_splat_array.sc.dx11.bin.h"
#endif

#include "vs_liquid_water_0.sc.glsl.bin.h"
#include "vs_liquid_water_0.sc.essl.bin.h"
#include "vs_liquid_water_0.sc.spv.bin.h"
#include "vs_liquid_water_0.sc.mtl.bin.h"
#include "vs_liquid_water_1.sc.glsl.bin.h"
#include "vs_liquid_water_1.sc.essl.bin.h"
#include "vs_liquid_water_1.sc.spv.bin.h"
#include "vs_liquid_water_1.sc.mtl.bin.h"
#include "vs_liquid_water_2.sc.glsl.bin.h"
#include "vs_liquid_water_2.sc.essl.bin.h"
#include "vs_liquid_water_2.sc.spv.bin.h"
#include "vs_liquid_water_2.sc.mtl.bin.h"
#include "vs_liquid_water_3.sc.glsl.bin.h"
#include "vs_liquid_water_3.sc.essl.bin.h"
#include "vs_liquid_water_3.sc.spv.bin.h"
#include "vs_liquid_water_3.sc.mtl.bin.h"
#include "fs_water.sc.glsl.bin.h"
#include "fs_water.sc.essl.bin.h"
#include "fs_water.sc.spv.bin.h"
#include "fs_water.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_liquid_water_0.sc.dx11.bin.h"
#include "vs_liquid_water_1.sc.dx11.bin.h"
#include "vs_liquid_water_2.sc.dx11.bin.h"
#include "vs_liquid_water_3.sc.dx11.bin.h"
#include "fs_water.sc.dx11.bin.h"
#endif

#include "vs_liquid_water_no_spec_0.sc.glsl.bin.h"
#include "vs_liquid_water_no_spec_0.sc.essl.bin.h"
#include "vs_liquid_water_no_spec_0.sc.spv.bin.h"
#include "vs_liquid_water_no_spec_0.sc.mtl.bin.h"
#include "vs_liquid_water_no_spec_1.sc.glsl.bin.h"
#include "vs_liquid_water_no_spec_1.sc.essl.bin.h"
#include "vs_liquid_water_no_spec_1.sc.spv.bin.h"
#include "vs_liquid_water_no_spec_1.sc.mtl.bin.h"
#include "vs_liquid_water_no_spec_2.sc.glsl.bin.h"
#include "vs_liquid_water_no_spec_2.sc.essl.bin.h"
#include "vs_liquid_water_no_spec_2.sc.spv.bin.h"
#include "vs_liquid_water_no_spec_2.sc.mtl.bin.h"
#include "vs_liquid_water_no_spec_3.sc.glsl.bin.h"
#include "vs_liquid_water_no_spec_3.sc.essl.bin.h"
#include "vs_liquid_water_no_spec_3.sc.spv.bin.h"
#include "vs_liquid_water_no_spec_3.sc.mtl.bin.h"
#include "fs_liquid_water_no_spec.sc.glsl.bin.h"
#include "fs_liquid_water_no_spec.sc.essl.bin.h"
#include "fs_liquid_water_no_spec.sc.spv.bin.h"
#include "fs_liquid_water_no_spec.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_liquid_water_no_spec_0.sc.dx11.bin.h"
#include "vs_liquid_water_no_spec_1.sc.dx11.bin.h"
#include "vs_liquid_water_no_spec_2.sc.dx11.bin.h"
#include "vs_liquid_water_no_spec_3.sc.dx11.bin.h"
#include "fs_liquid_water_no_spec.sc.dx11.bin.h"
#endif

#include "vs_liquid_magma.sc.glsl.bin.h"
#include "vs_liquid_magma.sc.essl.bin.h"
#include "vs_liquid_magma.sc.spv.bin.h"
#include "vs_liquid_magma.sc.mtl.bin.h"
#include "fs_liquid_magma.sc.glsl.bin.h"
#include "fs_liquid_magma.sc.essl.bin.h"
#include "fs_liquid_magma.sc.spv.bin.h"
#include "fs_liquid_magma.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_liquid_magma.sc.dx11.bin.h"
#include "fs_liquid_magma.sc.dx11.bin.h"
#endif

#include "vs_liquid_procedural_water.sc.glsl.bin.h"
#include "vs_liquid_procedural_water.sc.essl.bin.h"
#include "vs_liquid_procedural_water.sc.spv.bin.h"
#include "vs_liquid_procedural_water.sc.mtl.bin.h"
#include "fs_liquid_procedural_water.sc.glsl.bin.h"
#include "fs_liquid_procedural_water.sc.essl.bin.h"
#include "fs_liquid_procedural_water.sc.spv.bin.h"
#include "fs_liquid_procedural_water.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_liquid_procedural_water.sc.dx11.bin.h"
#include "fs_liquid_procedural_water.sc.dx11.bin.h"
#endif

#include "vs_weather.sc.glsl.bin.h"
#include "vs_weather.sc.essl.bin.h"
#include "vs_weather.sc.spv.bin.h"
#include "vs_weather.sc.mtl.bin.h"
#include "fs_weather.sc.glsl.bin.h"
#include "fs_weather.sc.essl.bin.h"
#include "fs_weather.sc.spv.bin.h"
#include "fs_weather.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_weather.sc.dx11.bin.h"
#include "fs_weather.sc.dx11.bin.h"
#endif

#include "vs_minimap.sc.glsl.bin.h"
#include "vs_minimap.sc.essl.bin.h"
#include "vs_minimap.sc.spv.bin.h"
#include "vs_minimap.sc.mtl.bin.h"
#include "fs_minimap.sc.glsl.bin.h"
#include "fs_minimap.sc.essl.bin.h"
#include "fs_minimap.sc.spv.bin.h"
#include "fs_minimap.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_minimap.sc.dx11.bin.h"
#include "fs_minimap.sc.dx11.bin.h"
#endif

#include "vs_wmo.sc.glsl.bin.h"
#include "vs_wmo.sc.essl.bin.h"
#include "vs_wmo.sc.spv.bin.h"
#include "vs_wmo.sc.mtl.bin.h"
#include "fs_wmo.sc.glsl.bin.h"
#include "fs_wmo.sc.essl.bin.h"
#include "fs_wmo.sc.spv.bin.h"
#include "fs_wmo.sc.mtl.bin.h"
#include "fs_wmo_specular.sc.glsl.bin.h"
#include "fs_wmo_specular.sc.essl.bin.h"
#include "fs_wmo_specular.sc.spv.bin.h"
#include "fs_wmo_specular.sc.mtl.bin.h"
#include "fs_wmo_env.sc.glsl.bin.h"
#include "fs_wmo_env.sc.essl.bin.h"
#include "fs_wmo_env.sc.spv.bin.h"
#include "fs_wmo_env.sc.mtl.bin.h"
#include "fs_wmo_two_layer.sc.glsl.bin.h"
#include "fs_wmo_two_layer.sc.essl.bin.h"
#include "fs_wmo_two_layer.sc.spv.bin.h"
#include "fs_wmo_two_layer.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_wmo.sc.dx11.bin.h"
#include "fs_wmo.sc.dx11.bin.h"
#include "fs_wmo_specular.sc.dx11.bin.h"
#include "fs_wmo_env.sc.dx11.bin.h"
#include "fs_wmo_two_layer.sc.dx11.bin.h"
#endif

#include "vs_ribbon.sc.glsl.bin.h"
#include "vs_ribbon.sc.essl.bin.h"
#include "vs_ribbon.sc.spv.bin.h"
#include "vs_ribbon.sc.mtl.bin.h"
#include "fs_ribbon.sc.glsl.bin.h"
#include "fs_ribbon.sc.essl.bin.h"
#include "fs_ribbon.sc.spv.bin.h"
#include "fs_ribbon.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_ribbon.sc.dx11.bin.h"
#include "fs_ribbon.sc.dx11.bin.h"
#endif

#include "vs_nameplate.sc.glsl.bin.h"
#include "vs_nameplate.sc.essl.bin.h"
#include "vs_nameplate.sc.spv.bin.h"
#include "vs_nameplate.sc.mtl.bin.h"
#include "fs_nameplate.sc.glsl.bin.h"
#include "fs_nameplate.sc.essl.bin.h"
#include "fs_nameplate.sc.spv.bin.h"
#include "fs_nameplate.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_nameplate.sc.dx11.bin.h"
#include "fs_nameplate.sc.dx11.bin.h"
#endif

#include "vs_decal.sc.glsl.bin.h"
#include "vs_decal.sc.essl.bin.h"
#include "vs_decal.sc.spv.bin.h"
#include "vs_decal.sc.mtl.bin.h"
#include "fs_decal.sc.glsl.bin.h"
#include "fs_decal.sc.essl.bin.h"
#include "fs_decal.sc.spv.bin.h"
#include "fs_decal.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_decal.sc.dx11.bin.h"
#include "fs_decal.sc.dx11.bin.h"
#endif

#include "vs_debug.sc.glsl.bin.h"
#include "vs_debug.sc.essl.bin.h"
#include "vs_debug.sc.spv.bin.h"
#include "vs_debug.sc.mtl.bin.h"
#include "fs_debug.sc.glsl.bin.h"
#include "fs_debug.sc.essl.bin.h"
#include "fs_debug.sc.spv.bin.h"
#include "fs_debug.sc.mtl.bin.h"

#include "vs_portal_fill.sc.glsl.bin.h"
#include "vs_portal_fill.sc.essl.bin.h"
#include "vs_portal_fill.sc.spv.bin.h"
#include "vs_portal_fill.sc.mtl.bin.h"

#include "vs_celestial.sc.glsl.bin.h"
#include "vs_celestial.sc.essl.bin.h"
#include "vs_celestial.sc.spv.bin.h"
#include "vs_celestial.sc.mtl.bin.h"
#include "fs_celestial.sc.glsl.bin.h"
#include "fs_celestial.sc.essl.bin.h"
#include "fs_celestial.sc.spv.bin.h"
#include "fs_celestial.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_portal_fill.sc.dx11.bin.h"
#include "vs_celestial.sc.dx11.bin.h"
#include "fs_celestial.sc.dx11.bin.h"
#include "vs_debug.sc.dx11.bin.h"
#include "fs_debug.sc.dx11.bin.h"
#endif

#if defined(_WIN32)
#endif

#include "vs_postprocess.sc.glsl.bin.h"
#include "vs_postprocess.sc.essl.bin.h"
#include "vs_postprocess.sc.spv.bin.h"
#include "vs_postprocess.sc.mtl.bin.h"
#include "fs_postprocess_death.sc.glsl.bin.h"
#include "fs_postprocess_death.sc.essl.bin.h"
#include "fs_postprocess_death.sc.spv.bin.h"
#include "fs_postprocess_death.sc.mtl.bin.h"
#include "fs_postprocess_blit.sc.glsl.bin.h"
#include "fs_postprocess_blit.sc.essl.bin.h"
#include "fs_postprocess_blit.sc.spv.bin.h"
#include "fs_postprocess_blit.sc.mtl.bin.h"
#include "fs_postprocess_box4.sc.glsl.bin.h"
#include "fs_postprocess_box4.sc.essl.bin.h"
#include "fs_postprocess_box4.sc.spv.bin.h"
#include "fs_postprocess_box4.sc.mtl.bin.h"
#include "fs_postprocess_blur_h.sc.glsl.bin.h"
#include "fs_postprocess_blur_h.sc.essl.bin.h"
#include "fs_postprocess_blur_h.sc.spv.bin.h"
#include "fs_postprocess_blur_h.sc.mtl.bin.h"
#include "fs_postprocess_blur_v.sc.glsl.bin.h"
#include "fs_postprocess_blur_v.sc.essl.bin.h"
#include "fs_postprocess_blur_v.sc.spv.bin.h"
#include "fs_postprocess_blur_v.sc.mtl.bin.h"
#include "fs_postprocess_composite.sc.glsl.bin.h"
#include "fs_postprocess_composite.sc.essl.bin.h"
#include "fs_postprocess_composite.sc.spv.bin.h"
#include "fs_postprocess_composite.sc.mtl.bin.h"
#if defined(_WIN32)
#include "vs_postprocess.sc.dx11.bin.h"
#include "fs_postprocess_death.sc.dx11.bin.h"
#include "fs_postprocess_blit.sc.dx11.bin.h"
#include "fs_postprocess_box4.sc.dx11.bin.h"
#include "fs_postprocess_blur_h.sc.dx11.bin.h"
#include "fs_postprocess_blur_v.sc.dx11.bin.h"
#include "fs_postprocess_composite.sc.dx11.bin.h"
#endif

#if defined(_WIN32)
#define _OPENWOW_DX11_ENTRY(_name)                                        \
    { bgfx::RendererType::Direct3D11, _name##_dx11,                       \
      uint32_t(sizeof(_name##_dx11)) },
#else
#define _OPENWOW_DX11_ENTRY(_name)
#endif

#define OPENWOW_EMBEDDED_SHADER(_name)                                    \
    {                                                                     \
        #_name,                                                           \
        {                                                                 \
            { bgfx::RendererType::OpenGLES, _name##_essl,                \
              uint32_t(sizeof(_name##_essl)) },                           \
            { bgfx::RendererType::OpenGL,   _name##_glsl,                \
              uint32_t(sizeof(_name##_glsl)) },                           \
            { bgfx::RendererType::Vulkan,   _name##_spv,                 \
              uint32_t(sizeof(_name##_spv)) },                            \
            { bgfx::RendererType::Metal,    _name##_mtl,                 \
              uint32_t(sizeof(_name##_mtl)) },                            \
            _OPENWOW_DX11_ENTRY(_name)                                    \
            { bgfx::RendererType::Noop,                                   \
              (const uint8_t*)"VSH\x5\x0\x0\x0\x0\x0\x0", 10 },         \
            { bgfx::RendererType::Count, NULL, 0 }                       \
        }                                                                 \
    }

#define OPENWOW_EMBEDDED_SHADER_END()                                     \
    {                                                                     \
        NULL,                                                             \
        {                                                                 \
            { bgfx::RendererType::Count, NULL, 0 }                       \
        }                                                                 \
    }

#define OPENWOW_PP_SHADERS_AVAILABLE 1

namespace openwow::render {

static const bgfx::EmbeddedShader s_uiShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_ui),
    OPENWOW_EMBEDDED_SHADER(fs_ui),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_uiMaterialShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_ui),
    OPENWOW_EMBEDDED_SHADER(fs_ui_material),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_worldTextShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_ui),
    OPENWOW_EMBEDDED_SHADER(fs_world_text),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_m2Shaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_8),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_8_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_8_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_8_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_8_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_16),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_16_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_16_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_16_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_16_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_32),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_32_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_32_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_32_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_32_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_64),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_64_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_64_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_64_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_64_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_128),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_128_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_128_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_128_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_128_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_256),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_256_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_256_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_256_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_bones_256_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_instanced),
    OPENWOW_EMBEDDED_SHADER(vs_m2_instanced_lit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_instanced_lit_noenv),
    OPENWOW_EMBEDDED_SHADER(vs_m2_instanced_unlit_env),
    OPENWOW_EMBEDDED_SHADER(vs_m2_instanced_unlit_noenv),
    OPENWOW_EMBEDDED_SHADER(fs_m2),
    OPENWOW_EMBEDDED_SHADER(fs_m2_t1_op0),
    OPENWOW_EMBEDDED_SHADER(fs_m2_t1_op1),
    OPENWOW_EMBEDDED_SHADER(fs_m2_t2_op0_op6),
    OPENWOW_EMBEDDED_SHADER(fs_m2_t2_op1_op0),
    OPENWOW_EMBEDDED_SHADER(fs_m2_t2_op1_op1),
    OPENWOW_EMBEDDED_SHADER(fs_m2_t2_op1_op4),
    OPENWOW_EMBEDDED_SHADER(fs_m2_special1),
    OPENWOW_EMBEDDED_SHADER(fs_m2_special2),
    OPENWOW_EMBEDDED_SHADER(fs_m2_special3),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_particleShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_particle),
    OPENWOW_EMBEDDED_SHADER(fs_particle),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_skyShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_sky),
    OPENWOW_EMBEDDED_SHADER(fs_sky),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_terrainShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_terrain),
    OPENWOW_EMBEDDED_SHADER(fs_terrain),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_distantTerrainShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_distant_terrain),
    OPENWOW_EMBEDDED_SHADER(fs_distant_terrain),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_terrainSplatShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_terrain_splat),
    OPENWOW_EMBEDDED_SHADER(fs_terrain_splat),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_terrainSplatArrayShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_terrain_splat_array),
    OPENWOW_EMBEDDED_SHADER(fs_terrain_splat_array),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_liquidWaterShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_0),
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_1),
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_2),
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_3),
    OPENWOW_EMBEDDED_SHADER(fs_water),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_liquidWaterNoSpecShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_no_spec_0),
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_no_spec_1),
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_no_spec_2),
    OPENWOW_EMBEDDED_SHADER(vs_liquid_water_no_spec_3),
    OPENWOW_EMBEDDED_SHADER(fs_liquid_water_no_spec),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_liquidMagmaShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_liquid_magma),
    OPENWOW_EMBEDDED_SHADER(fs_liquid_magma),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_liquidProceduralWaterShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_liquid_procedural_water),
    OPENWOW_EMBEDDED_SHADER(fs_liquid_procedural_water),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_weatherShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_weather),
    OPENWOW_EMBEDDED_SHADER(fs_weather),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_minimapShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_minimap),
    OPENWOW_EMBEDDED_SHADER(fs_minimap),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_wmoShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_wmo),
    OPENWOW_EMBEDDED_SHADER(fs_wmo),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_wmoSpecularShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_wmo),
    OPENWOW_EMBEDDED_SHADER(fs_wmo_specular),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_wmoEnvShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_wmo),
    OPENWOW_EMBEDDED_SHADER(fs_wmo_env),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_wmoTwoLayerShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_wmo),
    OPENWOW_EMBEDDED_SHADER(fs_wmo_two_layer),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ribbonShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_ribbon),
    OPENWOW_EMBEDDED_SHADER(fs_ribbon),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_nameplateShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_nameplate),
    OPENWOW_EMBEDDED_SHADER(fs_nameplate),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_decalShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_decal),
    OPENWOW_EMBEDDED_SHADER(fs_decal),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_debugShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_debug),
    OPENWOW_EMBEDDED_SHADER(fs_debug),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_portalFillShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_portal_fill),
    OPENWOW_EMBEDDED_SHADER(fs_debug),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_celestialShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_celestial),
    OPENWOW_EMBEDDED_SHADER(fs_celestial),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ppDeathShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_postprocess),
    OPENWOW_EMBEDDED_SHADER(fs_postprocess_death),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ppBlitShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_postprocess),
    OPENWOW_EMBEDDED_SHADER(fs_postprocess_blit),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ppBox4Shaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_postprocess),
    OPENWOW_EMBEDDED_SHADER(fs_postprocess_box4),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ppBlurHShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_postprocess),
    OPENWOW_EMBEDDED_SHADER(fs_postprocess_blur_h),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ppBlurVShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_postprocess),
    OPENWOW_EMBEDDED_SHADER(fs_postprocess_blur_v),
    OPENWOW_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_ppCompositeShaders[] = {
    OPENWOW_EMBEDDED_SHADER(vs_postprocess),
    OPENWOW_EMBEDDED_SHADER(fs_postprocess_composite),
    OPENWOW_EMBEDDED_SHADER_END()
};

}
