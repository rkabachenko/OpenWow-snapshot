cmake_minimum_required(VERSION 3.24)

file(GLOB_RECURSE OPENWOW_VCPKG_OVERLAY_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-overlay-ports/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-overlay-triplets/*"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${OPENWOW_VCPKG_OVERLAY_FILES})

project(OpenWoW VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
  if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
  endif()
  if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/ProjectOptions.cmake)

include(cmake/BuildOptimization.cmake)

include(CTest)
find_package(Freetype REQUIRED)
find_package(JPEG REQUIRED)
find_package(EXPAT REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(bgfx CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(ZLIB REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

add_library(openwow_ffmpeg INTERFACE)
target_include_directories(openwow_ffmpeg SYSTEM INTERFACE
  ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(openwow_ffmpeg INTERFACE
  ${FFMPEG_LIBRARY_DIRS}
)

set(OPENWOW_FFMPEG_LINK_LIBRARIES "")
set(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_LIB OFF)
foreach(FFMPEG_LINK_ITEM IN LISTS FFMPEG_LINK_LIBRARIES)
  if(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_LIB)
    list(APPEND OPENWOW_FFMPEG_LINK_LIBRARIES
      "$<LINK_LIBRARY:FRAMEWORK,${FFMPEG_LINK_ITEM}>"
    )
    set(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_LIB OFF)
  elseif(FFMPEG_LINK_ITEM STREQUAL "-framework")
    set(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_LIB ON)
  else()
    list(APPEND OPENWOW_FFMPEG_LINK_LIBRARIES "${FFMPEG_LINK_ITEM}")
  endif()
endforeach()
if(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_LIB)
  message(FATAL_ERROR "FFmpeg pkg-config data contains an incomplete -framework library entry")
endif()
list(REMOVE_DUPLICATES OPENWOW_FFMPEG_LINK_LIBRARIES)
target_link_libraries(openwow_ffmpeg INTERFACE
  ${OPENWOW_FFMPEG_LINK_LIBRARIES}
)
target_compile_options(openwow_ffmpeg INTERFACE
  ${FFMPEG_CFLAGS_OTHER}
)

set(OPENWOW_FFMPEG_LINK_OPTIONS "")
set(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_NAME OFF)
foreach(FFMPEG_LINK_OPTION IN LISTS FFMPEG_LDFLAGS_OTHER)
  if(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_NAME)
    list(APPEND OPENWOW_FFMPEG_LINK_OPTIONS
      "LINKER:SHELL:-framework ${FFMPEG_LINK_OPTION}"
    )
    set(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_NAME OFF)
  elseif(FFMPEG_LINK_OPTION STREQUAL "-framework")
    set(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_NAME ON)
  else()
    list(APPEND OPENWOW_FFMPEG_LINK_OPTIONS "${FFMPEG_LINK_OPTION}")
  endif()
endforeach()
if(OPENWOW_FFMPEG_EXPECT_FRAMEWORK_NAME)
  message(FATAL_ERROR "FFmpeg pkg-config data contains an incomplete -framework option")
endif()

list(REMOVE_DUPLICATES OPENWOW_FFMPEG_LINK_OPTIONS)
target_link_options(openwow_ffmpeg INTERFACE
  ${OPENWOW_FFMPEG_LINK_OPTIONS}
)
pkg_check_modules(PCRE REQUIRED IMPORTED_TARGET libpcre)

add_subdirectory(third_party/wow_lua)

set(OPENWOW_STORMLIB_TARGET "")
if(OPENWOW_ENABLE_MPQ_VFS)

  find_package(BZip2 REQUIRED)

  find_package(stormlib CONFIG QUIET)
  if(stormlib_FOUND)
    set(OPENWOW_HAS_STORMLIB ON)
    set(OPENWOW_STORMLIB_TARGET stormlib::stormlib)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/StormLib/CMakeLists.txt")
    add_subdirectory(third_party/StormLib EXCLUDE_FROM_ALL)
    set(OPENWOW_HAS_STORMLIB ON)
    set(OPENWOW_STORMLIB_TARGET storm)
    message(STATUS "StormLib package not found; using bundled third_party/StormLib")
  else()
    set(OPENWOW_HAS_STORMLIB OFF)
    message(WARNING "StormLib not found: MPQ VFS disabled; stock MPQ-only installs will fail at startup.")
  endif()
else()
  set(OPENWOW_HAS_STORMLIB OFF)
endif()

add_subdirectory(src/openwow)

if(OPENWOW_BUILD_CLIENT)
  add_subdirectory(apps/client)
endif()

include(cmake/Packaging.cmake)
