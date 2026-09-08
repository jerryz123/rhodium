# Builds the reusable C++ Perfetto library, standalone converter, and contract tests.
cmake_minimum_required(VERSION 3.20)
project(rheg_perfetto LANGUAGES CXX)
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(rheg_spike
  URL https://codeload.github.com/riscv-software-src/riscv-isa-sim/tar.gz/e1fa113cfb6d55d878a3c1ea3befa8d9c13ce154
  URL_HASH SHA256=1d605e77d7489d1efd180acf0c562314a1b51138d38a311647d96b5d86e806ec)
FetchContent_MakeAvailable(rheg_spike)
# Adapt parser failures and the opcode-free Zic64b cache-block property.
# The downloaded source tree stays untouched.
file(READ "${rheg_spike_SOURCE_DIR}/disasm/isa_parser.cc" spike_parser)
string(REGEX MATCHALL "  abort\\(\\)" spike_aborts "${spike_parser}")
list(LENGTH spike_aborts spike_abort_count)
if(NOT spike_abort_count EQUAL 2)
  message(FATAL_ERROR "Pinned Spike parser failure sites changed")
endif()
string(REPLACE "  abort();" "  throw std::invalid_argument(\"invalid RISC-V ISA configuration\");" spike_parser "${spike_parser}")
# Like Zkt, Zic64b adds no decoder flags. Register it in the parser rather than
# stripping tokens from the ISA string, preserving syntax and unknown-name checks.
string(REGEX MATCHALL "[{]\"zkt\"[}]" spike_zkt_entries "${spike_parser}")
list(LENGTH spike_zkt_entries spike_zkt_count)
if(NOT spike_zkt_count EQUAL 1)
  message(FATAL_ERROR "Pinned Spike property-extension table changed")
endif()
string(REPLACE "{\"zkt\"}" "{\"zic64b\"},\n  {\"zkt\"}" spike_parser "${spike_parser}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spike-adapter")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/spike-adapter/isa_parser.cc" "// Adapts pinned Spike ISA errors and the opcode-free Zic64b property for embedding.\n${spike_parser}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/spike-adapter/config.h" "// Standalone disassembly requires no simulator configuration defines.\n")
add_library(rheg_disasm OBJECT
  "${rheg_spike_SOURCE_DIR}/disasm/disasm.cc"
  "${rheg_spike_SOURCE_DIR}/disasm/regnames.cc"
  "${CMAKE_CURRENT_BINARY_DIR}/spike-adapter/isa_parser.cc")
target_include_directories(rheg_disasm SYSTEM PRIVATE
  "${rheg_spike_SOURCE_DIR}/riscv" "${rheg_spike_SOURCE_DIR}/softfloat"
  "${CMAKE_CURRENT_BINARY_DIR}/spike-adapter")
target_compile_features(rheg_disasm PRIVATE cxx_std_17)
find_package(nlohmann_json 3.12.0 EXACT QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
  FetchContent_MakeAvailable(nlohmann_json)
endif()
if(NOT TARGET rheg_runtime)
  add_library(rheg_runtime STATIC ../runtime/rheg.cc)
  target_include_directories(rheg_runtime PUBLIC ../runtime)
  target_compile_features(rheg_runtime PUBLIC cxx_std_17)
endif()
add_library(rheg_perfetto STATIC rheg_perfetto.cc $<TARGET_OBJECTS:rheg_disasm>)
target_include_directories(rheg_perfetto PUBLIC .)
target_include_directories(rheg_perfetto SYSTEM PRIVATE "${rheg_spike_SOURCE_DIR}/riscv")
target_compile_features(rheg_perfetto PUBLIC cxx_std_17)
target_link_libraries(rheg_perfetto PUBLIC rheg_runtime PRIVATE nlohmann_json::nlohmann_json)
add_executable(rheg-perfetto main.cc)
target_link_libraries(rheg-perfetto PRIVATE rheg_perfetto)
option(RHEG_PERFETTO_TESTS "Build event exporter tests" OFF)
if(RHEG_PERFETTO_TESTS)
  enable_testing()
  add_executable(event-perfetto-test ../tests/event-perfetto-test.cpp)
  target_link_libraries(event-perfetto-test PRIVATE rheg_perfetto)
  add_test(NAME event-perfetto COMMAND event-perfetto-test ${CMAKE_CURRENT_BINARY_DIR})
  add_executable(event-stream-test ../tests/event-stream-test.cpp)
  target_link_libraries(event-stream-test PRIVATE rheg_perfetto)
endif()
