add_library(openwow_ui_display_settings
  display_settings_service.cpp
)

target_include_directories(openwow_ui_display_settings
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(openwow_ui_display_settings PUBLIC cxx_std_20)
