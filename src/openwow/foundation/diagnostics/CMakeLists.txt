add_library(openwow_foundation_diagnostics
  error.cpp
  logging.cpp
)

target_include_directories(openwow_foundation_diagnostics
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_foundation_diagnostics)
