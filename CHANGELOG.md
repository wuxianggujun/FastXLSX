# Changelog

This project follows a conservative pre-1.0 versioning workflow.

## Versioning workflow

- `CMakeLists.txt` `project(FastXLSX VERSION ...)` and `vcpkg.json`
  `version-string` must stay in lockstep.
- Release tags should use `vMAJOR.MINOR.PATCH`, for example `v0.1.0`.
- Before tagging a release, update this file with user-facing changes,
  compatibility notes, validation evidence, and known non-goals.
- Do not describe APIs as stable until the corresponding public header
  comments, package install/export validation, CI, and local QA evidence are
  complete.

## [Unreleased]

### Added

- Basic CMake install/export package support for `FastXLSX::fastxlsx`.
- Release documentation for dependencies, local development environment, and
  third-party notices.
- README install and CMake consumer guidance for the generated config package.

### Validation

- `cmake --preset windows-nmake-release`
- `cmake --build --preset windows-nmake-release`
- `ctest --preset windows-nmake-release --output-on-failure`
- `cmake --install build/windows-nmake-release --prefix build/qa/install-fastxlsx-release-docs-clean`
- A local consumer project configured with `find_package(FastXLSX CONFIG REQUIRED)`
  compiled, linked, and ran against the installed package.
- `cmake --preset windows-nmake-release-minizip`
- `cmake --build --preset windows-nmake-release-minizip`
- `ctest --preset windows-nmake-release-minizip --output-on-failure`
- `cmake --install build/windows-nmake-release-minizip --prefix build/qa/install-fastxlsx-release-minizip`
- A local consumer project compiled, linked, and ran against the minizip-enabled
  installed package while resolving `find_dependency(minizip-ng CONFIG)` through
  the resolved vcpkg dependency prefix.
- `FASTXLSX_BUILD_EXAMPLES=ON` example targets
  `fastxlsx_minimal_writer_example` and `fastxlsx_streaming_writer_example`
  compiled against the public umbrella header.

### Not yet claimed

- Stable public API.
- CI release readiness.
- Native chart/VBA generation or editing.
- Complete existing-workbook object lifecycle, relationship repair/pruning, or
  orphan cleanup.

## [0.1.0] - Not released

- Initial project version used by CMake and vcpkg manifest metadata.
