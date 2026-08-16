
function(openwow_compile_shader)
  cmake_parse_arguments(
    PARSE_ARGV 0 ARG
    ""
    "TYPE;SHADER;VARYING;OUTPUT_DIR;OUT_FILES_VAR;BIN2C_BASENAME"
    "INCLUDE_DIRS;DEFINES"
  )

  if(NOT ARG_TYPE)
    message(FATAL_ERROR "openwow_compile_shader: TYPE is required (VERTEX or FRAGMENT)")
  endif()
  if(NOT ARG_SHADER)
    message(FATAL_ERROR "openwow_compile_shader: SHADER is required")
  endif()
  if(NOT ARG_OUTPUT_DIR)
    set(ARG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
  endif()

  string(TOUPPER "${ARG_TYPE}" _TYPE_UPPER)

  get_filename_component(_SHADER_BASENAME "${ARG_SHADER}" NAME)
  get_filename_component(_SHADER_NAME_WE  "${ARG_SHADER}" NAME_WE)
  get_filename_component(_SHADER_ABSOLUTE "${ARG_SHADER}" ABSOLUTE)
  get_filename_component(_SHADER_DIRECTORY "${_SHADER_ABSOLUTE}" DIRECTORY)
  file(GLOB _LOCAL_SHADER_INCLUDES CONFIGURE_DEPENDS
       "${_SHADER_DIRECTORY}/*.sh")

  if(DEFINED ARG_BIN2C_BASENAME AND NOT ARG_BIN2C_BASENAME STREQUAL "")
    set(_BIN2C_STEM "${ARG_BIN2C_BASENAME}")
  else()
    set(_BIN2C_STEM "${_SHADER_NAME_WE}")
  endif()

  set(_PROFILE_NAMES   120     300_es   spirv  metal)
  set(_PROFILE_PLATS   LINUX   ANDROID  LINUX  OSX)

  if(WIN32 OR MINGW OR MSYS OR CYGWIN)
    list(APPEND _PROFILE_NAMES s_5_0)
    list(APPEND _PROFILE_PLATS WINDOWS)
    set(_DX11_LABEL "/dx11")
  else()
    set(_DX11_LABEL "")
  endif()

  set(_ALL_OUTPUTS "")
  set(_COMMANDS "")

  list(LENGTH _PROFILE_NAMES _PROFILE_COUNT)
  math(EXPR _LAST_IDX "${_PROFILE_COUNT} - 1")

  foreach(_I RANGE ${_LAST_IDX})
    list(GET _PROFILE_NAMES ${_I} _PROFILE)
    list(GET _PROFILE_PLATS ${_I} _PLATFORM)

    _bgfx_get_profile_ext(${_PROFILE} _PROFILE_EXT)

    if(DEFINED ARG_BIN2C_BASENAME AND NOT ARG_BIN2C_BASENAME STREQUAL "")
      set(_OUTPUT_STEM "${ARG_BIN2C_BASENAME}")
    else()
      set(_OUTPUT_STEM "${_SHADER_NAME_WE}")
    endif()
    set(_OUTPUT "${ARG_OUTPUT_DIR}/${_OUTPUT_STEM}.sc.${_PROFILE_EXT}.bin.h")

    _bgfx_shaderc_parse(
      _CLI
      ${_TYPE_UPPER} ${_PLATFORM} WERROR "$<$<CONFIG:debug>:DEBUG>$<$<CONFIG:relwithdebinfo>:DEBUG>"
      FILE ${_SHADER_ABSOLUTE}
      OUTPUT ${_OUTPUT}
      PROFILE ${_PROFILE}
      O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
      VARYINGDEF ${ARG_VARYING}
      INCLUDES ${BGFX_SHADER_INCLUDE_PATH} ${ARG_INCLUDE_DIRS}
      BIN2C ${_BIN2C_STEM}_${_PROFILE_EXT}
    )
    if(DEFINED ARG_DEFINES AND NOT "${ARG_DEFINES}" STREQUAL "")
      list(JOIN ARG_DEFINES ";" _SHADER_DEFINES)
      list(APPEND _CLI --define "${_SHADER_DEFINES}")
    endif()

    list(APPEND _ALL_OUTPUTS "${_OUTPUT}")
    list(APPEND _COMMANDS COMMAND bgfx::shaderc ${_CLI})
  endforeach()

  add_custom_command(
    OUTPUT ${_ALL_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUTPUT_DIR}"
    ${_COMMANDS}
    MAIN_DEPENDENCY "${_SHADER_ABSOLUTE}"
    DEPENDS "${ARG_VARYING}" ${_LOCAL_SHADER_INCLUDES}
    COMMENT "Compiling shader ${_SHADER_NAME_WE} (glsl/essl/spv/mtl${_DX11_LABEL})"
  )

  if(DEFINED ARG_OUT_FILES_VAR)
    set(${ARG_OUT_FILES_VAR} ${_ALL_OUTPUTS} PARENT_SCOPE)
  endif()
endfunction()

function(openwow_compile_shader_pair)
  cmake_parse_arguments(
    PARSE_ARGV 0 PAIR
    ""
    "VS;FS;VARYING;OUTPUT_DIR;OUT_VS_VAR;OUT_FS_VAR"
    "INCLUDE_DIRS"
  )

  openwow_compile_shader(
    TYPE VERTEX
    SHADER "${PAIR_VS}"
    VARYING "${PAIR_VARYING}"
    OUTPUT_DIR "${PAIR_OUTPUT_DIR}"
    OUT_FILES_VAR _VS_FILES
    INCLUDE_DIRS ${PAIR_INCLUDE_DIRS}
  )

  openwow_compile_shader(
    TYPE FRAGMENT
    SHADER "${PAIR_FS}"
    VARYING "${PAIR_VARYING}"
    OUTPUT_DIR "${PAIR_OUTPUT_DIR}"
    OUT_FILES_VAR _FS_FILES
    INCLUDE_DIRS ${PAIR_INCLUDE_DIRS}
  )

  if(DEFINED PAIR_OUT_VS_VAR)
    set(${PAIR_OUT_VS_VAR} ${_VS_FILES} PARENT_SCOPE)
  endif()
  if(DEFINED PAIR_OUT_FS_VAR)
    set(${PAIR_OUT_FS_VAR} ${_FS_FILES} PARENT_SCOPE)
  endif()
endfunction()
