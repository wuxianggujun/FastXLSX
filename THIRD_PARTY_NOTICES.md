# Third-Party Notices

FastXLSX itself is licensed under the MIT License. See `LICENSE`.

This file records third-party dependency notices for the current repository
state. Before publishing a release artifact, verify the exact license metadata
from the resolved vcpkg baseline and include any required upstream license text
in the distributed package.

## Runtime dependencies used by the default build

### stb

- Purpose: PNG/JPEG header probing, image pixel decoding helpers, and validation
  for streaming-only new-workbook image insertion.
- Source: vcpkg port `stb`.
- Current vcpkg license metadata observed in local validation:
  `MIT OR CC-PDDC`.
- Integration boundary: header-only dependency found through vcpkg's
  `FindStb.cmake`; `STB_IMAGE_IMPLEMENTATION` is defined in one `.cpp` file.

## Optional runtime dependencies

These are used only when configured with `FASTXLSX_ENABLE_MINIZIP_NG=ON` and
the `planned-runtime` vcpkg feature.

### minizip-ng

- Purpose: opt-in ZIP package writer/read support for DEFLATE paths.
- Source: vcpkg port `minizip-ng` with `default-features=false` and `zlib`
  feature enabled.
- License: verify from the resolved vcpkg package before release packaging.

### zlib

- Purpose: compression backend selected by the current minizip-ng vcpkg feature.
- Source: transitive vcpkg dependency of `minizip-ng[zlib]`.
- License: verify from the resolved vcpkg package before release packaging.

## Planned but not linked by default

The following ports are recorded in `vcpkg.json` feature groups for future work
or opt-in validation. They must not be presented as default runtime
dependencies unless the build actually links them.

- `zlib-ng`
- `expat`
- `pugixml`
- `catch2`
- `benchmark`

## Non-dependency policy

FastXLSX does not vendor third-party source into `src/` or `include/`. Do not
add `OpenXLSX`, `xlnt`, `libxlsxwriter`, or `QXlsx` as runtime dependencies;
they may only be used as references or benchmark comparison points.
