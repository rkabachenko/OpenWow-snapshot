add_subdirectory(diagnostics)
add_subdirectory(text)

add_library(openwow_foundation
  hashing/retail_adler_seed.cpp
  hashing/retail_sha1.cpp
  math/loose_octree.cpp
)

target_include_directories(openwow_foundation
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_foundation)
