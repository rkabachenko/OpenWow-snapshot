add_library(openwow_foundation_text INTERFACE)

target_include_directories(openwow_foundation_text
  INTERFACE ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(openwow_foundation_text
  INTERFACE cxx_std_20
)

add_library(openwow_foundation_text_regex
  retail_regex.cpp
)

target_include_directories(openwow_foundation_text_regex
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(openwow_foundation_text_regex
  PUBLIC openwow_foundation_text
  PRIVATE PkgConfig::PCRE
)
openwow_configure_target(openwow_foundation_text_regex)
