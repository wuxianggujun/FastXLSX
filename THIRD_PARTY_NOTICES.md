# Third-Party Notices

FastXLSX 本身使用 MIT License，见 `LICENSE`。发布制品前应以 resolved vcpkg baseline 和实际安装包复核许可证文本。

## Default Production Features

### minizip-ng

- vcpkg feature：`runtime-minizip`，port `minizip-ng[core,zlib]`。
- 用途：stored + DEFLATE ZIP package reader/writer。
- 当前实现 private link；安装配置在启用时要求 consumer 可解析 minizip-ng。
- License：按 resolved vcpkg metadata/上游包复核。

### zlib

- 来源：`runtime-minizip` 安装的 `minizip-ng[core,zlib]` dependency graph。
- 用途：production minizip-ng 的 DEFLATE dependency；显式 `FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=ON` 时还供 internal one-pass direct-raw-DEFLATE engine 使用。
- 默认 FastXLSX production target 不直接链接 `ZLIB::ZLIB`；只有 profiling build private-link zlib，且对应非默认安装配置要求 consumer 可解析 `ZLIB::ZLIB`。
- License：zlib License；发布前按 resolved vcpkg package 复核并随制品保留相应 notice。

### stb

- vcpkg feature：`images`。
- 用途：PNG/JPEG metadata、decode 与当前图片切片。
- Header-only，只用于 FastXLSX 实现编译；关闭 images 时不需要 stb。
- vcpkg metadata 当前为 `MIT OR CC-PDDC`，发布前复核。

## Non-runtime / Future Features

- `planned-xml`：zlib-ng、Expat、pugixml；当前源码未链接。
- `planned-dev`：Catch2、Google Benchmark 候选；当前未由 feature 自动接线。
- `reference-benchmarks`：OpenXLSX、xlnt，只用于 opt-in benchmark，不进入 FastXLSX runtime。
