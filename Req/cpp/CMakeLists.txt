cmake_minimum_required(VERSION 3.16)
project(requihash_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-O2 -Wall)

add_executable(req_test req_test.cpp)
add_executable(req_gen req_gen.cpp)

# The benchmark is built to match the Rust release profile as closely as the
# toolchains allow, so the C++ vs Rust comparison isolates implementation, not
# optimizer settings: Rust = opt-level 3 + fat LTO + 1 codegen unit + native CPU.
add_executable(req_bench req_bench.cpp)
target_compile_options(req_bench PRIVATE -O3 -flto -mcpu=native)
target_link_options(req_bench PRIVATE -flto)
