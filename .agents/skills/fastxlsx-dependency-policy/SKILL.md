---
name: fastxlsx-dependency-policy
description: "应用 FastXLSX vcpkg、linkage、license 与 export 边界。"
---
# FastXLSX Dependency Policy

- 默认 features：`runtime-minizip`、`images`。
- `runtime-minizip` 是当前 production ZIP backend。
- 启用 `runtime-minizip` 时 production private link `MINIZIP::minizip-ng`，install config 显式解析 minizip-ng；默认不直接链接/export `ZLIB::ZLIB`。只有 `FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=ON` 的 benchmark/profile build 才直接链接 zlib、编译 internal direct-raw engine，并在非默认 profiling install 中显式解析 ZLIB。Public/default DEFLATE 始终由 minizip 管理。
- Production PackageEditor staged/fused CRC 复用 minizip-ng 的 `mz_crypt_crc32_update`；stored-only 保留 portable fallback。`FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING=ON` 只在 minizip package backend 不变时切换 private CRC implementation，默认 OFF、不进入 public API、install dependency 或 consumer definition。
- `images` 可关闭；关闭时不安装/查找 stb。
- `planned-xml` 当前未链接；`planned-dev` 当前未自动接线。
- Reference libraries 只用于 benchmark。

使用 manifest mode + `find_package`/`find_path`；记录 feature、linkage、license、export/consumer 影响。Manifest presence 不等于 linked capability。
