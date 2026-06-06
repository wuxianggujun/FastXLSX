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

本项目主开发环境是 Visual Studio 2026 / MSVC 2026。当前本机已验证
VS2026 Developer Command Prompt + NMake。

示例：

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

如果其他机器上的 Visual Studio 2026 对应了新的 CMake 生成器名称，应使用本机
CMake 支持的实际生成器名称。不要把旧版 Visual Studio 生成器名称作为默认事实。

可以用下面命令查看可用生成器：

```bash
cmake --help
```

## 依赖管理

FastXLSX 推荐使用 `vcpkg` manifest mode 管理第三方依赖。

初始依赖边界记录在 [依赖策略](DEPENDENCIES.md)：

- `minizip-ng`
- `zlib-ng` / `zlib`
- `Expat`
- `pugixml`
- `Catch2`
- `Google Benchmark`

后续应通过项目根目录的 `vcpkg.json` 固定直接依赖和 baseline。
在正式接入代码前，文档先固定依赖方向，不在 CMake 中提前引入未使用的库。

## 兼容目标

当前优先级：

1. Windows + MSVC 2026
2. Windows + 其他 MSVC 版本
3. Linux + GCC / Clang
4. macOS + Clang

跨平台支持是长期目标，但早期开发不应该为了兼容所有平台牺牲核心架构速度。
