# 开发环境

## 主开发环境

FastXLSX 的主开发环境记录为：

```text
操作系统：Windows
主编译器：MSVC 2026
C++ 标准：C++20
构建系统：CMake
依赖管理：vcpkg manifest mode
```

MSVC 2026 是本项目的优先目标环境。其他编译器可以后续补充支持，但不作为
当前阶段的第一优先级。

## CMake 和生成器的关系

CMake 是配置工具，不是实际编译器。

在 Windows 上，CMake 需要选择一个生成器，例如：

```text
Visual Studio 生成器
NMake Makefiles
Ninja
```

如果没有显式指定 `-G`，CMake 会自动选择一个默认生成器。

之前配置失败的原因是：

```text
CMake 自动选择了 NMake Makefiles
但当前终端环境没有 nmake
```

这不是项目代码问题，而是生成器环境问题。

## 推荐配置方式

本项目主开发环境是 Visual Studio 2026 / MSVC 2026。当前推荐并已验证的
入口是 VS2026 Developer Command Prompt + `windows-nmake-release` preset。

示例：

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

只有在排查 preset 外的生成器问题时，才手写 `-B build-nmake -G "NMake Makefiles"`；
这种手写目录跑 CTest 时必须显式加 `--timeout 60`。

如果其他机器上的 Visual Studio 2026 对应了新的 CMake 生成器名称，应使用本机
CMake 支持的实际生成器名称。不要把旧版 Visual Studio 生成器名称作为默认事实。

可以用下面命令查看可用生成器：

```bash
cmake --help
```

## 本机核验结果

2026-06-06 本机普通 PowerShell 核验结果：

- `vcpkg` 可用，路径来自 `VCPKG_ROOT`。
- 本机 `VCPKG_ROOT` 指向一个有效 vcpkg 根目录，且
  `scripts/buildsystems/vcpkg.cmake` 存在。
- `vcpkg version` 为 `2026-05-27` 版本线。
- 本机 `cmake --version` 为 `3.31.1`。
- 本机 `cmake --help` 显示的默认生成器是 `NMake Makefiles`。
- 当前推荐且已验证路径是 VS2026 Developer Command Prompt + `NMake Makefiles`。
- 其他可见生成器只代表本机 `cmake --help` 输出，不作为 FastXLSX 当前推荐或验证路径。
  不要使用 Visual Studio 2022 / VS17 作为主线配置。

上述结果只描述当前机器事实。其他机器应以本机 `cmake --help`、
`where vcpkg` / `Get-Command vcpkg` 和实际 Visual Studio 安装为准。

注意：进入 Visual Studio Developer Command Prompt 后，Visual Studio 可能会
重新设置 `VCPKG_ROOT`，例如指向 VS 自带的 vcpkg。运行 vcpkg preset 前，
应在 DevCmd 环境里再次确认 `VCPKG_ROOT` 的实际值；如果要使用指定 vcpkg，
应在调用 `VsDevCmd.bat` 之后重新设置 `VCPKG_ROOT`。

## CMake Presets

仓库根目录提供 `CMakePresets.json` 作为开源工程入口。

`CMakePresets.json` 使用 schema version 3，要求 CMake 3.21+。这只是
preset 文件要求；项目 `CMakeLists.txt` 的最低版本仍是 3.20。

当前 preset 分为两类：

- `windows-nmake-release`：默认推荐入口，不使用 vcpkg，不安装外部依赖。
- `windows-nmake-release-vcpkg`：显式使用
  `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`，用于后续验证
  vcpkg manifest 和依赖接入。
- `windows-nmake-release-minizip`：显式使用 vcpkg toolchain，并启用
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` 与 `VCPKG_MANIFEST_FEATURES=planned-runtime`，
  用于验证 opt-in minizip-ng package writer backend。
- `windows-nmake-release-benchmark`：显式启用手工 benchmark target，不使用
  vcpkg，不注册默认 CTest。
- `windows-nmake-release-benchmark-minizip`：显式启用手工 benchmark target 和
  opt-in minizip backend，用于本地性能对比。

在 VS2026 Developer Command Prompt 中可以运行：

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

如果要验证 vcpkg toolchain preset，先设置 `VCPKG_ROOT` 指向要使用的
vcpkg 根目录，并确认该环境变量没有被 DevCmd 改写，再运行：

```powershell
cmake --preset windows-nmake-release-vcpkg
cmake --build --preset windows-nmake-release-vcpkg
ctest --preset windows-nmake-release-vcpkg
```

验证 opt-in minizip-ng backend 时运行：

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

注意：当前 `vcpkg.json` 默认不拉取 FastXLSX 依赖。只有 minizip preset 或手写
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 加 `VCPKG_MANIFEST_FEATURES=planned-runtime`
才会安装并链接 `minizip-ng[core,zlib]`。`windows-nmake-release-vcpkg` 仍只是
toolchain 和 manifest 入口，不代表 XML parser、DOM editing 或测试框架已经接入。
首次运行 vcpkg preset 时，vcpkg 仍可能初始化自己的工具缓存，例如下载解压工具。

手工 benchmark 不进入默认 CTest：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
```

直接运行 `fastxlsx_bench_streaming_writer` 且不传 `--output` / `--result` 时，
默认结果写到 benchmark target 的 binary dir。手工工具限制 `--sheets <= 1024`，
用于避免极端输入污染 benchmark 结果；这不是默认 CTest 或 CI 路径。

## GitHub Actions CI

仓库提供 `.github/workflows/ci.yml`，当前 CI 只做 Windows build/test：

- runner 使用 GitHub 官方 `windows-2025-vs2026` 标签。
- workflow 通过 `vswhere` 定位 Visual Studio，再调用
  `VC\Auxiliary\Build\vcvars64.bat` 初始化 MSVC/NMake 环境。
- CMake 使用 `windows-nmake-release` preset。
- CI 不启用 vcpkg toolchain，不安装外部 vcpkg 依赖。
- CI 调用 `ctest --preset windows-nmake-release`。60s 超时来自
  `CMakePresets.json` 的 test preset 和 `tests/CMakeLists.txt` 的测试属性，
  避免普通单元测试卡死。

根据 GitHub Actions 官方 2026-05-14 changelog，`windows-2025-vs2026`
是 VS2026 镜像测试标签，`windows-latest` / `windows-2025` 的 VS2026
迁移计划从 2026-06-08 开始分阶段进行。因此当前 CI 明确绑定测试标签，
不把 `windows-latest` 已经等同于 VS2026 作为事实。
2026-06-08 之后应复查该 runner 标签和 `windows-latest` / `windows-2025`
实际镜像状态，再决定是否调整 workflow。

## 依赖管理

FastXLSX 推荐使用 `vcpkg` manifest mode 管理第三方依赖。

初始依赖边界记录在 [依赖策略](DEPENDENCIES.md)：

- `minizip-ng`
- `zlib-ng` / `zlib`
- `Expat`
- `pugixml`
- `stb`
- `Catch2`
- `Google Benchmark`

当前项目根目录已有保守的 `vcpkg.json`：

- 默认 `dependencies` 为空，保证普通配置和 CI 不依赖外部包。
- `planned-runtime` 是 opt-in feature。当前源码只使用其中的
  `minizip-ng[core,zlib]`；`zlib-ng`、`expat` 和 `pugixml` 仍是后续计划依赖。
- 已本机确认 `planned-runtime` clean install 可用，`minizip-ng` CMake 用法为
  `find_package(minizip-ng CONFIG REQUIRED)` 和 `MINIZIP::minizip-ng`。
- 在正式接入代码前，不在 CMake 中提前引入未使用的库。

## 兼容目标

当前优先级：

1. Windows + MSVC 2026
2. Windows + 其他 MSVC 版本
3. Linux + GCC / Clang
4. macOS + Clang

跨平台支持是长期目标，但早期开发不应该为了兼容所有平台牺牲核心架构速度。
