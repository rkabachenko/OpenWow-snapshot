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

# LUA_USE_POSIX must not reach MSVC. luaconf.h turns it into LUA_USE_MKSTEMP /
# LUA_USE_ISATTY / LUA_USE_POPEN / LUA_USE_ULONGJMP, and the first two guard
# `#include <unistd.h>`, which MSVC does not ship: loslib.c died with
# "luaconf.h(298): fatal error C1083: Cannot open include file: 'unistd.h'".
# Upstream Lua 5.1 defines LUA_USE_* per platform for exactly this reason and
# passes nothing on Windows, where luaconf.h derives LUA_WIN from _WIN32 and
# supplies the equivalents itself (_popen/_pclose, _isatty, tmpnam).
target_compile_definitions(openwow_lua PRIVATE
  $<$<NOT:$<PLATFORM_ID:Windows>>:LUA_USE_POSIX>
  $<$<CONFIG:Debug>:LUA_USE_APICHECK>
)

# libm and libdl are Unix libraries. The old else() branch also caught WIN32
# and asked MSVC to link a nonexistent m.lib.
if(UNIX)
  target_link_libraries(openwow_lua PUBLIC m)
  if(NOT APPLE)
    target_link_libraries(openwow_lua PUBLIC dl)
  endif()
endif()
