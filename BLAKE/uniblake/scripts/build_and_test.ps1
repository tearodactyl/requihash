# uniblake - Windows build + test (PowerShell). Win10 / Win11, native.
#
# Works with either the MSVC generator (Visual Studio) or Ninja/MinGW,
# whichever CMake finds. Run from a Developer PowerShell (for MSVC) or
# any PowerShell with CMake + a compiler on PATH.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build_and_test.ps1
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$build = Join-Path $root "build"

Write-Host "== uniblake build+test (Windows) =="
Write-Host "root:  $root"
Write-Host "build: $build"
Write-Host ("cpu:   " + $env:PROCESSOR_IDENTIFIER)
Write-Host ""

# Configure. Let CMake pick the default generator; -A x64 applies only
# to the Visual Studio generator and is ignored otherwise.
$isVS = (cmake --help | Select-String "Visual Studio 17") -ne $null
if ($isVS) {
  cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64 -DUB_ENABLE_BROKEN_KERNEL=ON
  cmake --build $build --config Release
  $cfg = "-C Release"
} else {
  cmake -S $root -B $build -DUB_ENABLE_BROKEN_KERNEL=ON
  cmake --build $build
  $cfg = ""
}

Write-Host ""
Write-Host "== ctest =="
if ($isVS) { ctest --test-dir $build -C Release --output-on-failure }
else       { ctest --test-dir $build --output-on-failure }

Write-Host ""
Write-Host "== probe + KAT =="
$exe = if ($isVS) { Join-Path $build "Release\ub_test.exe" } else { Join-Path $build "ub_test.exe" }
& $exe

Write-Host ""
Write-Host "OK"
