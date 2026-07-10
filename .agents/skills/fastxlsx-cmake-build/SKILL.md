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

修改新 source/test/smoke 必须同步 CMake。Package 变更验证 focused → production/stored/no-images → install/consumer `find_package()`。普通终端缺少 nmake 时先加载 VS Developer 环境。