# uam (deko3d's GLSL -> DKSH compiler) as a static library for the Switch, plus the C bridge
# (uam_bridge.cpp). Mirrors uam's meson.build minus source/main.cpp (adapted from the nsmbw port).
cmake_minimum_required(VERSION 3.16)
project(uamcore C CXX)

set(UAM_ROOT "" CACHE PATH "patched uam checkout")
set(UAM_BRIDGE "${CMAKE_CURRENT_LIST_DIR}/../uam-bridge")
find_program(UAM_BISON NAMES bison PATHS /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin NO_DEFAULT_PATH)
find_program(UAM_FLEX NAMES flex REQUIRED)
if(NOT UAM_BISON)
  message(FATAL_ERROR "bison 3 is needed (Homebrew: brew install bison)")
endif()
if(NOT UAM_PYTHON)
  message(FATAL_ERROR "UAM_PYTHON (a python with mako) is needed")
endif()

set(_gen "${CMAKE_BINARY_DIR}/mesa-imported")
file(MAKE_DIRECTORY "${_gen}/glsl/glcpp")

add_custom_command(
  OUTPUT "${_gen}/glsl/glsl_parser.cpp" "${_gen}/glsl/glsl_parser.h"
  COMMAND "${UAM_BISON}" -o "${_gen}/glsl/glsl_parser.cpp" -p _mesa_glsl_
          --defines=${_gen}/glsl/glsl_parser.h "${UAM_ROOT}/mesa-imported/glsl/glsl_parser.yy"
  DEPENDS "${UAM_ROOT}/mesa-imported/glsl/glsl_parser.yy")
add_custom_command(
  OUTPUT "${_gen}/glsl/glsl_lexer.cpp"
  COMMAND "${UAM_FLEX}" -o "${_gen}/glsl/glsl_lexer.cpp" "${UAM_ROOT}/mesa-imported/glsl/glsl_lexer.ll"
  DEPENDS "${UAM_ROOT}/mesa-imported/glsl/glsl_lexer.ll")
add_custom_command(
  OUTPUT "${_gen}/glsl/glcpp/glcpp-parse.c" "${_gen}/glsl/glcpp/glcpp-parse.h"
  COMMAND "${UAM_BISON}" -o "${_gen}/glsl/glcpp/glcpp-parse.c" -p glcpp_parser_
          --defines=${_gen}/glsl/glcpp/glcpp-parse.h "${UAM_ROOT}/mesa-imported/glsl/glcpp/glcpp-parse.y"
  DEPENDS "${UAM_ROOT}/mesa-imported/glsl/glcpp/glcpp-parse.y")
add_custom_command(
  OUTPUT "${_gen}/glsl/glcpp/glcpp-lex.c"
  COMMAND "${UAM_FLEX}" -o "${_gen}/glsl/glcpp/glcpp-lex.c" "${UAM_ROOT}/mesa-imported/glsl/glcpp/glcpp-lex.l"
  DEPENDS "${UAM_ROOT}/mesa-imported/glsl/glcpp/glcpp-lex.l")
set(_generated
  "${_gen}/glsl/glsl_parser.cpp" "${_gen}/glsl/glsl_lexer.cpp"
  "${_gen}/glsl/glcpp/glcpp-parse.c" "${_gen}/glsl/glcpp/glcpp-lex.c")
foreach(_variant enum constant strings)
  if(_variant STREQUAL "enum")
    set(_out "${_gen}/glsl/ir_expression_operation.h")
  else()
    set(_out "${_gen}/glsl/ir_expression_operation_${_variant}.h")
  endif()
  add_custom_command(
    OUTPUT "${_out}"
    COMMAND "${UAM_PYTHON}" "${UAM_ROOT}/mesa-imported/glsl/ir_expression_operation.py" ${_variant} > "${_out}"
    DEPENDS "${UAM_ROOT}/mesa-imported/glsl/ir_expression_operation.py")
  list(APPEND _generated "${_out}")
endforeach()

file(GLOB _codegen "${UAM_ROOT}/mesa-imported/codegen/*.cpp")
file(GLOB _tgsi "${UAM_ROOT}/mesa-imported/tgsi/*.c")
file(GLOB _util "${UAM_ROOT}/mesa-imported/util/*.c")
file(GLOB _cso "${UAM_ROOT}/mesa-imported/cso_cache/*.c")
file(GLOB _glsl "${UAM_ROOT}/mesa-imported/glsl/*.cpp")
file(GLOB _compiler "${UAM_ROOT}/mesa-imported/compiler/*.c" "${UAM_ROOT}/mesa-imported/compiler/*.cpp")
file(GLOB _program "${UAM_ROOT}/mesa-imported/program/*.c" "${UAM_ROOT}/mesa-imported/program/*.cpp")
file(GLOB _st "${UAM_ROOT}/mesa-imported/state_tracker/*.c" "${UAM_ROOT}/mesa-imported/state_tracker/*.cpp")
file(GLOB _main "${UAM_ROOT}/mesa-imported/main/*.c" "${UAM_ROOT}/mesa-imported/main/*.cpp")

add_library(uamcore STATIC
  "${UAM_ROOT}/source/compiler_iface.cpp"
  "${UAM_ROOT}/source/glsl_frontend.cpp"
  "${UAM_ROOT}/source/mini-os.c"
  "${UAM_ROOT}/source/tgsi_support.cpp"
  "${UAM_ROOT}/mesa-imported/glsl/glcpp/pp.c"
  "${UAM_BRIDGE}/uam_bridge.cpp"
  ${_codegen} ${_tgsi} ${_util} ${_cso} ${_glsl} ${_compiler} ${_program} ${_st} ${_main}
  ${_generated})
target_include_directories(uamcore PRIVATE
  "${_gen}" "${_gen}/glsl" "${_gen}/glsl/glcpp"
  "${UAM_ROOT}" "${UAM_ROOT}/mesa-imported" "${UAM_ROOT}/mesa-imported/glsl"
  "${UAM_ROOT}/mesa-imported/glsl/glcpp" "${UAM_ROOT}/source" "${UAM_BRIDGE}")
target_compile_definitions(uamcore PRIVATE PACKAGE_STRING="uam 1.1.0" DESKTOP _USE_MATH_DEFINES
  _GNU_SOURCE HAVE_POSIX_MEMALIGN NDEBUG)
set_target_properties(uamcore PROPERTIES C_STANDARD 99 C_EXTENSIONS ON CXX_STANDARD 11 CXX_EXTENSIONS ON)
target_compile_options(uamcore PRIVATE -w -O2 -ffunction-sections -fdata-sections)
