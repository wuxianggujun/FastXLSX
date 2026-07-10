# 依赖

FastXLSX 使用 vcpkg manifest mode。依赖是否出现在 manifest、是否被当前 profile 安装、是否被源码链接是三个不同事实。

## 默认 production features

### `runtime-minizip`

- Port：`minizip-ng[core,zlib]`。
- CMake：`FASTXLSX_ENABLE_MINIZIP_NG=ON`，private link `MINIZIP::minizip-ng`。
- 用途：stored + DEFLATE ZIP package reader/writer。
- Install config：启用时 `find_dependency(minizip-ng CONFIG)`。

### `images`

- Port：`stb`。
- CMake：`FASTXLSX_ENABLE_IMAGES=ON`。
- 用途：PNG/JPEG metadata、decode、Streaming insertion 和已有 media replacement 的当前窄切片。
- Header-only，只在实现编译时需要；不开启时不查找 stb，改编译 disabled stubs，并向 consumer 传播 `FASTXLSX_HAS_IMAGES=0`。

## 显式 profiles

- `windows-nmake-release`：production，minizip + images。
- `windows-nmake-release-stored`：只安装 `images`，关闭 minizip；仅 stored ZIP bootstrap。
- `windows-nmake-release-no-images`：只安装 `runtime-minizip`，关闭 images，并运行 disabled-feature smoke。

## Future / Development features

- `planned-xml`：zlib-ng、Expat、pugixml；当前源码不链接，不得写成已实现 parser/DOM backend。
- `planned-dev`：Catch2、Google Benchmark 候选依赖；当前 tests/benchmarks 不由该 feature 自动接线。
- `reference-benchmarks`：OpenXLSX/xlnt，仅用于 opt-in 对照，不是 runtime foundation。

## 引入规则

- 优先 `find_package` / `find_path`，不默认 FetchContent，不 vendoring 到 `src/` 或 public `include/`。
- 记录 feature、linkage、license、export/consumer 影响和 fallback。
- ZIP/XML/image 库提供通用能力；OpenXML/OPC/XLSX 语义由 FastXLSX 实现。