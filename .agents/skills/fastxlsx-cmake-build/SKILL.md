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

引用 `vcpkg.json`、`CMakePresets.json`、`examples/`、CI 文件或真实测试源码前，先确认它们是否存在。当前可见 `vcpkg.json`、`CMakePresets.json`、`.github/workflows/ci.yml` 和 `examples/`，但这些只能写为基础。默认构建现在通过 vcpkg manifest 接入 `stb`，用于 PNG/JPEG `read_image_info()` 元数据 helper 和 `WorksheetWriter::add_image()` 的 streaming 图片插入基础切片；`FASTXLSX_ENABLE_MINIZIP_NG=ON` 才通过 `planned-runtime` 接入 `find_package(minizip-ng CONFIG REQUIRED)` / `MINIZIP::minizip-ng`。CI workflow 现在有默认 vcpkg-backed job 和 opt-in minizip vcpkg matrix job；远端 vcpkg availability、安装耗时和缓存行为必须以实际 CI run 为准。

## 当前 CMake 事实

- 最低 CMake 版本是 `3.20`。
- 项目是 `FastXLSX`，版本 `0.1.0`，语言是 `CXX`。
- C++ 标准是 C++20，强制启用，并关闭编译器扩展。
- `fastxlsx` 当前是 compiled library，源码列表在顶层 `CMakeLists.txt`；当前已接入
  `src/image.cpp`、`src/opc.cpp`、`src/package_editor.cpp`、`src/package_reader.cpp`、
  `src/package_writer.cpp`、`src/streaming_writer.cpp`、`src/workbook.cpp`、
  `src/xml.cpp`、`src/zip_store_writer.cpp`。
- 别名目标是 `FastXLSX::fastxlsx`。
- 选项：
  - `FASTXLSX_BUILD_TESTS` 默认 `ON`。
    启用测试时，`fastxlsx` 和 `fastxlsx_streaming_writer_tests` 会以 `PRIVATE`
    方式定义 `FASTXLSX_ENABLE_TEST_HOOKS=1`，只用于低成本触发内部边界测试。
    不要把该宏传播为 `PUBLIC` / `INTERFACE`，也不要把测试 hook 当成用户 API。
  - `FASTXLSX_BUILD_EXAMPLES` 默认 `OFF`，当前已有 `add_subdirectory(examples)`
    分支。
  - `FASTXLSX_BUILD_BENCHMARKS` 默认 `OFF`，启用后添加 `benchmarks/` 下的
    手工 benchmark target；这些 target 不注册 CTest。
  - `FASTXLSX_ENABLE_DOM_EDITING` 默认 `ON`。
  - `FASTXLSX_ENABLE_MINIZIP_NG` 默认 `OFF`，启用后链接 `MINIZIP::minizip-ng`
    并定义 `FASTXLSX_HAS_MINIZIP_NG`。
- 当前没有单独的 stb 开关；顶层 CMake 在 vcpkg manifest 环境下把
  `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/stb` 和根目录
  `vcpkg_installed/*/share/stb` 加入 `CMAKE_MODULE_PATH`，
  从对应 installed include 目录预解析 `Stb_INCLUDE_DIR`，再调用
  `find_package(Stb MODULE REQUIRED)`，并把 `${Stb_INCLUDE_DIR}` 作为 `fastxlsx`
  的 private include path。
- `tests/CMakeLists.txt` 注册 `fastxlsx_tests`、`fastxlsx_streaming_writer_tests`
  `fastxlsx_opc_tests` 和 `fastxlsx_image_tests`，CTest 名称是
  `fastxlsx.unit`、`fastxlsx.streaming`、`fastxlsx.opc` 和 `fastxlsx.image`。
- 当前测试不依赖 Catch2。

## 本地配置命令

Windows/MSVC 优先使用 Visual Studio 2026 / MSVC 2026。推荐在 VS2026
Developer Command Prompt 中使用 preset：

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

如果其他机器上的 Visual Studio 2026 对应新的 CMake 生成器名称，先查：

```powershell
cmake --help
```

不要把当前 `build/CMakeCache.txt` 作为规范。它曾显示 `NMake Makefiles`，
而开发文档说明：未显式指定 `-G` 时可能误选 NMake，并在没有 `nmake`
的环境里失败。

当前可见 `CMakePresets.json` 提供：
- `windows-nmake-release`：默认 vcpkg-backed NMake release，安装默认 `stb`。
- `windows-nmake-release-vcpkg`：兼容别名式 vcpkg toolchain preset。
- `windows-nmake-release-minizip`：启用 `planned-runtime` 和
  `FASTXLSX_ENABLE_MINIZIP_NG=ON`，用于 opt-in minizip backend 验证。
- `windows-nmake-release-benchmark`：启用 `FASTXLSX_BUILD_BENCHMARKS=ON`，
  使用默认 vcpkg 依赖，不进入默认 CTest。
- `windows-nmake-release-benchmark-minizip`：启用 benchmark 和 minizip backend，
  用于手工对比 stored bootstrap 与 minizip/DEFLATE 输出。

## 推荐流程

1. 修改前先确认当前 target 形态。
2. 如果添加 `.cpp`，同步更新顶层 `fastxlsx` 源文件列表，或新增明确的内部/测试 target。
3. 如果添加测试，挂到 `FASTXLSX_BUILD_TESTS`，并注册 CTest。
4. 如果添加示例，挂到 `FASTXLSX_BUILD_EXAMPLES`，并确认 `examples/`
   下的 target、输出文件和验证方式。
5. 如果添加 benchmark，挂到 `FASTXLSX_BUILD_BENCHMARKS`，不要调用
   `add_test()`，不要让它进入默认 CTest。
6. 生成物不进源码。`.gitignore` 已忽略 `build/`、CMake 生成文件、
   IDE 状态、二进制、日志和临时文件。

## 依赖处理

- 当前 `vcpkg.json` 是基础入口：默认依赖包含 `stb`；planned features 包含
  `minizip-ng`、`zlib-ng`、`expat`、`pugixml`、`catch2` 和 `benchmark`。
- 当前代码真正使用的第三方依赖是默认 `stb` 图片元数据 helper / streaming
  图片插入结构，以及 opt-in `minizip-ng[core,zlib]` backend。当前 minizip writer
  会显式关闭 data descriptor，用于生成当前 `PackageReader` 可验证的 DEFLATE
  fixture；这不代表 reader 已支持任意 data-descriptor ZIP input。
- 未使用的依赖不要提前加 `find_package` 或 link。
- 接入前必须验证真实 port 名、feature、imported target、triplet 行为和许可证。
- 依赖接入遵守 `fastxlsx-dependency-policy`。
- 默认不要用 `FetchContent` 拉核心依赖。
- 不要把第三方源码复制进 `src` 或 `include`。

## 本轮计划边界

- vcpkg：基础。manifest 默认依赖包含 `stb`；minizip backend 是 opt-in，不代表
  `zlib-ng`、`expat`、`pugixml`、Catch2 或 Benchmark 已接入源码。
- CMakePresets：基础。preset 可见，默认以 VS2026/MSVC 2026 + NMake + vcpkg 为主线。
- CI：基础。workflow 可见，目标是结构测试和 CTest 60s 门禁；默认 job 通过
  `ctest --preset windows-nmake-release` 运行，opt-in vcpkg matrix job 通过
  `ctest --preset windows-nmake-release-minizip` 运行。Excel 可视化验证仍是本地步骤，
  大型 benchmark 不进入 CI。

## 验证

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

验证 minizip backend：
```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

修改 CI、vcpkg features、minizip backend 或 image/stb 路径时，默认 preset
和相关 opt-in minizip preset 都要本地验证；推送后再确认对应 GitHub Actions job 真实通过。
构建手工 benchmark：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
```

直接运行 `fastxlsx_bench_streaming_writer` 且不传 `--output` / `--result` 时，
默认结果写到 benchmark target 的 binary dir。该手工工具限制 `--sheets <= 1024`；
不要把这个工具边界写成 FastXLSX public API 的 worksheet 数量承诺。
当前 benchmark JSON schema version 为 `3`，会记录 string pattern、package entry
source mode 和 `temporary_worksheet_part_footprint="worksheet-body-file-bytes"`；数值型
`temporary_worksheet_part_footprint_bytes` 只统计 benchmark-only worksheet body row
XML 写入字节，不是完整低内存、完整 package 或进程峰值内存数据。

如果 `ctest` 没有运行测试，先检查 `tests/CMakeLists.txt` 是否仍注册
`fastxlsx.unit`、`fastxlsx.streaming`、`fastxlsx.opc` 和 `fastxlsx.image`。
后台运行普通测试时使用 60s 超时；preset 和 `tests/CMakeLists.txt` 已承载该
边界。手写 `build-nmake` 排障目录时，显式加 `ctest --test-dir ... --timeout 60`。
