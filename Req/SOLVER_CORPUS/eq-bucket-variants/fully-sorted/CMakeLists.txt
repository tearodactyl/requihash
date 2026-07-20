cmake_minimum_required(VERSION 3.16)
project(eq_fully_sorted CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-O2 -Wall)

if(NOT DEFINED BLAKE2_REF_DIR)
  set(BLAKE2_REF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../BLAKE/vendor/blake2")
endif()
if(NOT EXISTS "${BLAKE2_REF_DIR}/blake2b-ref.c")
  message(FATAL_ERROR "Vendored blake2b-ref.c not found at ${BLAKE2_REF_DIR}")
endif()

set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common")

add_library(eq_fully_sorted_lib STATIC
  src/solver.cpp
  "${COMMON_DIR}/equihash_ref.cpp"
  "${COMMON_DIR}/brute_force.cpp"
  "${BLAKE2_REF_DIR}/blake2b-ref.c"
)
target_include_directories(eq_fully_sorted_lib PUBLIC src "${COMMON_DIR}" "${BLAKE2_REF_DIR}")

add_executable(eq_fs_gen src/eq_fs_gen.cpp)
target_link_libraries(eq_fs_gen PRIVATE eq_fully_sorted_lib)

enable_testing()
add_executable(eq_fs_differential tests/differential.cpp)
target_link_libraries(eq_fs_differential PRIVATE eq_fully_sorted_lib)
add_test(NAME eq_fs_differential COMMAND eq_fs_differential)
