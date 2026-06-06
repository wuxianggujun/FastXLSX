---
name: fastxlsx-dependency-policy
description: "应用 FastXLSX 依赖边界。用于添加、移除、vendoring 或修改第三方库、CMake 依赖发现、vcpkg manifest、license 记录、OpenXLSX/xlnt benchmark 参考、XML/ZIP/DOM/parser 库，以及 fast_float/simdutf/fmt 等可选性能依赖。"
---

# FastXLSX Dependency Policy

## 必读文件

- `docs/DEPENDENCIES.md`
- `docs/DEVELOPMENT_ENVIRONMENT.md`
- `docs/ARCHITECTURE.md`
- `README.md`
- `CMakeLists.txt`

改依赖配置前，先读取根目录 `vcpkg.json`。当前仓库已有保守 manifest：
默认 `dependencies` 为空，`planned-runtime` 和 `planned-dev` 只记录待接入依赖；
这些依赖尚未通过 CMake `find_package` / link 接入。

## 核心原则

通用底层能力使用成熟库。XLSX 语义层和性能热路径由 FastXLSX 自己实现。

第一阶段规划的运行依赖：

- `minizip-ng`：ZIP package 和 entry streaming。
- `zlib-ng / zlib`：DEFLATE 压缩，保留 `zlib` fallback。
- `Expat`：大型 XML event parser。
- `pugixml`：小型 XML 局部 DOM 编辑。

开发依赖：

- `Catch2`：单元测试。
- `Google Benchmark`：性能基准。

## FastXLSX 自研范围

不要把这些交给第三方 XLSX 库：

- OPC part 索引。
- relationships 管理。
- worksheet row/cell 编码。
- `sharedStrings` / `inlineStr` 策略。
- styles registry 和 style XML 生成。
- part-level rewrite。
- `FastXmlWriter`、`CellEncoder`、`RowStreamWriter` 热路径。

## 包管理规则

- 优先使用 `vcpkg` manifest mode。
- 依赖真正接入时，在根目录 `vcpkg.json` 和 baseline 中锁定。
- CMake 侧优先 `find_package`。
- 默认不要用 `FetchContent` 拉核心依赖。
- 不要把第三方源码复制进 `src` 或 `include`。
- 不在叙述文档里硬编码依赖版本；版本由 manifest/CI 锁定。

## Phase 1 ZIP bootstrap 例外

当前 `src/zip_store_writer.*` 是内部 bootstrap writer，只用于打通 Phase 1
OpenXML、CTest 和本机 Excel 验证。

限制：

- stored/no compression。
- 无 Zip64。
- 无真正 package entry streaming。
- 不进入 public API。
- 不作为性能、压缩率或长期 ZIP 后端的依据。

生产路线仍是 `vcpkg` manifest mode + `minizip-ng` / `zlib-ng`，后续接入时应
保留现有 OpenXML 结构测试，并把压缩后端测试从 OpenXML 语义测试中拆开。

## 明确不作为底座的库

以下库只能作为参考、经验来源或 benchmark 对象：

- `OpenXLSX`
- `xlnt`
- `libxlsxwriter`
- `QXlsx`

不要把它们接成 FastXLSX 运行时依赖。

## 后期可选依赖

只有 benchmark 或真实需求证明必要时才引入：

- `fast_float`：大量数字解析。
- `simdutf`：UTF 校验/转换。
- `fmt`：非热路径日志或诊断，且仅当 `std::format` 不足。

不要在没有测量的情况下把格式化库放进 cell XML 热路径。

## License 检查

新增依赖时记录许可证，并在发布包中保留所需 license 文本。FastXLSX 项目本身
使用 MIT License，见根目录 `LICENSE`。

## 验证

- 确认 `vcpkg.json` 与 CMake 中的包名真实有效。
- 使用目标生成器和 toolchain 配置。
- 添加 `find_package` 和链接关系后必须构建。
- 影响热路径的依赖变更必须补 benchmark，不能凭直觉判断性能。
