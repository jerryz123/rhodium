# Exposes the pinned, installed Sail model without build-directory dependencies.
# SPDX-License-Identifier: Apache-2.0
get_filename_component(_sail_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_path(SAIL_GMP_INCLUDE_DIR gmp.h REQUIRED)
find_library(SAIL_GMP_LIBRARY NAMES gmp REQUIRED)
if(NOT TARGET Sail::Model)
  add_library(Sail::Runtime STATIC IMPORTED)
  set_target_properties(Sail::Runtime PROPERTIES
    IMPORTED_LOCATION "${_sail_prefix}/lib/libsail_runtime.a"
    INTERFACE_LINK_LIBRARIES "${SAIL_GMP_LIBRARY};${CMAKE_DL_LIBS}")
  add_library(Sail::SoftFloat STATIC IMPORTED)
  set_target_properties(Sail::SoftFloat PROPERTIES
    IMPORTED_LOCATION "${_sail_prefix}/lib/libsail_softfloat.a")
  add_library(Sail::Model STATIC IMPORTED)
  set_target_properties(Sail::Model PROPERTIES
    IMPORTED_LOCATION "${_sail_prefix}/lib/libsail_riscv_model.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_sail_prefix}/include/sail-model;${SAIL_GMP_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "Sail::SoftFloat;Sail::Runtime")
endif()
