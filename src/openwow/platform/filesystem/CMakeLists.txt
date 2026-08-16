add_library(openwow_platform_filesystem
  filesystem.cpp
)

target_include_directories(openwow_platform_filesystem
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_platform_filesystem)
