option(OPENWOW_BUILD_CLIENT "Build the retail client executable" ON)
option(OPENWOW_ENABLE_MPQ_VFS "Enable MPQ VFS via StormLib" ON)
set(OPENWOW_LOCAL_CONTENT_ROOT
  "${PROJECT_SOURCE_DIR}/../LocalData/335a"
  CACHE PATH
  "Local build-12340 content root containing Data/ (kept outside Git)"
)
option(OPENWOW_EMBED_GAME_DATA
  "Copy OPENWOW_LOCAL_CONTENT_ROOT/Data into an Apple application bundle" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/IOSDevelopmentSigning.cmake")
if(NOT DEFINED OPENWOW_IOS_BUNDLE_IDENTIFIER OR
    OPENWOW_IOS_BUNDLE_IDENTIFIER STREQUAL "org.openwow.client")
  set(OPENWOW_IOS_BUNDLE_IDENTIFIER
    "${OPENWOW_IOS_DEFAULT_BUNDLE_IDENTIFIER}" CACHE STRING
    "Bundle identifier for the native iOS application" FORCE)
endif()
if(NOT DEFINED OPENWOW_IOS_DEVELOPMENT_TEAM OR
    OPENWOW_IOS_DEVELOPMENT_TEAM STREQUAL "")
  set(OPENWOW_IOS_DEVELOPMENT_TEAM
    "${OPENWOW_IOS_DEFAULT_DEVELOPMENT_TEAM}" CACHE STRING
    "Apple development team used for automatic iOS code signing" FORCE)
endif()
set(OPENWOW_IOS_DEVICE "" CACHE STRING
  "Device name or identifier used by the incremental iOS Data sync target")
set(OPENWOW_IOS_TEXTURE_CACHE_LOCALE "zhCN" CACHE STRING
  "Retail locale used to resolve the offline iOS ASTC texture cache")
set(OPENWOW_IOS_TEXTURE_CACHE_TOOL
  "${PROJECT_SOURCE_DIR}/build/release/tools/ios_texture_cache/openwow-ios-texture-cache"
  CACHE FILEPATH
  "Native host tool used to prepare the iOS ASTC texture cache")

option(OPENWOW_ENABLE_STREAMING_TELEMETRY
  "Send stock's streaming-install tracker announce over plaintext HTTP" OFF)
option(OPENWOW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(OPENWOW_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)

function(openwow_configure_target target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "openwow_configure_target: unknown target ${target}")
  endif()

  target_compile_features("${target}" PUBLIC cxx_std_20)

  target_compile_options("${target}" PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang>:
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wformat=2
      -Wundef
      -Wnon-virtual-dtor
      -Woverloaded-virtual
    >
    $<$<CXX_COMPILER_ID:GNU>:
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wformat=2
      -Wundef
      -Wnon-virtual-dtor
      -Woverloaded-virtual
    >

    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
  )

  target_compile_definitions("${target}" PRIVATE
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>
  )

  if(OPENWOW_WARNINGS_AS_ERRORS)
    target_compile_options("${target}" PRIVATE
      $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Werror>
      $<$<CXX_COMPILER_ID:MSVC>:/WX>
    )
  endif()

  if(OPENWOW_ENABLE_CLANG_TIDY)
    find_program(OPENWOW_CLANG_TIDY NAMES clang-tidy REQUIRED)
    set_property(TARGET "${target}" PROPERTY CXX_CLANG_TIDY
      "${OPENWOW_CLANG_TIDY}"
      "--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
    )
  endif()
endfunction()

function(openwow_link_apple_framework target framework_name)
  find_library(OPENWOW_FRAMEWORK_${framework_name} ${framework_name} REQUIRED)
  get_filename_component(OPENWOW_FRAMEWORKS_DIR
    "${OPENWOW_FRAMEWORK_${framework_name}}" DIRECTORY)
  target_include_directories(${target} SYSTEM PRIVATE
    "${OPENWOW_FRAMEWORKS_DIR}"
  )

  target_link_libraries(${target} PRIVATE "-framework ${framework_name}")
endfunction()
