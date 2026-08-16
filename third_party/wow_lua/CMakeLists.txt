add_library(openwow_lua STATIC
  src/openwow_client_crt_random.cpp
  src/lapi.c
  src/lauxlib.c
  src/lbaselib.c
  src/lcode.c
  src/ldblib.c
  src/ldebug.c
  src/ldo.c
  src/ldump.c
  src/lfunc.c
  src/lgc.c
  src/linit.c
  src/liolib.c
  src/llex.c
  src/lmathlib.c
  src/lmem.c
  src/loadlib.c
  src/lobject.c
  src/lopcodes.c
  src/loslib.c
  src/lparser.c
  src/lstate.c
  src/lstring.c
  src/lstrlib.c
  src/ltable.c
  src/ltablib.c
  src/ltm.c
  src/lundump.c
  src/lvm.c
  src/lzio.c
)

target_include_directories(openwow_lua PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(openwow_lua PRIVATE
  $<$<NOT:$<PLATFORM_ID:Windows>>:LUA_USE_POSIX>
  $<$<CONFIG:Debug>:LUA_USE_APICHECK>
)

if(UNIX)
  target_link_libraries(openwow_lua PUBLIC m)
  if(NOT APPLE)
    target_link_libraries(openwow_lua PUBLIC dl)
  endif()
endif()
