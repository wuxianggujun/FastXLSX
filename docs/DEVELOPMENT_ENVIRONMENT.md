# Development Environment

FastXLSX is currently developed and validated primarily on Windows with Visual
Studio 2026 / MSVC 2026 and CMake presets.

## Required tools

- Visual Studio 2026 with MSVC C++ tools.
- CMake 3.20 or newer.
- vcpkg available through `VCPKG_ROOT`, `VCPKG_INSTALLATION_ROOT`, or the
  Visual Studio bundled vcpkg path used by the presets.
- Python for repository verification scripts.
- Excel is optional for local COM-based visual QA scripts.

## Recommended local workflow

Run from a VS2026 Developer Command Prompt, or initialize the environment first:

```powershell
cmd /c '"D:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset windows-nmake-release'
cmd /c '"D:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" && cmake --build --preset windows-nmake-release'
cmd /c '"D:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" && ctest --preset windows-nmake-release --output-on-failure'
```

The `windows-nmake-release` preset uses `NMake Makefiles`. Running it from a
plain PowerShell where `nmake` is not on `PATH` will fail during compiler
detection.

## Presets

- `windows-nmake-release`: default vcpkg-backed smoke path with `stb`.
- `windows-nmake-release-vcpkg`: compatibility vcpkg toolchain preset.
- `windows-nmake-release-minizip`: opt-in `planned-runtime` path with
  `FASTXLSX_ENABLE_MINIZIP_NG=ON`.
- `windows-nmake-release-benchmark`: opt-in benchmark build.
- `windows-nmake-release-benchmark-minizip`: opt-in benchmark build with the
  minizip backend.
- `windows-nmake-release-reference-benchmark`: opt-in third-party C++ writer
  adapter benchmark build. It enables `FASTXLSX_BUILD_REFERENCE_BENCHMARKS=ON`
  and the vcpkg `reference-benchmarks` feature for OpenXLSX / xlnt adapters.

## Install/export validation

For release packaging work, validate both the project and a small installed
consumer:

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release --output-on-failure
cmake --install build/windows-nmake-release --prefix build/qa/install-fastxlsx
```

Then configure a separate consumer with:

```cmake
find_package(FastXLSX CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE FastXLSX::fastxlsx)
```

For the opt-in minizip package, repeat the same flow with
`windows-nmake-release-minizip` and install to a separate prefix. The installed
`FastXLSXConfig.cmake` intentionally calls `find_dependency(minizip-ng CONFIG)`
for that build, so the consumer configure step must also make the resolved
vcpkg dependency prefix available through the vcpkg toolchain or
`CMAKE_PREFIX_PATH`.

## Generated files

Do not stage build outputs, generated workbooks, local logs, private state, or
temporary consumer projects under `build/qa/`. The repository `.gitignore`
already excludes these paths.
