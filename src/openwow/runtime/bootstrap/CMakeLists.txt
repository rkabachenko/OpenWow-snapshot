add_library(openwow_runtime_bootstrap
  startup_trace.cpp
)

target_include_directories(openwow_runtime_bootstrap
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_runtime_bootstrap)
