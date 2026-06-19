# Dependencies

FastXLSX uses vcpkg manifest mode for third-party dependencies. Dependency
choices must preserve the project boundary: OpenXML/XLSX semantics and hot
worksheet XML paths stay inside FastXLSX; generic image and ZIP plumbing may use
mature libraries.

## Current default dependency

### `stb`

- Declared in `vcpkg.json` default `dependencies`.
- Used for PNG/JPEG image header probing and pixel decoding helpers.
- Also validates PNG/JPEG inputs for streaming-only new-workbook image
  insertion.
- Current local vcpkg metadata reports license `MIT OR CC-PDDC`.
- CMake integration uses vcpkg's `FindStb.cmake` and `${Stb_INCLUDE_DIR}`.
- `STB_IMAGE_IMPLEMENTATION` must stay in exactly one `.cpp` file.

`stb` does not provide OpenXML media, drawing, relationship, or content type
semantics. It must not be documented as existing-workbook image preservation or
drawing editing support.

## Optional runtime dependency group

The `planned-runtime` vcpkg feature is opt-in. It is used by presets that enable
`FASTXLSX_ENABLE_MINIZIP_NG=ON`.

- `minizip-ng[core,zlib]`: opt-in ZIP package backend.
- `zlib`: compression backend used by the current minizip-ng feature selection.
- `zlib-ng`, `expat`, and `pugixml`: recorded for planned ZIP/XML work, but not
  linked by the default build.

The minizip-ng path is not the default runtime path. Default builds still use
the internal stored ZIP bootstrap writer unless configured otherwise.

## Planned development dependency group

The `planned-dev` vcpkg feature records future development dependencies:

- `catch2`
- `benchmark`

Current tests do not use Catch2, and benchmark targets are opt-in through
`FASTXLSX_BUILD_BENCHMARKS=ON`.

## Release packaging notes

- Keep `vcpkg.json` and CMake dependency discovery in sync.
- Do not add a `find_package()` or link dependency unless source code actually
  uses that dependency.
- Do not use `FetchContent` for core dependencies by default.
- Do not vendor third-party source into `src/` or `include/`.
- Update `THIRD_PARTY_NOTICES.md` before publishing artifacts.
- Verify the exact dependency license metadata from the resolved vcpkg baseline
  before tagging a release.

## Non-goals

Do not add these as FastXLSX runtime dependencies:

- `OpenXLSX`
- `xlnt`
- `libxlsxwriter`
- `QXlsx`

They may be used only as references or benchmark comparison points.
