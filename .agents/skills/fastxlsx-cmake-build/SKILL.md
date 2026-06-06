---
name: fastxlsx-cmake-build
description: "配置、构建和排查 FastXLSX CMake 工程。用于修改 CMakeLists.txt、添加源码/测试/示例 target、本地构建失败、生成器问题、CTest 接入、INTERFACE 目标转 compiled target，或为 C++20/MSVC 2026 优先项目做轻量构建验证。"
---

# FastXLSX CMake Build

## 必读文件

- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `docs/DEVELOPMENT_ENVIRONMENT.md`
- `docs/DEPENDENCIES.md`
- `.gitignore`

引用 `vcpkg.json`、`CMakePresets.json`、`examples/`、CI 文件或真实测试源码前，
先确认它们是否存在。当前可见 `vcpkg.json`、`CMakePresets.json`、
`.github/workflows/ci.yml` 和 `examples/`，但这些只能写为基础：
`vcpkg.json` 仍把第三方依赖放在 planned features，CMake 尚未接入
`find_package` / link，CI 路径也不能在本地文档里写成已验证完成。

## 当前 CMake 事实

- 最低 CMake 版本是 `3.20`。
- 项目是 `FastXLSX`，版本 `0.1.0`，语言是 `CXX`。
- C++ 标准是 C++20，强制启用，并关闭编译器扩展。
- `fastxlsx` 当前是 compiled library，源码列表在顶层 `CMakeLists.txt`；当前已接入
  `src/opc.cpp`、`src/streaming_writer.cpp`、`src/workbook.cpp`、`src/xml.cpp`、
  `src/zip_store_writer.cpp`。
- 别名目标是 `FastXLSX::fastxlsx`。
- 选项：
  - `FASTXLSX_BUILD_TESTS` 默认 `ON`。
  - `FASTXLSX_BUILD_EXAMPLES` 默认 `OFF`，当前已有 `add_subdirectory(examples)`
    分支。
  - `FASTXLSX_ENABLE_DOM_EDITING` 默认 `ON`。
- `tests/CMakeLists.txt` 注册 `fastxlsx_tests`、`fastxlsx_streaming_writer_tests`
  和 `fastxlsx_opc_tests`，CTest 名称是 `fastxlsx.unit`、`fastxlsx.streaming`
  和 `fastxlsx.opc`。
- 当前测试不依赖 Catch2。

## 本地配置命令

Windows/MSVC 优先使用 Visual Studio 2026 / MSVC 2026。当前本机已验证
VS2026 Developer Command Prompt + NMake：

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

如果其他机器上的 Visual Studio 2026 对应新的 CMake 生成器名称，先查：

```powershell
cmake --help
```

不要把当前 `build/CMakeCache.txt` 作为规范。它曾显示 `NMake Makefiles`，
而开发文档说明：未显式指定 `-G` 时可能误选 NMake，并在没有 `nmake`
的环境里失败。

当前可见 `CMakePresets.json` 提供 `windows-nmake-release` 和
`windows-nmake-release-vcpkg` 基础 preset。前者不使用 vcpkg；后者通过
`VCPKG_ROOT` 指向 vcpkg toolchain，但 manifest 没有默认第三方依赖，主要用于
后续验证 toolchain 和 package target。

## 推荐流程

1. 修改前先确认当前 target 形态。
2. 如果添加 `.cpp`，同步更新顶层 `fastxlsx` 源文件列表，或新增明确的内部/测试 target。
3. 如果添加测试，挂到 `FASTXLSX_BUILD_TESTS`，并注册 CTest。
4. 如果添加示例，挂到 `FASTXLSX_BUILD_EXAMPLES`，并确认 `examples/`
   下的 target、输出文件和验证方式。
5. 生成物不进源码。`.gitignore` 已忽略 `build/`、CMake 生成文件、
   IDE 状态、二进制、日志和临时文件。

## 依赖处理

- 当前 `vcpkg.json` 是基础入口：默认依赖为空，planned features 包含
  `minizip-ng`、`zlib-ng`、`expat`、`pugixml`、`catch2` 和 `benchmark`。
- 代码真正使用依赖前，不要提前加 `find_package` 或 link。
- 接入前必须验证真实 port 名、feature、imported target、triplet 行为和许可证。
- 依赖接入遵守 `fastxlsx-dependency-policy`。
- 默认不要用 `FetchContent` 拉核心依赖。
- 不要把第三方源码复制进 `src` 或 `include`。

## 本轮计划边界

- vcpkg：基础。manifest 可见，但第三方依赖仍是 planned features，不代表生产 ZIP
  backend 已接入。
- CMakePresets：基础。preset 可见，默认以 VS2026/MSVC 2026 + NMake 为主线。
- CI：基础。workflow 可见，目标是结构测试和 `ctest --timeout 60`；Excel 可视化
  验证仍是本地步骤，大型 benchmark 不进入默认 CI。

## 验证

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

如果 `ctest` 没有运行测试，先检查 `tests/CMakeLists.txt` 是否仍注册
`fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`。
后台运行普通测试时使用 60s 超时。
