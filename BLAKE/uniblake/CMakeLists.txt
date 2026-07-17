cmake_minimum_required(VERSION 3.16)
project(uniblake C)

# --- uniblake PoC build ---
# One static lib (the ub_ primitive + dispatch + oracle backend) and one
# test/validation executable. Cross-platform by construction: no
# absolute paths, the vendored reference is referenced repo-relative,
# and per-target detail is compile-guarded in the C, not the build.
# See BUILD.md for platform-specific invocation (arm64 macOS, x86
# Ubuntu 24, Windows 11 native/WSL2/cross).

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Repo-relative path to the single vendored reference (BLAKE.md §0).
# Overridable to keep parity with the corpus ports' convention.
set(UB_BLAKE2_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/blake2"
    CACHE PATH "Directory of the vendored portable blake2 reference")

if(NOT EXISTS "${UB_BLAKE2_VENDOR}/blake2b-ref.c")
  message(FATAL_ERROR
    "uniblake: vendored reference not found at ${UB_BLAKE2_VENDOR} "
    "(expected blake2b-ref.c). Set -DUB_BLAKE2_VENDOR=<dir>.")
endif()

add_library(uniblake STATIC
  src/ub_core.c
  src/ub_const.c
  src/ub_selftest.c
  src/ub_snapshot.c
  src/kernel_ref.c
  src/kernel_ref_unrolled.c
  src/kernel_neon.c
  src/ub_probe.c
  src/ub_oracle.c
)

target_include_directories(uniblake
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${UB_BLAKE2_VENDOR}
)

# The reference's load64/store64 pick a fast path when the target is
# known little-endian. arm64 and x86_64 are both LE.
include(TestBigEndian)
test_big_endian(UB_BIG_ENDIAN)
if(NOT UB_BIG_ENDIAN)
  target_compile_definitions(uniblake PRIVATE NATIVE_LITTLE_ENDIAN)
endif()

if(MSVC)
  target_compile_options(uniblake PRIVATE /W4)
else()
  target_compile_options(uniblake PRIVATE -Wall -Wextra -O2)
endif()

# Optional: compile the deliberately-broken kernel so a test can prove
# the self-test gate rejects it. Off by default.
option(UB_ENABLE_BROKEN_KERNEL "Build the broken test kernel" OFF)
if(UB_ENABLE_BROKEN_KERNEL)
  target_sources(uniblake PRIVATE src/kernel_broken.c)
  target_compile_definitions(uniblake PRIVATE UB_ENABLE_BROKEN_KERNEL)
endif()

# --- test / validation harness ---
enable_testing()
add_executable(ub_test tests/ub_test.c)
target_link_libraries(ub_test PRIVATE uniblake)
target_include_directories(ub_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)   # for ub_oracle.h / ub_probe.h in the test
add_test(NAME ub_validation COMMAND ub_test)

# Gate test: forcing each kernel via env still passes.
add_test(NAME ub_force_ref COMMAND ub_test)
set_tests_properties(ub_force_ref PROPERTIES ENVIRONMENT "UB_FORCE_IMPL=ref")
add_test(NAME ub_force_unrolled COMMAND ub_test)
set_tests_properties(ub_force_unrolled PROPERTIES ENVIRONMENT "UB_FORCE_IMPL=ref_unrolled")

# Stress test (U2): every kernel vs oracle across a large battery.
add_executable(ub_stress tests/ub_stress.c)
target_link_libraries(ub_stress PRIVATE uniblake)
target_include_directories(ub_stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME ub_stress COMMAND ub_stress)

# Snapshot validation (U3).
add_executable(ub_snapshot_test tests/ub_snapshot_test.c)
target_link_libraries(ub_snapshot_test PRIVATE uniblake)
target_include_directories(ub_snapshot_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME ub_snapshot COMMAND ub_snapshot_test)

# Dispatch-cost benchmark (U1). Plain executable, not a ctest.
add_executable(ub_bench tests/ub_bench.c)
target_link_libraries(ub_bench PRIVATE uniblake)
target_include_directories(ub_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Kernel speed benchmark (U2): compare registered kernels head-to-head.
add_executable(ub_kbench tests/ub_kbench.c)
target_link_libraries(ub_kbench PRIVATE uniblake)
target_include_directories(ub_kbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Gate-rejection test: only meaningful with the broken kernel compiled.
if(UB_ENABLE_BROKEN_KERNEL)
  add_executable(ub_gate_test tests/ub_gate_test.c)
  target_link_libraries(ub_gate_test PRIVATE uniblake)
  target_include_directories(ub_gate_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(ub_gate_test PRIVATE UB_ENABLE_BROKEN_KERNEL)
  add_test(NAME ub_gate_rejection COMMAND ub_gate_test)
endif()
