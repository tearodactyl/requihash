cmake_minimum_required(VERSION 3.16)
project(cs_klist CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-O2 -Wall)

# Portable BLAKE2b: the repository's vendored copy (repo-relative, no
# machine-specific path) -- same canonical implementation RK and RZ use.
# Override with -DBLAKE2_REF_DIR=... to build against a different copy.
# Provenance: ../../../BLAKE/uniblake/PROVENANCE.md.
if(NOT DEFINED BLAKE2_REF_DIR)
  set(BLAKE2_REF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../BLAKE/vendor/blake2")
endif()
if(NOT EXISTS "${BLAKE2_REF_DIR}/blake2b-ref.c")
  message(FATAL_ERROR "Vendored blake2b-ref.c not found at ${BLAKE2_REF_DIR} -- "
    "see BLAKE/uniblake/PROVENANCE.md, or set -DBLAKE2_REF_DIR=... explicitly.")
endif()

add_library(cs_klist STATIC src/klist.cpp "${BLAKE2_REF_DIR}/blake2b-ref.c")
target_include_directories(cs_klist PUBLIC src "${BLAKE2_REF_DIR}")

add_executable(cs_gen src/cs_gen.cpp)
target_link_libraries(cs_gen PRIVATE cs_klist)

enable_testing()
add_executable(cs_differential tests/differential.cpp)
target_link_libraries(cs_differential PRIVATE cs_klist)
add_test(NAME cs_differential COMMAND cs_differential "${CMAKE_SOURCE_DIR}/vectors")
