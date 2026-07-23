cmake_minimum_required(VERSION 3.16)
project(rk_original CXX C)

# C++14 deliberately (this program's floor for anything that may sit next
# to the Zero400/ZeroPerf C++14 lineage); no architecture-specific flags,
# no intrinsics -- portable across Linux / macOS (arm64 and x86_64) /
# Windows (MSVC or MinGW). C is enabled for the vendored blake2b-ref.c
# (a CXX-only project silently drops .c sources from the build).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The repository's canonical portable BLAKE2b (bytes in
# BLAKE/vendor/blake2; provenance pin in BLAKE/uniblake/PROVENANCE.md);
# -DBLAKE2_REF_DIR=... overrides.
if(NOT DEFINED BLAKE2_REF_DIR)
  set(BLAKE2_REF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../BLAKE/vendor/blake2")
endif()
if(NOT EXISTS "${BLAKE2_REF_DIR}/blake2b-ref.c")
  message(FATAL_ERROR "Vendored blake2b-ref.c not found at ${BLAKE2_REF_DIR} -- "
    "see BLAKE/uniblake/PROVENANCE.md, or set -DBLAKE2_REF_DIR=... explicitly.")
endif()

add_library(rk_pow STATIC pow.cc "${BLAKE2_REF_DIR}/blake2b-ref.c")
target_include_directories(rk_pow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${BLAKE2_REF_DIR}")

# Upstream's own CLI (pow-test.cc, verbatim): ./equihash -n N -k K -s S
add_executable(equihash pow-test.cc)
target_link_libraries(equihash PRIVATE rk_pow)

# JSON vector generator for ../vectors/*.json
add_executable(rk_vecgen vecgen.cc)
target_link_libraries(rk_vecgen PRIVATE rk_pow)

# Single-attempt measurement driver (see rk_single_bench.cpp doc comment).
add_executable(rk_single_bench rk_single_bench.cpp)
target_link_libraries(rk_single_bench PRIVATE rk_pow)
