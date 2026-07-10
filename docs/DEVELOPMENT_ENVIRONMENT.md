# 开发环境

## 基线

- C++20。
- CMake 3.20+；presets schema 需要 CMake 3.21+。
- Visual Studio 2026 / MSVC 2026 优先。
- vcpkg manifest mode，`VCPKG_ROOT` 指向可用 vcpkg。
- 所有文档/源码使用 UTF-8 与仓库既有 LF 策略。

## Production

在 VS Developer Command Prompt：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

该 profile 默认启用 DEFLATE reader/writer 与 images。

## Stored bootstrap

```powershell
cmake --preset windows-nmake-release-stored
cmake --build --preset windows-nmake-release-stored
ctest --preset windows-nmake-release-stored
```

只支持 stored ZIP entries，不代表常见 Excel DEFLATE workbook 的 production 编辑能力。

## No-images

```powershell
cmake --preset windows-nmake-release-no-images
cmake --build --preset windows-nmake-release-no-images
build\windows-nmake-release-no-images\fastxlsx_image_disabled_smoke.exe
```

该 profile 不安装 stb，public image call 必须抛 feature-disabled error。

## 常见环境错误

普通 PowerShell/CMD 未加载 MSVC 环境时，NMake preset 会在 `project()` 阶段报告找不到 `nmake` 或 C++ compiler。先运行 `vcvars64.bat`，再确认：

```powershell
where cl
where nmake
cmake --version
```

不要使用旧 `build/CMakeCache.txt` 推断推荐配置。