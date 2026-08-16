
option(OPENWOW_ENABLE_THINLTO
  "Compile and link our own targets with ThinLTO (clang) / whole-program optimization (MSVC, GCC)"
  ON)

set(OPENWOW_PGO_MODE "off" CACHE STRING
  "Profile-guided optimization mode: off, generate (instrumented build), or use (consume a merged profile)")
set_property(CACHE OPENWOW_PGO_MODE PROPERTY STRINGS off generate use)

set(OPENWOW_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH
  "Directory the instrumented binary writes raw profiles into (OPENWOW_PGO_MODE=generate). Baked into the binary as an absolute path, so profiles land here regardless of the client's working directory.")

set(OPENWOW_PGO_PROFILE_DATA "" CACHE FILEPATH
  "Merged profile consumed by OPENWOW_PGO_MODE=use. Empty selects the per-toolchain default under OPENWOW_PGO_PROFILE_DIR.")

set(OPENWOW_THINLTO_LINKER "auto" CACHE STRING
  "Linker to drive the ThinLTO link with on ELF targets: auto, lld, gold, or default (leave the toolchain default alone)")
set_property(CACHE OPENWOW_THINLTO_LINKER PROPERTY STRINGS auto lld gold default)

set(OPENWOW_THINLTO_CACHE_DIR "${CMAKE_BINARY_DIR}/thinlto-cache" CACHE PATH
  "ThinLTO incremental link cache. Lets a relink after a one-file change re-optimize only the affected modules.")

macro(openwow_select_elf_lto_linker)
  set(_openwow_lto_linker "${OPENWOW_THINLTO_LINKER}")
  if(_openwow_lto_linker STREQUAL "auto")
    find_program(OPENWOW_LD_LLD NAMES ld.lld lld)
    find_program(OPENWOW_LD_GOLD NAMES ld.gold)
    if(OPENWOW_LD_LLD)
      set(_openwow_lto_linker "lld")
    elseif(OPENWOW_LD_GOLD)
      set(_openwow_lto_linker "gold")
    else()
      set(_openwow_lto_linker "default")
    endif()
  endif()

  file(MAKE_DIRECTORY "${OPENWOW_THINLTO_CACHE_DIR}")
  if(_openwow_lto_linker STREQUAL "lld")
    list(APPEND OPENWOW_OPT_LINK_OPTIONS
      "-fuse-ld=lld"
      "LINKER:--thinlto-cache-dir=${OPENWOW_THINLTO_CACHE_DIR}"
    )
    set(OPENWOW_OPT_LTO_LINKER_DESC "lld, incremental cache on")
  elseif(_openwow_lto_linker STREQUAL "gold")
    list(APPEND OPENWOW_OPT_LINK_OPTIONS
      "-fuse-ld=gold"
      "LINKER:-plugin-opt,cache-dir=${OPENWOW_THINLTO_CACHE_DIR}"
    )
    set(OPENWOW_OPT_LTO_LINKER_DESC "gold + LLVMgold plugin, incremental cache on")
  else()

    set(OPENWOW_OPT_LTO_LINKER_DESC "toolchain default linker, no incremental cache")
    message(WARNING
      "OPENWOW_ENABLE_THINLTO: neither ld.lld nor ld.gold was found (or "
      "OPENWOW_THINLTO_LINKER=default). The default linker must have been built with LLVM "
      "bitcode plugin support or the link will fail, and ThinLTO's incremental cache is "
      "unavailable. Install lld and re-configure for the supported path.")
  endif()
endmacro()

macro(openwow_require_lto_archiver _ar_name _ranlib_name)
  get_filename_component(_openwow_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

  find_program(OPENWOW_LTO_AR_${_ar_name} NAMES ${_ar_name}
    HINTS "${_openwow_compiler_dir}")
  find_program(OPENWOW_LTO_RANLIB_${_ranlib_name} NAMES ${_ranlib_name}
    HINTS "${_openwow_compiler_dir}")
  set(OPENWOW_LTO_AR "${OPENWOW_LTO_AR_${_ar_name}}")
  set(OPENWOW_LTO_RANLIB "${OPENWOW_LTO_RANLIB_${_ranlib_name}}")
  if(NOT OPENWOW_LTO_AR)
    message(FATAL_ERROR
      "OPENWOW_ENABLE_THINLTO requires ${_ar_name} to archive the project's static libraries: "
      "under LTO their members are compiler IR, which the default archiver cannot index. "
      "Install it (LLVM/binutils toolchain) or turn OPENWOW_ENABLE_THINLTO off.")
  endif()
  set(CMAKE_AR "${OPENWOW_LTO_AR}")
  if(OPENWOW_LTO_RANLIB)
    set(CMAKE_RANLIB "${OPENWOW_LTO_RANLIB}")
  endif()
  set(OPENWOW_OPT_LTO_ARCHIVER_DESC "${_ar_name}")
endmacro()

macro(openwow_append_static_linker_flag _flag)
  if(NOT CMAKE_STATIC_LINKER_FLAGS MATCHES "${_flag}")
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} ${_flag}")
  endif()
endmacro()

macro(openwow_msvc_force_ltcg_for_pgo)
  if(NOT "/GL" IN_LIST OPENWOW_OPT_COMPILE_OPTIONS)
    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "/GL")
  endif()
  if(NOT "/LTCG" IN_LIST OPENWOW_OPT_LINK_OPTIONS)
    list(APPEND OPENWOW_OPT_LINK_OPTIONS "/LTCG")
  endif()
  openwow_append_static_linker_flag("/LTCG")
endmacro()

if(NOT OPENWOW_PGO_MODE MATCHES "^(off|generate|use)$")
  message(FATAL_ERROR
    "OPENWOW_PGO_MODE must be one of: off, generate, use (got \"${OPENWOW_PGO_MODE}\").")
endif()

if(NOT OPENWOW_THINLTO_LINKER MATCHES "^(auto|lld|gold|default)$")
  message(FATAL_ERROR
    "OPENWOW_THINLTO_LINKER must be one of: auto, lld, gold, default (got \"${OPENWOW_THINLTO_LINKER}\").")
endif()

if(OPENWOW_ENABLE_THINLTO AND CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  message(FATAL_ERROR
    "OPENWOW_ENABLE_THINLTO and CMAKE_INTERPROCEDURAL_OPTIMIZATION both drive link-time "
    "optimization. Pick one: unset CMAKE_INTERPROCEDURAL_OPTIMIZATION and use "
    "OPENWOW_ENABLE_THINLTO (which additionally wires the ThinLTO cache, the archiver and "
    "the PGO modes), or turn OPENWOW_ENABLE_THINLTO off.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(OPENWOW_OPT_TOOLCHAIN "clang-cl")
  else()
    set(OPENWOW_OPT_TOOLCHAIN "clang-gnu")
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(OPENWOW_OPT_TOOLCHAIN "msvc")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(OPENWOW_OPT_TOOLCHAIN "gnu")
else()
  set(OPENWOW_OPT_TOOLCHAIN "unsupported")
endif()

if(OPENWOW_OPT_TOOLCHAIN STREQUAL "unsupported"
   AND (OPENWOW_ENABLE_THINLTO OR NOT OPENWOW_PGO_MODE STREQUAL "off"))
  message(FATAL_ERROR
    "OPENWOW_ENABLE_THINLTO / OPENWOW_PGO_MODE are implemented for Clang, AppleClang, "
    "clang-cl, MSVC and GCC. This build uses CMAKE_CXX_COMPILER_ID=\"${CMAKE_CXX_COMPILER_ID}\". "
    "Turn both off, or extend cmake/BuildOptimization.cmake.")
endif()

if(OPENWOW_PGO_PROFILE_DATA)
  set(OPENWOW_PGO_PROFILE_DATA_RESOLVED "${OPENWOW_PGO_PROFILE_DATA}")
elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "msvc")
  set(OPENWOW_PGO_PROFILE_DATA_RESOLVED "${OPENWOW_PGO_PROFILE_DIR}/openwow-client.pgd")
elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "gnu")
  set(OPENWOW_PGO_PROFILE_DATA_RESOLVED "${OPENWOW_PGO_PROFILE_DIR}")
else()
  set(OPENWOW_PGO_PROFILE_DATA_RESOLVED "${OPENWOW_PGO_PROFILE_DIR}/openwow.profdata")
endif()

if(OPENWOW_PGO_MODE STREQUAL "use")
  set(OPENWOW_PGO_MISSING_PROFILE OFF)
  if(NOT EXISTS "${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")
    set(OPENWOW_PGO_MISSING_PROFILE ON)
  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "gnu")

    file(GLOB OPENWOW_PGO_GCDA_FILES "${OPENWOW_PGO_PROFILE_DATA_RESOLVED}/*.gcda")
    if(NOT OPENWOW_PGO_GCDA_FILES)
      set(OPENWOW_PGO_MISSING_PROFILE ON)
    endif()
  endif()

  if(OPENWOW_PGO_MISSING_PROFILE)

    if(OPENWOW_OPT_TOOLCHAIN STREQUAL "gnu")
      set(OPENWOW_PGO_MERGE_HINT
        "3. Nothing to merge -- GCC's -fprofile-use consumes the directory of .gcda\n     files the instrumented build wrote.")
    elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "msvc")
      set(OPENWOW_PGO_MERGE_HINT
        "3. Merge the .pgc counter files the instrumented runs produced:\n     pgomgr /merge \"${OPENWOW_PGO_PROFILE_DIR}\" \"${OPENWOW_PGO_PROFILE_DATA_RESOLVED}\"")
    else()
      set(OPENWOW_PGO_MERGE_HINT
        "3. Merge the raw profiles the instrumented runs wrote:\n     llvm-profdata merge -output=${OPENWOW_PGO_PROFILE_DATA_RESOLVED} ${OPENWOW_PGO_PROFILE_DIR}/*.profraw")
    endif()
    message(FATAL_ERROR
      "OPENWOW_PGO_MODE=use, but no profile data exists at\n"
      "  ${OPENWOW_PGO_PROFILE_DATA_RESOLVED}\n"
      "Configuring `use` without a profile would silently produce an ordinary build, so this is a hard configure failure rather than a warning.\n"
      "\n"
      "Produce the profile first:\n"
      "  1. Configure and build a separate tree with -DOPENWOW_PGO_MODE=generate\n"
      "  2. Run the client through the representative scenes\n"
      "  ${OPENWOW_PGO_MERGE_HINT}\n"
      "Then re-run this configure, or point it at an existing profile with\n"
      "  -DOPENWOW_PGO_PROFILE_DATA=/path/to/profile\n"
      "\n"
      "Full workflow: docs/build_optimization.md")
  endif()
endif()

set(OPENWOW_OPT_COMPILE_OPTIONS "")
set(OPENWOW_OPT_LINK_OPTIONS "")
set(OPENWOW_OPT_SUMMARY "")
set(OPENWOW_OPT_LTO_ARCHIVER_DESC "toolchain default archiver")
set(OPENWOW_OPT_LTO_LINKER_DESC "")

if(OPENWOW_ENABLE_THINLTO)
  if(OPENWOW_OPT_TOOLCHAIN STREQUAL "clang-gnu")
    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "-flto=thin")

    list(APPEND OPENWOW_OPT_LINK_OPTIONS "-flto=thin")

    if(APPLE)

      file(MAKE_DIRECTORY "${OPENWOW_THINLTO_CACHE_DIR}")
      list(APPEND OPENWOW_OPT_LINK_OPTIONS
        "LINKER:-cache_path_lto,${OPENWOW_THINLTO_CACHE_DIR}"
      )

      set(OPENWOW_OPT_LTO_ARCHIVER_DESC "cctools ar/ranlib (bitcode-aware)")
      set(OPENWOW_OPT_LTO_LINKER_DESC "ld64, incremental cache on")
    else()
      openwow_select_elf_lto_linker()
      openwow_require_lto_archiver(llvm-ar llvm-ranlib)
    endif()
    list(APPEND OPENWOW_OPT_SUMMARY
      "ThinLTO: -flto=thin | ${OPENWOW_OPT_LTO_LINKER_DESC} | ${OPENWOW_OPT_LTO_ARCHIVER_DESC}")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "clang-cl")

    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "-flto=thin")
    file(MAKE_DIRECTORY "${OPENWOW_THINLTO_CACHE_DIR}")
    list(APPEND OPENWOW_OPT_LINK_OPTIONS
      "LINKER:/lldltocache:${OPENWOW_THINLTO_CACHE_DIR}")
    openwow_require_lto_archiver(llvm-lib llvm-ranlib)
    list(APPEND OPENWOW_OPT_SUMMARY
      "ThinLTO: -flto=thin | lld-link /lldltocache | ${OPENWOW_OPT_LTO_ARCHIVER_DESC}")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "msvc")

    list(APPEND OPENWOW_OPT_SUMMARY
      "Whole-program optimization: OFF on MSVC (/GL objects overrun lib.exe's 4 GB archive limit)")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "gnu")

    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "-flto=auto")
    list(APPEND OPENWOW_OPT_LINK_OPTIONS "-flto=auto")
    openwow_require_lto_archiver(gcc-ar gcc-ranlib)
    list(APPEND OPENWOW_OPT_SUMMARY
      "LTO: -flto=auto | ${OPENWOW_OPT_LTO_ARCHIVER_DESC} (GCC has no ThinLTO; WHOPR is the analogue)")
  endif()
endif()

if(OPENWOW_PGO_MODE STREQUAL "generate")
  file(MAKE_DIRECTORY "${OPENWOW_PGO_PROFILE_DIR}")

  if(OPENWOW_OPT_TOOLCHAIN MATCHES "^clang")

    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "-fprofile-generate=${OPENWOW_PGO_PROFILE_DIR}")

    list(APPEND OPENWOW_OPT_LINK_OPTIONS "-fprofile-generate=${OPENWOW_PGO_PROFILE_DIR}")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "msvc")
    openwow_msvc_force_ltcg_for_pgo()
    list(APPEND OPENWOW_OPT_LINK_OPTIONS
      "/GENPROFILE:PGD=${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "gnu")
    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "-fprofile-generate=${OPENWOW_PGO_PROFILE_DIR}")
    list(APPEND OPENWOW_OPT_LINK_OPTIONS "-fprofile-generate=${OPENWOW_PGO_PROFILE_DIR}")
  endif()

  list(APPEND OPENWOW_OPT_SUMMARY
    "PGO: instrumented build, profiles -> ${OPENWOW_PGO_PROFILE_DIR}")

elseif(OPENWOW_PGO_MODE STREQUAL "use")
  if(OPENWOW_OPT_TOOLCHAIN MATCHES "^clang")
    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS "-fprofile-use=${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")

    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS
      "-Wno-profile-instr-unprofiled"
      "-Wno-profile-instr-out-of-date"
    )
    list(APPEND OPENWOW_OPT_LINK_OPTIONS "-fprofile-use=${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "msvc")
    openwow_msvc_force_ltcg_for_pgo()
    list(APPEND OPENWOW_OPT_LINK_OPTIONS
      "/USEPROFILE:PGD=${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")

  elseif(OPENWOW_OPT_TOOLCHAIN STREQUAL "gnu")
    list(APPEND OPENWOW_OPT_COMPILE_OPTIONS
      "-fprofile-use=${OPENWOW_PGO_PROFILE_DATA_RESOLVED}"

      "-Wno-missing-profile"
      "-fprofile-correction"
    )
    list(APPEND OPENWOW_OPT_LINK_OPTIONS
      "-fprofile-use=${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")
  endif()

  list(APPEND OPENWOW_OPT_SUMMARY "PGO: using ${OPENWOW_PGO_PROFILE_DATA_RESOLVED}")
endif()

if(OPENWOW_OPT_COMPILE_OPTIONS)
  add_compile_options(${OPENWOW_OPT_COMPILE_OPTIONS})
endif()
if(OPENWOW_OPT_LINK_OPTIONS)
  add_link_options(${OPENWOW_OPT_LINK_OPTIONS})
endif()

if(OPENWOW_OPT_SUMMARY)
  foreach(OPENWOW_OPT_SUMMARY_LINE IN LISTS OPENWOW_OPT_SUMMARY)
    message(STATUS "OpenWoW optimization -- ${OPENWOW_OPT_SUMMARY_LINE}")
  endforeach()
  message(STATUS
    "OpenWoW optimization -- vcpkg dependencies are prebuilt native archives and do not "
    "participate in LTO/PGO; these flags cover this project's own targets only.")
  if(OPENWOW_ENABLE_THINLTO)

    message(STATUS
      "OpenWoW optimization -- expect the openwow-client link to go from seconds to "
      "roughly 1-4 minutes on a cold ThinLTO cache, with peak link memory several GB "
      "higher. Warm-cache relinks after a single-file edit are far cheaper. "
      "Cache: ${OPENWOW_THINLTO_CACHE_DIR}")
  endif()
endif()
