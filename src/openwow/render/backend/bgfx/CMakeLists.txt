set(OPENWOW_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
set(OPENWOW_SHADER_DIR "${OPENWOW_GENERATED_DIR}/shaders")

include("${PROJECT_SOURCE_DIR}/cmake/CompileShaders.cmake")

set(_SHADER_SRC "${PROJECT_SOURCE_DIR}/src/openwow/render/shaders")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_ui.sc"
  VARYING "${_SHADER_SRC}/varying.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_UI_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_ui.sc"
  VARYING "${_SHADER_SRC}/varying.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_UI_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_ui_material.sc"
  VARYING "${_SHADER_SRC}/varying.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_UI_MATERIAL_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_world_text.sc"
  VARYING "${_SHADER_SRC}/varying.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WORLD_TEXT_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

set(OPENWOW_M2_VSH)
set(OPENWOW_M2_FSH)

set(_M2_VS_PERMUTATIONS "" "_lit_env" "_lit_noenv" "_unlit_env" "_unlit_noenv")
foreach(_M2_BONE_PALETTE_SIZE IN ITEMS 8 16 32 64 128 256)
  foreach(_M2_VS_PERMUTATION IN LISTS _M2_VS_PERMUTATIONS)
    openwow_compile_shader(TYPE VERTEX
      SHADER "${_SHADER_SRC}/vs_m2${_M2_VS_PERMUTATION}.sc"
      VARYING "${_SHADER_SRC}/varying_m2.def.sc" OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
      OUT_FILES_VAR _M2_VSH_VARIANT
      BIN2C_BASENAME "vs_m2_bones_${_M2_BONE_PALETTE_SIZE}${_M2_VS_PERMUTATION}"
      DEFINES "OPENWOW_M2_BONE_PALETTE_SIZE=${_M2_BONE_PALETTE_SIZE}"
      INCLUDE_DIRS "${_SHADER_SRC}")
    list(APPEND OPENWOW_M2_VSH ${_M2_VSH_VARIANT})
  endforeach()
endforeach()

foreach(_M2_VS_PERMUTATION IN LISTS _M2_VS_PERMUTATIONS)
  openwow_compile_shader(TYPE VERTEX
    SHADER "${_SHADER_SRC}/vs_m2_instanced${_M2_VS_PERMUTATION}.sc"
    VARYING "${_SHADER_SRC}/varying_m2.def.sc" OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
    OUT_FILES_VAR _M2_VSH_VARIANT
    BIN2C_BASENAME "vs_m2_instanced${_M2_VS_PERMUTATION}"
    DEFINES "OPENWOW_M2_BONE_PALETTE_SIZE=8"
    INCLUDE_DIRS "${_SHADER_SRC}")
  list(APPEND OPENWOW_M2_VSH ${_M2_VSH_VARIANT})
endforeach()

foreach(_M2_FS_PERMUTATION IN ITEMS
    fs_m2
    fs_m2_t1_op0 fs_m2_t1_op1
    fs_m2_t2_op0_op6 fs_m2_t2_op1_op0 fs_m2_t2_op1_op1 fs_m2_t2_op1_op4
    fs_m2_special1 fs_m2_special2 fs_m2_special3)
  openwow_compile_shader(TYPE FRAGMENT
    SHADER "${_SHADER_SRC}/${_M2_FS_PERMUTATION}.sc"
    VARYING "${_SHADER_SRC}/varying_m2.def.sc" OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
    OUT_FILES_VAR _M2_FSH_VARIANT INCLUDE_DIRS "${_SHADER_SRC}")
  list(APPEND OPENWOW_M2_FSH ${_M2_FSH_VARIANT})
endforeach()

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_terrain.sc"
  VARYING "${_SHADER_SRC}/varying_terrain.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_TERRAIN_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_terrain.sc"
  VARYING "${_SHADER_SRC}/varying_terrain.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_TERRAIN_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_distant_terrain.sc"
  VARYING "${_SHADER_SRC}/varying_distant_terrain.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_DISTANT_TERRAIN_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_distant_terrain.sc"
  VARYING "${_SHADER_SRC}/varying_distant_terrain.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_DISTANT_TERRAIN_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_terrain_splat.sc"
  VARYING "${_SHADER_SRC}/varying_terrain_splat.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_TERRAIN_SPLAT_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_terrain_splat.sc"
  VARYING "${_SHADER_SRC}/varying_terrain_splat.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_TERRAIN_SPLAT_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_terrain_splat_array.sc"
  VARYING "${_SHADER_SRC}/varying_terrain_splat.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_TERRAIN_SPLAT_ARRAY_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_terrain_splat_array.sc"
  VARYING "${_SHADER_SRC}/varying_terrain_splat.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_TERRAIN_SPLAT_ARRAY_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_sky.sc"
  VARYING "${_SHADER_SRC}/varying_sky.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_SKY_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_sky.sc"
  VARYING "${_SHADER_SRC}/varying_sky.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_SKY_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_weather.sc"
  VARYING "${_SHADER_SRC}/varying_weather.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WEATHER_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_weather.sc"
  VARYING "${_SHADER_SRC}/varying_weather.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WEATHER_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

set(OPENWOW_LIQUID_WATER_VSH)
set(OPENWOW_LIQUID_WATER_NO_SPEC_VSH)
foreach(_LIQUID_POINT_LIGHT_COUNT RANGE 0 3)
  openwow_compile_shader(TYPE VERTEX SHADER "${_SHADER_SRC}/vs_water.sc"
    VARYING "${_SHADER_SRC}/varying_water.def.sc"
    OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
    OUT_FILES_VAR _LIQUID_WATER_VARIANT_VSH
    BIN2C_BASENAME "vs_liquid_water_${_LIQUID_POINT_LIGHT_COUNT}"
    DEFINES "OPENWOW_LIQUID_POINT_LIGHT_COUNT=${_LIQUID_POINT_LIGHT_COUNT}"
    INCLUDE_DIRS "${_SHADER_SRC}")
  list(APPEND OPENWOW_LIQUID_WATER_VSH ${_LIQUID_WATER_VARIANT_VSH})

  openwow_compile_shader(TYPE VERTEX
    SHADER "${_SHADER_SRC}/vs_liquid_water_no_spec.sc"
    VARYING "${_SHADER_SRC}/varying_water.def.sc"
    OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
    OUT_FILES_VAR _LIQUID_WATER_NO_SPEC_VARIANT_VSH
    BIN2C_BASENAME "vs_liquid_water_no_spec_${_LIQUID_POINT_LIGHT_COUNT}"
    DEFINES "OPENWOW_LIQUID_POINT_LIGHT_COUNT=${_LIQUID_POINT_LIGHT_COUNT}"
    INCLUDE_DIRS "${_SHADER_SRC}")
  list(APPEND OPENWOW_LIQUID_WATER_NO_SPEC_VSH
       ${_LIQUID_WATER_NO_SPEC_VARIANT_VSH})
endforeach()

openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_water.sc"
  VARYING "${_SHADER_SRC}/varying_water.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WATER_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_liquid_water_no_spec.sc"
  VARYING "${_SHADER_SRC}/varying_water.def.sc" OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_LIQUID_WATER_NO_SPEC_FSH INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX SHADER "${_SHADER_SRC}/vs_liquid_magma.sc"
  VARYING "${_SHADER_SRC}/varying_liquid_magma.def.sc" OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_LIQUID_MAGMA_VSH INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_liquid_magma.sc"
  VARYING "${_SHADER_SRC}/varying_liquid_magma.def.sc" OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_LIQUID_MAGMA_FSH INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX SHADER "${_SHADER_SRC}/vs_liquid_procedural_water.sc"
  VARYING "${_SHADER_SRC}/varying_liquid_procedural_water.def.sc"
  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_LIQUID_PROCEDURAL_WATER_VSH INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_liquid_procedural_water.sc"
  VARYING "${_SHADER_SRC}/varying_liquid_procedural_water.def.sc"
  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_LIQUID_PROCEDURAL_WATER_FSH INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_minimap.sc"
  VARYING "${_SHADER_SRC}/varying_minimap.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_MINIMAP_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_minimap.sc"
  VARYING "${_SHADER_SRC}/varying_minimap.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_MINIMAP_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_particle.sc"
  VARYING "${_SHADER_SRC}/varying_particle.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PARTICLE_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_particle.sc"
  VARYING "${_SHADER_SRC}/varying_particle.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PARTICLE_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_postprocess.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_postprocess_death.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_DEATH_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_postprocess_blit.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_BLIT_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_postprocess_box4.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_BOX4_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_postprocess_blur_h.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_BLUR_H_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_postprocess_blur_v.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_BLUR_V_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_postprocess_composite.sc"
  VARYING "${_SHADER_SRC}/varying_postprocess.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PP_COMPOSITE_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_wmo.sc"
  VARYING "${_SHADER_SRC}/varying_wmo.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WMO_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_wmo.sc"
  VARYING "${_SHADER_SRC}/varying_wmo.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WMO_FSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_wmo_specular.sc"
  VARYING "${_SHADER_SRC}/varying_wmo.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WMO_SPECULAR_FSH  INCLUDE_DIRS "${_SHADER_SRC}"
  BIN2C_BASENAME fs_wmo_specular)
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_wmo_env.sc"
  VARYING "${_SHADER_SRC}/varying_wmo.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WMO_ENV_FSH  INCLUDE_DIRS "${_SHADER_SRC}"
  BIN2C_BASENAME fs_wmo_env)
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_wmo_two_layer.sc"
  VARYING "${_SHADER_SRC}/varying_wmo.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_WMO_TWO_LAYER_FSH  INCLUDE_DIRS "${_SHADER_SRC}"
  BIN2C_BASENAME fs_wmo_two_layer)

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_ribbon.sc"
  VARYING "${_SHADER_SRC}/varying_ribbon.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_RIBBON_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_ribbon.sc"
  VARYING "${_SHADER_SRC}/varying_ribbon.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_RIBBON_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_nameplate.sc"
  VARYING "${_SHADER_SRC}/varying_nameplate.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_NAMEPLATE_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_nameplate.sc"
  VARYING "${_SHADER_SRC}/varying_nameplate.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_NAMEPLATE_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_decal.sc"
  VARYING "${_SHADER_SRC}/varying_decal.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_DECAL_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_decal.sc"
  VARYING "${_SHADER_SRC}/varying_decal.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_DECAL_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_debug.sc"
  VARYING "${_SHADER_SRC}/varying_debug.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_DEBUG_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_debug.sc"
  VARYING "${_SHADER_SRC}/varying_debug.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_DEBUG_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_portal_fill.sc"
  VARYING "${_SHADER_SRC}/varying_debug.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_PORTAL_FILL_VSH  INCLUDE_DIRS "${_SHADER_SRC}")

openwow_compile_shader(TYPE VERTEX   SHADER "${_SHADER_SRC}/vs_celestial.sc"
  VARYING "${_SHADER_SRC}/varying_celestial.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_CELESTIAL_VSH  INCLUDE_DIRS "${_SHADER_SRC}")
openwow_compile_shader(TYPE FRAGMENT SHADER "${_SHADER_SRC}/fs_celestial.sc"
  VARYING "${_SHADER_SRC}/varying_celestial.def.sc"  OUTPUT_DIR "${OPENWOW_SHADER_DIR}"
  OUT_FILES_VAR OPENWOW_CELESTIAL_FSH  INCLUDE_DIRS "${_SHADER_SRC}")

set(OPENWOW_ALL_SHADER_OUTPUTS
  ${OPENWOW_UI_VSH}           ${OPENWOW_UI_FSH} ${OPENWOW_UI_MATERIAL_FSH}
  ${OPENWOW_WORLD_TEXT_FSH}
  ${OPENWOW_M2_VSH}           ${OPENWOW_M2_FSH}
  ${OPENWOW_TERRAIN_VSH}      ${OPENWOW_TERRAIN_FSH}
  ${OPENWOW_DISTANT_TERRAIN_VSH} ${OPENWOW_DISTANT_TERRAIN_FSH}
  ${OPENWOW_TERRAIN_SPLAT_VSH} ${OPENWOW_TERRAIN_SPLAT_FSH}
  ${OPENWOW_TERRAIN_SPLAT_ARRAY_VSH} ${OPENWOW_TERRAIN_SPLAT_ARRAY_FSH}
  ${OPENWOW_SKY_VSH}          ${OPENWOW_SKY_FSH}
  ${OPENWOW_WEATHER_VSH}      ${OPENWOW_WEATHER_FSH}
  ${OPENWOW_LIQUID_WATER_VSH} ${OPENWOW_WATER_FSH}
  ${OPENWOW_LIQUID_WATER_NO_SPEC_VSH} ${OPENWOW_LIQUID_WATER_NO_SPEC_FSH}
  ${OPENWOW_LIQUID_MAGMA_VSH} ${OPENWOW_LIQUID_MAGMA_FSH}
  ${OPENWOW_LIQUID_PROCEDURAL_WATER_VSH}
  ${OPENWOW_LIQUID_PROCEDURAL_WATER_FSH}
  ${OPENWOW_MINIMAP_VSH}      ${OPENWOW_MINIMAP_FSH}
  ${OPENWOW_PARTICLE_VSH}     ${OPENWOW_PARTICLE_FSH}
  ${OPENWOW_PP_VSH}           ${OPENWOW_PP_DEATH_FSH}
  ${OPENWOW_PP_BLIT_FSH}      ${OPENWOW_PP_BOX4_FSH}
  ${OPENWOW_PP_BLUR_H_FSH}
  ${OPENWOW_PP_BLUR_V_FSH}    ${OPENWOW_PP_COMPOSITE_FSH}
  ${OPENWOW_WMO_VSH}          ${OPENWOW_WMO_FSH}
  ${OPENWOW_WMO_SPECULAR_FSH}  ${OPENWOW_WMO_ENV_FSH} ${OPENWOW_WMO_TWO_LAYER_FSH}
  ${OPENWOW_RIBBON_VSH}       ${OPENWOW_RIBBON_FSH}
  ${OPENWOW_NAMEPLATE_VSH}    ${OPENWOW_NAMEPLATE_FSH}
  ${OPENWOW_DECAL_VSH}        ${OPENWOW_DECAL_FSH}
  ${OPENWOW_DEBUG_VSH}        ${OPENWOW_DEBUG_FSH}
  ${OPENWOW_PORTAL_FILL_VSH}
  ${OPENWOW_CELESTIAL_VSH}    ${OPENWOW_CELESTIAL_FSH}
)
add_custom_target(openwow_shaders DEPENDS ${OPENWOW_ALL_SHADER_OUTPUTS})

add_library(openwow_render_bgfx
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/offscreen_render_target.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_glyph_atlas.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_text_cache.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_renderer_context.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_resource_registry.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/glue/glue_texture_stream.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/display_info_resolver.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_renderer_backend.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/glue/glue_bgfx_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/glue/glue_ghost_lighting.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/glue/glue_model_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/integration/ui/bgfx_display_device_adapter.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/game_loading_screen.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/animation/model_render_callback_pipeline.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/materials/model_ffx_render_callback.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/animation/animation_state.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/game/game_loop.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/game/world_scene.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/object_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/game/world_presentation_publisher.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/mount_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/equipment_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/bowstring_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/shaders/shader_registry.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/nameplate_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/unit_name_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/chat_bubble.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/floating_text.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/cursor_overlay_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/doodads/doodad_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/projected_decal_conjugation.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/selection_circle.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/blob_shadow.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/m2_projected_texture_decal.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/projectiles/missile_trajectory_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/textures/texture_manager.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/textures/texture_mip_upload.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/textures/texture_slice_arrays.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/textures/texture_streamer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_animator.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_cpu_skinning.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_particle_system.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_material_pipeline.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_texture_unit_preparation.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_shaders.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_skin_profile.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_skinned_mesh.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_skin_geometry.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/terrain/terrain_mesh.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/terrain/terrain_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/environment/sky_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/environment/sky_settings.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/environment/sky_cvar_callbacks.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/environment/weather_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/water/water_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/particles/particle_system.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/portrait_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/readback/pixel_readback.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_render_pipeline.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_animation_selection.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_animation_runtime.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_instance_store.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_instance_transform.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_instance_appearance.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_visual_clone.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_model_repository.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_model_preparation.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_gpu_resources.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_render_validation.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_submit_trace.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_geometry_submitter.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_effect_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_resource_streamer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_sequence_streamer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_model_queries.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_spatial_queries.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_visibility.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_system.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_system_state.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_camera_math.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/character_appearance_geosets.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/character_appearance_texture_baker.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/character_appearance_texture_sources.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/spell_visuals/spell_visual_effects.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/spell_visuals/spell_visual_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/characters/model_portrait.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/animation/attachment_graph.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/models/animation/m2_attachment_transform.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/spell_visuals/spell_visual_data.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/shadow_data.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/shadow_presentation_runtime.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/presentation/world_presentation_scene.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_ribbon_emitter.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/shaders/sky_shader_data.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/geometry/vertex_cache_optimizer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/wmo/wmo_mesh.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/wmo/wmo_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/wmo/wmo_portal_fill_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/terrain/distant_terrain.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/terrain/terrain_lod.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/scene_graph.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/frustum_culler.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/occlusion/occlusion_depth_buffer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/occlusion/occluder_polygon_builder.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/particles/particle_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/water/water_particulate_system.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/fonts/font_string_flags.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/fonts/glyph_atlas.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_text_escapes.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_render_context.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_animation.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/world/environment/celestial_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_shaders.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/text_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/textures/texture_asset.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/diagnostics/debug_draw_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/render_resource_snapshot.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/ffx_glow.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/m2/m2_cvar_callbacks.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/effects/postprocess/post_process.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/api/math/render_matrix_math.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/retail_render_profile.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/resources/textures/texture_cache_budget.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/scene/world_frame.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/platform/gamma_controller.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/ui/ui_acceleration.cpp
  ${OPENWOW_UI_VSH}
  ${OPENWOW_UI_FSH}
  ${OPENWOW_UI_MATERIAL_FSH}
  ${OPENWOW_WORLD_TEXT_FSH}
  ${OPENWOW_M2_VSH}
  ${OPENWOW_M2_FSH}
  ${OPENWOW_TERRAIN_VSH}
  ${OPENWOW_TERRAIN_FSH}
  ${OPENWOW_DISTANT_TERRAIN_VSH}
  ${OPENWOW_DISTANT_TERRAIN_FSH}
  ${OPENWOW_TERRAIN_SPLAT_VSH}
  ${OPENWOW_TERRAIN_SPLAT_FSH}
  ${OPENWOW_TERRAIN_SPLAT_ARRAY_VSH}
  ${OPENWOW_TERRAIN_SPLAT_ARRAY_FSH}
  ${OPENWOW_SKY_VSH}
  ${OPENWOW_SKY_FSH}
  ${OPENWOW_WEATHER_VSH}
  ${OPENWOW_WEATHER_FSH}
  ${OPENWOW_LIQUID_WATER_VSH}
  ${OPENWOW_WATER_FSH}
  ${OPENWOW_LIQUID_WATER_NO_SPEC_VSH}
  ${OPENWOW_LIQUID_WATER_NO_SPEC_FSH}
  ${OPENWOW_LIQUID_MAGMA_VSH}
  ${OPENWOW_LIQUID_MAGMA_FSH}
  ${OPENWOW_LIQUID_PROCEDURAL_WATER_VSH}
  ${OPENWOW_LIQUID_PROCEDURAL_WATER_FSH}
  ${OPENWOW_PARTICLE_VSH}
  ${OPENWOW_PARTICLE_FSH}
  ${OPENWOW_MINIMAP_VSH}
  ${OPENWOW_MINIMAP_FSH}
  ${OPENWOW_PP_VSH}
  ${OPENWOW_PP_DEATH_FSH}
  ${OPENWOW_PP_BLIT_FSH}
  ${OPENWOW_PP_BOX4_FSH}
  ${OPENWOW_PP_BLUR_H_FSH}
  ${OPENWOW_PP_BLUR_V_FSH}
  ${OPENWOW_PP_COMPOSITE_FSH}
  ${OPENWOW_WMO_VSH}
  ${OPENWOW_WMO_FSH}
  ${OPENWOW_WMO_SPECULAR_FSH}
  ${OPENWOW_WMO_ENV_FSH}
  ${OPENWOW_WMO_TWO_LAYER_FSH}
  ${OPENWOW_RIBBON_VSH}
  ${OPENWOW_RIBBON_FSH}
  ${OPENWOW_NAMEPLATE_VSH}
  ${OPENWOW_NAMEPLATE_FSH}
  ${OPENWOW_DECAL_VSH}
  ${OPENWOW_DECAL_FSH}
  ${OPENWOW_DEBUG_VSH}
  ${OPENWOW_DEBUG_FSH}
  ${OPENWOW_PORTAL_FILL_VSH}
  ${OPENWOW_CELESTIAL_VSH}
  ${OPENWOW_CELESTIAL_FSH}
)
if(APPLE)
  target_sources(openwow_render_bgfx PRIVATE
    ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/metal_presentation_layer.mm
  )
  target_link_libraries(openwow_render_bgfx PRIVATE "-framework QuartzCore")
endif()
target_include_directories(openwow_render_bgfx PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${OPENWOW_GENERATED_DIR}
  ${OPENWOW_SHADER_DIR}
  ${PROJECT_SOURCE_DIR}/third_party
)
target_link_libraries(openwow_render_bgfx PRIVATE
  openwow_data_runtime
  openwow_debug_runtime
  openwow_engine_core
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_game
  openwow_input
  openwow_lua
  openwow_media_runtime
  openwow_platform_runtime
  openwow_render_runtime
  openwow_ui_runtime
  openwow_world_runtime
)
target_link_libraries(openwow_render_bgfx PRIVATE openwow_ui_display_settings)
target_link_libraries(openwow_render_bgfx PUBLIC openwow_render_api)
target_link_libraries(openwow_render_bgfx PUBLIC bgfx::bgfx bgfx::bx bgfx::bimg)
target_link_libraries(openwow_render_bgfx PRIVATE SDL2::SDL2)
add_dependencies(openwow_render_bgfx openwow_shaders)
openwow_configure_target(openwow_render_bgfx)

add_library(openwow_render_bgfx_imgui
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_imgui_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/render/backend/bgfx/bgfx_imgui_renderer.h
)
target_include_directories(openwow_render_bgfx_imgui PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(openwow_render_bgfx_imgui PUBLIC openwow_render_bgfx)
target_link_libraries(openwow_render_bgfx_imgui PRIVATE imgui::imgui)
openwow_configure_target(openwow_render_bgfx_imgui)
