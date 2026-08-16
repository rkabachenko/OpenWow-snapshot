add_library(openwow_platform_dynamic_libraries
  dynamic_library.cpp
)

target_include_directories(openwow_platform_dynamic_libraries
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(openwow_platform_dynamic_libraries
  PRIVATE ${CMAKE_DL_LIBS}
)
openwow_configure_target(openwow_platform_dynamic_libraries)
