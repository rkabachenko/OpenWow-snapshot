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

if(IS_DIRECTORY "${OPENWOW_LOCAL_CONTENT_ROOT}/Data")
  target_compile_definitions(openwow-client PRIVATE
    OPENWOW_DEVELOPMENT_CONTENT_ROOT="${OPENWOW_LOCAL_CONTENT_ROOT}"
  )
  message(STATUS "OpenWoW local content root: ${OPENWOW_LOCAL_CONTENT_ROOT}")
else()
  message(STATUS
    "OpenWoW local content root is not present at ${OPENWOW_LOCAL_CONTENT_ROOT}; "
    "the client will use explicit, bundled, or colocated Data")
endif()

set(OPENWOW_PROJECT_OVERRIDE_ROOT "${PROJECT_SOURCE_DIR}/assets/overrides")
if(IS_DIRECTORY "${OPENWOW_PROJECT_OVERRIDE_ROOT}")
  target_compile_definitions(openwow-client PRIVATE
    OPENWOW_DEVELOPMENT_OVERRIDE_ROOT="${OPENWOW_PROJECT_OVERRIDE_ROOT}"
  )
endif()

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

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "The native iOS application target requires the Xcode generator")
  endif()
  if(NOT TARGET SDL2::SDL2main)
    message(FATAL_ERROR "The iOS SDL2 package does not provide SDL2::SDL2main")
  endif()

  target_sources(openwow-client PRIVATE
    mobile/mobile_input_controller.cpp
    mobile/mobile_input_controller.h
    mobile/mobile_ios_platform.h
    mobile/mobile_ios_platform.mm
    mobile/mobile_log_export.mm
  )
  set_source_files_properties(mobile/mobile_log_export.mm PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc")

  function(openwow_add_ios_resource_tree target source_root bundle_root)
    file(GLOB_RECURSE resource_files CONFIGURE_DEPENDS
      LIST_DIRECTORIES FALSE "${source_root}/*")
    list(FILTER resource_files EXCLUDE REGEX "/(\\.gitkeep|\\.DS_Store)$")
    foreach(resource_file IN LISTS resource_files)
      file(RELATIVE_PATH resource_relative "${source_root}" "${resource_file}")
      get_filename_component(resource_directory "${resource_relative}" DIRECTORY)
      if(resource_directory STREQUAL "")
        set(resource_location "Resources/${bundle_root}")
      else()
        set(resource_location "Resources/${bundle_root}/${resource_directory}")
      endif()
      set_source_files_properties("${resource_file}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "${resource_location}")
    endforeach()
    target_sources(${target} PRIVATE ${resource_files})
  endfunction()

  configure_file(
    "${OPENWOW_PACKAGING_DIR}/ios/Info.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Info-iOS.plist"
    @ONLY
  )

  set_target_properties(openwow-client PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info-iOS.plist"
    OUTPUT_NAME "OpenWoW"
    SKIP_BUILD_RPATH ON
    XCODE_GENERATE_SCHEME ON
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
      "${OPENWOW_PACKAGING_DIR}/ios/OpenWoW.entitlements"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OPENWOW_IOS_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "NO"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )
  if(OPENWOW_IOS_DEVELOPMENT_TEAM)
    set_target_properties(openwow-client PROPERTIES
      XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OPENWOW_IOS_DEVELOPMENT_TEAM}"
    )
  endif()

  target_link_libraries(openwow-client PRIVATE SDL2::SDL2main)
  foreach(framework IN ITEMS
      AudioToolbox AVFoundation Foundation GameController Metal QuartzCore
      Security SystemConfiguration UIKit)
    openwow_link_apple_framework(openwow-client "${framework}")
  endforeach()

  # Let actool generate the device icons, Assets.car and matching Info.plist
  # entries together so the archive carries the App Store icon metadata.
  set(OPENWOW_IOS_ASSET_CATALOG "${OPENWOW_PACKAGING_DIR}/ios/Assets.xcassets")
  target_sources(openwow-client PRIVATE "${OPENWOW_IOS_ASSET_CATALOG}")
  set_source_files_properties("${OPENWOW_IOS_ASSET_CATALOG}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

  openwow_add_ios_resource_tree(openwow-client
    "${OPENWOW_PROJECT_OVERRIDE_ROOT}" "OpenWoWOverrides")
  if(OPENWOW_EMBED_GAME_DATA)
    if(NOT IS_DIRECTORY "${OPENWOW_LOCAL_CONTENT_ROOT}/Data")
      message(FATAL_ERROR
        "OPENWOW_EMBED_GAME_DATA requires Data/ under "
        "OPENWOW_LOCAL_CONTENT_ROOT=${OPENWOW_LOCAL_CONTENT_ROOT}")
    endif()
    openwow_add_ios_resource_tree(openwow-client
      "${OPENWOW_LOCAL_CONTENT_ROOT}/Data" "GameRoot/Data")
  endif()

  find_program(OPENWOW_XCRUN_EXECUTABLE NAMES xcrun REQUIRED)
  set(OPENWOW_IOS_SYNC_MARKER
    "${CMAKE_CURRENT_BINARY_DIR}/.openwow-ios-data-ready")
  add_custom_target(openwow-ios-sync-data
    COMMAND "${CMAKE_COMMAND}"
      "-DOPENWOW_IOS_DEVICE=${OPENWOW_IOS_DEVICE}"
      "-DOPENWOW_IOS_BUNDLE_IDENTIFIER=${OPENWOW_IOS_BUNDLE_IDENTIFIER}"
      "-DOPENWOW_LOCAL_CONTENT_ROOT=${OPENWOW_LOCAL_CONTENT_ROOT}"
      "-DOPENWOW_PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DOPENWOW_IOS_TEXTURE_CACHE_LOCALE=${OPENWOW_IOS_TEXTURE_CACHE_LOCALE}"
      "-DOPENWOW_IOS_TEXTURE_CACHE_TOOL=${OPENWOW_IOS_TEXTURE_CACHE_TOOL}"
      "-DOPENWOW_XCRUN_EXECUTABLE=${OPENWOW_XCRUN_EXECUTABLE}"
      "-DOPENWOW_IOS_SYNC_MARKER=${OPENWOW_IOS_SYNC_MARKER}"
      -P "${PROJECT_SOURCE_DIR}/cmake/SyncIOSData.cmake"
    COMMENT "Incrementally sync build-12340 Data to an installed iOS app"
    USES_TERMINAL
    VERBATIM)

  install(TARGETS openwow-client
    BUNDLE DESTINATION .
    COMPONENT client
  )
elseif(APPLE)

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
  install(DIRECTORY "${OPENWOW_PROJECT_OVERRIDE_ROOT}/"
    DESTINATION "${OPENWOW_MACOS_BUNDLE_NAME}/Contents/Resources/OpenWoWOverrides"
    COMPONENT client
    PATTERN ".gitkeep" EXCLUDE
  )

  if(OPENWOW_EMBED_GAME_DATA)
    if(NOT IS_DIRECTORY "${OPENWOW_LOCAL_CONTENT_ROOT}/Data")
      message(FATAL_ERROR
        "OPENWOW_EMBED_GAME_DATA requires Data/ under "
        "OPENWOW_LOCAL_CONTENT_ROOT=${OPENWOW_LOCAL_CONTENT_ROOT}")
    endif()
    install(DIRECTORY "${OPENWOW_LOCAL_CONTENT_ROOT}/Data"
      DESTINATION "${OPENWOW_MACOS_BUNDLE_NAME}/Contents/Resources/GameRoot"
      COMPONENT client
      USE_SOURCE_PERMISSIONS
    )
  endif()

  install(CODE [[
    set(_openwow_bundle_path
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/OpenWoW.app")
    execute_process(
      COMMAND /usr/bin/codesign --force --sign - "${_openwow_bundle_path}"
      RESULT_VARIABLE _openwow_codesign_result
      OUTPUT_VARIABLE _openwow_codesign_output
      ERROR_VARIABLE _openwow_codesign_error
    )
    if(NOT _openwow_codesign_result EQUAL 0)
      message(FATAL_ERROR
        "Failed to ad-hoc sign ${_openwow_bundle_path}: "
        "${_openwow_codesign_output}${_openwow_codesign_error}")
    endif()
    message(STATUS "Ad-hoc signed: ${_openwow_bundle_path}")
  ]] COMPONENT client)
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
