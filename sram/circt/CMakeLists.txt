# Builds the technology-independent CIRCT pass that applies occurrence-specific SRAM policy.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(RhodiumMemorySites LANGUAGES C CXX)

find_package(CIRCT REQUIRED CONFIG)

add_library(rhodium-memory-sites MODULE MemorySitePass.cpp)
target_compile_features(rhodium-memory-sites PRIVATE cxx_std_17)
if(NOT MSVC)
  target_compile_options(rhodium-memory-sites PRIVATE -fno-rtti)
endif()
target_include_directories(rhodium-memory-sites PRIVATE ${CIRCT_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_link_libraries(rhodium-memory-sites PRIVATE CIRCTHW MLIRIR MLIRPass MLIRSupport LLVMSupport)
set_target_properties(rhodium-memory-sites PROPERTIES PREFIX "" BUILD_RPATH "${CIRCT_LIBRARY_DIRS}")
if(APPLE)
  target_link_options(rhodium-memory-sites PRIVATE "-Wl,-undefined,dynamic_lookup")
endif()
