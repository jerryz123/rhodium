# Compiles repository-owned bounded vector workloads against unmodified Highway kernels.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.23)
project(rhodium-highway LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
add_compile_definitions(HWY_COMPILE_ONLY_STATIC HWY_HAVE_RUNTIME_DISPATCH_RVV=0 HWY_RVV_HAVE_F16_VEC=1
  HWY_OS_RNG_SECURE_SEED=0 PROFILER_ENABLED=0)
add_compile_options(-ffunction-sections -fdata-sections -fno-pie)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-Wno-pragmas>")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include;${CMAKE_CURRENT_SOURCE_DIR}/serial-pool.h>")
include_directories("${HIGHWAY_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}")

set(CORE_SOURCES platform.cc)
foreach(NAME abort aligned_allocator os_rng per_target print profiler targets)
  list(APPEND CORE_SOURCES "${HIGHWAY_SOURCE}/hwy/${NAME}.cc")
endforeach()
add_library(highway-core STATIC ${CORE_SOURCES})
file(GLOB SORT_SOURCES "${HIGHWAY_SOURCE}/hwy/contrib/sort/vqsort*.cc")
add_library(highway-contrib STATIC ${SORT_SOURCES}
  "${HIGHWAY_SOURCE}/hwy/contrib/iguana/iguana.cc"
  "${HIGHWAY_SOURCE}/hwy/contrib/iguana/ans.cc"
  "${HIGHWAY_SOURCE}/hwy/contrib/hash/phast.cc"
  "${HIGHWAY_SOURCE}/hwy/contrib/hash/shardmul.cc"
  "${HIGHWAY_SOURCE}/hwy/contrib/hash/cuckoo2x2.cc")
add_library(highway-runtime OBJECT start.S runtime.c memory.c driver.cc)

# Keep sparse branches instead of dense hashing of empty slots, without
# disabling vectorization of unconditional initialization and copies.
set_source_files_properties(benchmarks/phast.cc PROPERTIES COMPILE_OPTIONS -fno-tree-loop-if-convert)
set_source_files_properties(memory.c PROPERTIES COMPILE_OPTIONS
  "-fno-builtin;-fno-tree-loop-distribute-patterns;-fno-tree-vectorize;-fno-strict-aliasing")

file(GLOB BENCHMARKS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cc")
list(SORT BENCHMARKS)
set(INVENTORY "")
foreach(SOURCE IN LISTS BENCHMARKS)
  get_filename_component(STEM "${SOURCE}" NAME_WE)
  set(NAME "highway_${STEM}")
  add_executable(${NAME} "${SOURCE}")
  target_link_libraries(${NAME} PRIVATE highway-runtime
    -Wl,--start-group highway-contrib highway-core stdc++ c m gcc -Wl,--end-group)
  target_link_options(${NAME} PRIVATE -nostartfiles -nostdlib -no-pie
    "-Wl,-T,${HIGHWAY_LINKER}" -Wl,--gc-sections -Wl,--no-warn-rwx-segments)
  set_property(TARGET ${NAME} APPEND PROPERTY LINK_DEPENDS "${HIGHWAY_LINKER}")
  string(APPEND INVENTORY "${NAME}\t$<TARGET_FILE:${NAME}>\n")
endforeach()
if(NOT INVENTORY)
  message(FATAL_ERROR "Highway selected no vector benchmarks")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/inventory.tsv" CONTENT "${INVENTORY}")
