# 依赖

FastXLSX 使用 vcpkg manifest mode。依赖是否出现在 manifest、是否被当前 profile 安装、是否被源码链接是三个不同事实。

## 默认 production features

### `runtime-minizip`

- Port：`minizip-ng[core,zlib]`。
- CMake：`FASTXLSX_ENABLE_MINIZIP_NG=ON`，production library private link `MINIZIP::minizip-ng`；PackageReader/PackageWriter 使用 minizip-ng ZIP API，PackageEditor staged/fused CRC 使用 `mz_crypt_crc32_update`。zlib 由 minizip-ng dependency graph 提供，但 FastXLSX 默认不直接链接 `ZLIB::ZLIB`；stored-only profile 继续使用 internal portable CRC fallback。`FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING=ON` 只在保持 minizip package backend 不变时强制 PackageEditor portable CRC，供 internal A/B 使用；默认 OFF、private compile definition、不进入 install export。
- 用途：stored + DEFLATE ZIP package reader/writer，默认/public 路径由 minizip 管理 DEFLATE。
- Install config：production 显式 `find_dependency(minizip-ng CONFIG)`；只有 `FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=ON` 的非默认安装才额外 `find_dependency(ZLIB)`。

### `images`

- Port：`stb`。
- CMake：`FASTXLSX_ENABLE_IMAGES=ON`。
- 用途：PNG/JPEG metadata、decode、Streaming insertion 和已有 media replacement 的当前窄切片。
- Header-only，只在实现编译时需要；不开启时不查找 stb，改编译 disabled stubs，并向 consumer 传播 `FASTXLSX_HAS_IMAGES=0`。

## 显式 profiles

- `windows-nmake-release`：production，minizip + images。
- `windows-nmake-release-stored`：只安装 `images`，关闭 minizip；仅 stored ZIP bootstrap。
- `windows-nmake-release-no-images`：只安装 `runtime-minizip`，关闭 images，并运行 disabled-feature smoke。
- `windows-nmake-release-direct-zlib-profile`：显式开启 internal direct-zlib profiling engine、focused tests 与 manual benchmarks；不是 production/install 默认 profile。
- `windows-nmake-release-patch-crc-minizip-profile`：internal Patch benchmark，使用 production minizip CRC，关闭 tests/direct-zlib。
- `windows-nmake-release-patch-crc-portable-profile`：与上一 profile 使用相同 minizip package backend，但强制 portable PackageEditor CRC；只用于 balanced A/B。

## Future / Development features

- `planned-xml`：zlib-ng、Expat、pugixml；当前源码不链接，不得写成已实现 parser/DOM backend。
- `planned-dev`：Catch2、Google Benchmark 候选依赖；当前 tests/benchmarks 不由该 feature 自动接线。
- `reference-benchmarks`：OpenXLSX/xlnt，仅用于 opt-in 对照，不是 runtime foundation。

## 引入规则

- 优先 `find_package` / `find_path`，不默认 FetchContent，不 vendoring 到 `src/` 或 public `include/`。
- 记录 feature、linkage、license、export/consumer 影响和 fallback。
- ZIP/XML/image 库提供通用能力；OpenXML/OPC/XLSX 语义由 FastXLSX 实现。
