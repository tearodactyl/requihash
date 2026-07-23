cmake_minimum_required(VERSION 3.16)
project(cs_klist_v2 CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-O2 -Wall)

if(NOT DEFINED BLAKE2_REF_DIR)
  set(BLAKE2_REF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../BLAKE/vendor/blake2")
endif()
if(NOT EXISTS "${BLAKE2_REF_DIR}/blake2b-ref.c")
  message(FATAL_ERROR "Vendored blake2b-ref.c not found at ${BLAKE2_REF_DIR}")
endif()

add_library(cs_klist_v2 STATIC src/klist_v2.cpp "${BLAKE2_REF_DIR}/blake2b-ref.c")
target_include_directories(cs_klist_v2 PUBLIC src "${BLAKE2_REF_DIR}")

add_executable(cs_v2_gen src/cs_v2_gen.cpp)
target_link_libraries(cs_v2_gen PRIVATE cs_klist_v2)

add_executable(cs_v2_bench src/cs_v2_bench.cpp)
target_link_libraries(cs_v2_bench PRIVATE cs_klist_v2)

enable_testing()
add_executable(cs_v2_differential tests/differential.cpp)
target_link_libraries(cs_v2_differential PRIVATE cs_klist_v2)
add_test(NAME cs_v2_differential COMMAND cs_v2_differential "${CMAKE_CURRENT_SOURCE_DIR}/../../vectors")
