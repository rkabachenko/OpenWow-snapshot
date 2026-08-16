find_package(SDL2 CONFIG REQUIRED)

add_executable(openwow-client
  composition/client_helpers.cpp
  composition/client_helpers.h
  composition/main.cpp
  glue_host/glue_client.cpp
  glue_host/glue_client.h
  glue_host/glue_client_input.cpp
  glue_host/debug_ui_control_adapter.cpp
  glue_host/debug_ui_control_adapter.h
  glue_host/glue_flow.cpp
  glue_host/glue_flow.h
  glue_host/glue_init_parity.h
  glue_host/presentation_settings.cpp
  glue_host/presentation_settings.h
  glue_host/realm_addon_handshake_composition.cpp
  glue_host/realm_addon_handshake_composition.h
  glue_host/glue_slider_input.h
  glue_host/glue_sound_playback_state.h
  glue_host/settings_capability_policy.cpp
  glue_host/settings_capability_policy.h
  glue_host/sdl_glue_host.cpp
  glue_host/sdl_glue_host.h
  scenarios/offline_world_fixture.cpp
  scenarios/offline_world_fixture.h
  scenarios/scenario_runner.cpp
  scenarios/scenario_runner.h
  scenarios/scenario_world_oracle.cpp
  scenarios/scenario_world_oracle.h
  scenarios/scenario_world_ui_driver.cpp
  scenarios/scenario_world_ui_driver.h
)
target_include_directories(openwow-client PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
)
target_compile_definitions(openwow-client PRIVATE
  OPENWOW_HAS_STORMLIB=$<BOOL:${OPENWOW_HAS_STORMLIB}>

  $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>
)

target_compile_options(openwow-client PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_include_directories(openwow-client SYSTEM PRIVATE
  $<TARGET_PROPERTY:openwow_lua,INTERFACE_INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(openwow-client PRIVATE
  openwow_game_dance_data_adapter
  openwow_render_bgfx
  openwow_runtime_bootstrap
)

set(OPENWOW_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging")
set(OPENWOW_VERSION "${PROJECT_VERSION}")

if(APPLE)

  set(OPENWOW_MACOS_BUNDLE_NAME "OpenWoW.app")
  set(OPENWOW_MACOS_BUNDLE_EXECUTABLE "$<TARGET_FILE_NAME:openwow-client>")
  set(OPENWOW_MACOS_BUNDLE_ICON_FILE "openwow.icns")

  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(OPENWOW_MACOS_MINIMUM_SYSTEM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  else()
    execute_process(
      COMMAND sw_vers -productVersion
      OUTPUT_VARIABLE OPENWOW_MACOS_MINIMUM_SYSTEM_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(NOT OPENWOW_MACOS_MINIMUM_SYSTEM_VERSION)
      set(OPENWOW_MACOS_MINIMUM_SYSTEM_VERSION "${CMAKE_HOST_SYSTEM_VERSION}")
    endif()
  endif()

  configure_file(
    "${OPENWOW_PACKAGING_DIR}/macos/Info.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.configured"
    @ONLY
  )
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.configured"
  )
  install(TARGETS openwow-client
    RUNTIME DESTINATION "${OPENWOW_MACOS_BUNDLE_NAME}/Contents/MacOS"
    COMPONENT client
  )
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    DESTINATION "${OPENWOW_MACOS_BUNDLE_NAME}/Contents"
    COMPONENT client
  )

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo" "APPL????")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo"
    DESTINATION "${OPENWOW_MACOS_BUNDLE_NAME}/Contents"
    COMPONENT client
  )
  install(FILES "${OPENWOW_PACKAGING_DIR}/icons/openwow.icns"
    DESTINATION "${OPENWOW_MACOS_BUNDLE_NAME}/Contents/Resources"
    COMPONENT client
  )
elseif(WIN32)

  set_target_properties(openwow-client PROPERTIES WIN32_EXECUTABLE ON)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(openwow-client PRIVATE SDL2::SDL2main)
  endif()

  string(REPLACE "." "," OPENWOW_VERSION_RC_COMMA "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
  set(OPENWOW_VERSION_RC "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
  configure_file(
    "${OPENWOW_PACKAGING_DIR}/windows/openwow-client.manifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/openwow-client.manifest"
    @ONLY
  )
  file(TO_CMAKE_PATH "${OPENWOW_PACKAGING_DIR}/icons/openwow.ico" OPENWOW_WINDOWS_ICON_PATH)
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/openwow-client.manifest" OPENWOW_WINDOWS_MANIFEST_PATH)
  configure_file(
    "${OPENWOW_PACKAGING_DIR}/windows/openwow-client.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/openwow-client.rc"
    @ONLY
  )

  enable_language(RC)
  target_sources(openwow-client PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/openwow-client.rc"
  )
  if(MSVC)
    target_link_options(openwow-client PRIVATE "/MANIFEST:NO")
  endif()
  install(TARGETS openwow-client
    RUNTIME DESTINATION .
    COMPONENT client
  )
else()

  include(GNUInstallDirs)
  install(TARGETS openwow-client
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT client
  )
  install(FILES "${OPENWOW_PACKAGING_DIR}/linux/openwow-client.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    COMPONENT client
  )
  install(FILES "${OPENWOW_PACKAGING_DIR}/icons/openwow.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
    COMPONENT client
  )
endif()
