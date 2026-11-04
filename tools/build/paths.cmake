# Keeps compiler-expanded filenames stable for repository-owned native CMake builds.
# SPDX-License-Identifier: Apache-2.0
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/paths.py")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/paths.py"
          --build "${CMAKE_BINARY_DIR}"
  OUTPUT_VARIABLE rhodium_path_flags
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" rhodium_path_flags "${rhodium_path_flags}")
add_compile_options(${rhodium_path_flags})
