# Connects upstream SKL's capability-filtered test catalog to the RV64 HTIF runtime.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.23)
project(rhodium-skl LANGUAGES C ASM)
set(CMAKE_C_STANDARD 23)
# Upstream's extension probe consumes SKL_COMPILE_OPTIONS, not CMAKE_C_FLAGS.
separate_arguments(SKL_COMPILE_OPTIONS NATIVE_COMMAND "${CMAKE_C_FLAGS}")

set(SKL_ENABLE_TESTS ON CACHE BOOL "Retain upstream test inputs as execution workloads" FORCE)
set(SKL_ENABLE_BENCHMARKS ON CACHE BOOL "Build upstream benchmark cases" FORCE)
set(SKL_ENABLE_CLANG_TIDY OFF CACHE BOOL "No host lint tool in target builds" FORCE)
set(SKL_ADD_TEST_FUNC rhodium_skl_add_test CACHE STRING "Bare-metal test integration" FORCE)

add_library(rhodium-skl-runtime OBJECT start.S runtime.c)

# Retain upstream's inventory and extension requirements, excluding the two
# explicitly unsupported workload categories rather than a kernel allowlist.
function(rhodium_skl_add_test NAME HARNESS TEST REQUIRED_EXTENSIONS OPTIONS)
  if(NAME MATCHES "(_x390|_ref)$")
    return()
  endif()
  foreach(EXT IN LISTS REQUIRED_EXTENSIONS)
    assert_extension_valid(${EXT})
    if(NOT EXT IN_LIST RISCV_EXTENSIONS)
      return()
    endif()
  endforeach()
  add_executable(${NAME} "${SKL_SOURCE}/test/${HARNESS}" "${SKL_SOURCE}/test/${TEST}")
  target_include_directories(${NAME} PRIVATE "${SKL_SOURCE}/test")
  target_compile_definitions(${NAME} PRIVATE SKL_ENABLE_TESTS SKL_ENABLE_BENCHMARKS)
  target_compile_options(${NAME} PRIVATE ${OPTIONS})
  target_link_libraries(${NAME} PRIVATE skl skl-ref skl-test-driver rhodium-skl-runtime
    -Wl,--start-group c m gcc -Wl,--end-group)
  target_link_options(${NAME} PRIVATE -nostdlib -nostartfiles -no-pie
    "-Wl,-T,${SKL_LINKER}" -Wl,--gc-sections -Wl,--no-warn-rwx-segments)
  set_property(TARGET ${NAME} APPEND PROPERTY LINK_DEPENDS "${SKL_LINKER}")
  set_property(GLOBAL APPEND PROPERTY RHODIUM_SKL_TESTS ${NAME})
endfunction()

add_subdirectory("${SKL_SOURCE}" upstream)

# Upstream still lists tuned implementations in libskl when their ISA matches.
# Exclude those sources as well as their executable suites. Upstream tables
# still reference skl-ref symbols, but the driver never executes their oracles.
get_target_property(KERNEL_SOURCES skl SOURCES)
list(FILTER KERNEL_SOURCES EXCLUDE REGEX "_x390\\.c$")
set_property(TARGET skl PROPERTY SOURCES "${KERNEL_SOURCES}")
# GCC safely ignores Clang's unroll hints, but upstream enables -Werror.
target_compile_options(skl PRIVATE -Wno-unknown-pragmas)

get_property(TEST_NAMES GLOBAL PROPERTY RHODIUM_SKL_TESTS)
if(NOT TEST_NAMES)
  message(FATAL_ERROR "SKL selected no vector suites for this target")
endif()
list(SORT TEST_NAMES)
set(INVENTORY "")
foreach(NAME IN LISTS TEST_NAMES)
  string(APPEND INVENTORY "${NAME}\t$<TARGET_FILE:${NAME}>\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/inventory.tsv" CONTENT "${INVENTORY}")
