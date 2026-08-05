---
name: fastxlsx-cmake-build
description: "配置、构建和排查 FastXLSX CMake/presets/install。"
---
# FastXLSX CMake Build

Target：`fastxlsx` / `FastXLSX::fastxlsx`，C++20。

Profiles：
- `windows-nmake-release`：production minizip + images。
- `windows-nmake-release-stored`：stored-only bootstrap。
- `windows-nmake-release-no-images`：minizip，无 stb，运行 image-disabled smoke。
- `windows-nmake-release-direct-zlib-profile`：显式开启 internal direct-zlib profiling、focused tests 与 benchmarks；不是 production/install 默认 profile。
- `windows-nmake-release-patch-crc-minizip-profile` / `windows-nmake-release-patch-crc-portable-profile`：相同 minizip package backend 下的 private PackageEditor CRC balanced A/B；后者只强制 portable CRC，二者都不是 install 默认 profile。

修改新 source/test/smoke 必须同步 CMake。验证遵守 `docs/TESTING_WORKFLOW.md` 分级：T1 小功能只构建直接 target 并运行 focused/必要邻接测试；只有 T2 大功能收口或明确 release gate 才全量 build + CTest。Package/install/export 边界确有变化时追加对应 profile 与 consumer `find_package()` smoke，但不因此机械运行全套。普通终端缺少 nmake 时先加载 VS Developer 环境。
