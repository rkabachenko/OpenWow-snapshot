add_library(openwow_data_formats
  dbc/dbc_file.cpp
  dbc/dbc_structures.cpp
  dbc/dbc_entries_extended.cpp
  dbc/dbc_entries_gameplay.cpp
  dbc/dbc_entries_world.cpp
  dbc/dbc_retail_catalog.cpp
)

target_include_directories(openwow_data_formats
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_data_formats)
