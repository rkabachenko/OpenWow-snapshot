add_library(openwow_render_api
  api/frame_graph.cpp
  api/render_resource_registry.cpp
)

target_include_directories(openwow_render_api
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_render_api)

openwow_add_component(openwow_render_runtime
  ui/minimap_surface_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/ui/game/runtime/render/ui_render_resources.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/ui/game/runtime/render/ui_compositor.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/ui/game/runtime/render/texture_region_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/ui/game/runtime/render/text_region_renderer.cpp
  ${PROJECT_SOURCE_DIR}/src/openwow/ui/game/runtime/render/model_region_renderer.cpp
  ui/chat_frame_presenter.cpp
  ui/loot_frame_presenter.cpp
  resources/textures/texture_filtering_mode.cpp
  platform/renderer_backend_selection.cpp
)
target_link_libraries(openwow_render_runtime PRIVATE

  bgfx::bgfx
  openwow_audio
  openwow_data_runtime
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_input
  openwow_lua
  openwow_media_runtime
  openwow_render_api
  openwow_render_bgfx
  openwow_ui_display_settings
  openwow_ui_widgets_media
  openwow_world_runtime
  Freetype::Freetype
  SDL2::SDL2
)
