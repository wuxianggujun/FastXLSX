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
先确认它们是否存在。当前没有 `vcpkg.json`、`CMakePresets.json`、`examples/`
或 CI 文件。

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
  - `FASTXLSX_BUILD_EXAMPLES` 默认 `OFF`，但当前没有实际分支。
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

## 推荐流程

1. 修改前先确认当前 target 形态。
2. 如果添加 `.cpp`，同步更新顶层 `fastxlsx` 源文件列表，或新增明确的内部/测试 target。
3. 如果添加测试，挂到 `FASTXLSX_BUILD_TESTS`，并注册 CTest。
4. 如果添加示例，挂到 `FASTXLSX_BUILD_EXAMPLES`；该选项目前只有定义。
5. 生成物不进源码。`.gitignore` 已忽略 `build/`、CMake 生成文件、
   IDE 状态、二进制、日志和临时文件。

## 依赖处理

- 代码真正使用依赖前，不要提前加 `find_package`。
- 依赖接入遵守 `fastxlsx-dependency-policy`。
- 默认不要用 `FetchContent` 拉核心依赖。
- 不要把第三方源码复制进 `src` 或 `include`。

## 验证

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

如果 `ctest` 没有运行测试，先检查 `tests/CMakeLists.txt` 是否仍注册
`fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`。
后台运行普通测试时使用 60s 超时。
