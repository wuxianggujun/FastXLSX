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
默认 `dependencies` 为空，`planned-runtime`、`planned-image` 和 `planned-dev`
记录 opt-in 或计划依赖。当前 `planned-runtime` 中的
`minizip-ng[core,zlib]` 通过 `FASTXLSX_ENABLE_MINIZIP_NG` 接入 CMake；
`planned-image` 中的 `stb` 通过 `FASTXLSX_ENABLE_STB` 接入 PNG/JPEG
`read_image_info()` 元数据 helper，并支撑 `WorksheetWriter::add_image()` 的
streaming-only new-workbook 图片插入基础切片。

## 核心原则

通用底层能力使用成熟库。XLSX 语义层和性能热路径由 FastXLSX 自己实现。

第一阶段规划的运行依赖：

- `minizip-ng`：ZIP package 和 entry streaming。
- `zlib-ng / zlib`：DEFLATE 压缩，保留 `zlib` fallback。
- `Expat`：大型 XML event parser。
- `pugixml`：小型 XML 局部 DOM 编辑。

Phase 5 图片读取/插入规划依赖：

- `stb`：图片解码、尺寸和像素读取。当前本机 vcpkg port 名称已验证为
  `stb`，license 为 `MIT OR CC-PDDC`。它只属于 opt-in `planned-image`，
  当前通过 `FASTXLSX_ENABLE_STB` 接入 CMake，用法是
  `find_package(Stb REQUIRED)` 和 `${Stb_INCLUDE_DIR}`，当前未见 imported target。

P17 图片依赖必须按阶段处理：P17a 已接入 opt-in `FASTXLSX_ENABLE_STB` 和
PNG/JPEG `read_image_info()` 元数据 API；P17b 已有
`WorksheetWriter::add_image()` 的 streaming-only new-workbook PNG/JPEG 基础插入切片。
这只能说明当前窄范围 OpenXML media/drawing packaging 已通过结构测试和本机 Excel
验证，不能代表 existing-workbook 图片保真、drawing 编辑、裁剪、旋转、压缩或格式转换。
当前 `ImageOptions` name/description 只是 drawing XML `xdr:cNvPr` metadata，不新增
依赖，也不改变 `stb` 只负责图片解码/尺寸/像素读取的边界。

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

生产路线仍是 `vcpkg` manifest mode。当前已有 verified opt-in
`minizip-ng[core,zlib]` backend；它使用 vcpkg `zlib`，不是 `zlib-ng`。后续接入
其他压缩或 XML 依赖时应保留现有 OpenXML 结构测试，并把压缩后端测试从
OpenXML 语义测试中拆开。

当前 `src/package_writer.*` 已作为内部 package writer boundary 存在。默认 backend
调用 stored ZIP bootstrap；`FASTXLSX_ENABLE_MINIZIP_NG=ON` 时调用 minizip-ng
DEFLATE backend。不要让 workbook writer 代码重新直接依赖 ZIP 细节。

## 当前 P2 依赖发现事实

- `minizip-ng[core,zlib]` 在当前 vcpkg metadata 中依赖 `zlib` port，不是 `zlib-ng`。
- `minizip-ng` 默认 features 包含 BZIP2/LZMA/ZSTD/加密等非必要能力；项目 manifest
  必须保留 `default-features: false` 和只启用 `zlib` 的约束，除非任务明确改策略。
- `minizip-ng` 用法已验证为 `find_package(minizip-ng CONFIG REQUIRED)` /
  `MINIZIP::minizip-ng`。
- `zlib` 用法已知为 `find_package(ZLIB REQUIRED)` / `ZLIB::ZLIB`。
- `zlib-ng`、`expat`、`pugixml` 已能随 planned-runtime clean install，但当前源码
  尚未链接或使用它们。
- `windows-nmake-release-vcpkg` preset 只启用 toolchain；默认不会安装
  `planned-runtime` feature。
- `windows-nmake-release-minizip` preset 会启用 `planned-runtime` 和
  `FASTXLSX_ENABLE_MINIZIP_NG=ON`。

## 明确不作为底座的库

以下库只能作为参考、经验来源或 benchmark 对象：

- `OpenXLSX`
- `xlnt`
- `libxlsxwriter`
- `QXlsx`

不要把它们接成 FastXLSX 运行时依赖。

## 后期可选依赖

只有 benchmark 或真实需求证明必要时才引入：

- `stb`：Phase 5 图片读取/插入需要的轻量图片解码库；只负责解码和尺寸/像素读取，
  不负责 OpenXML drawing/media packaging。
- `fast_float`：大量数字解析。
- `simdutf`：UTF 校验/转换。
- `fmt`：非热路径日志或诊断，且仅当 `std::format` 不足。

不要在没有测量的情况下把格式化库放进 cell XML 热路径。

## License 检查

新增依赖时记录许可证，并在发布包中保留所需 license 文本。FastXLSX 项目本身
使用 MIT License，见根目录 `LICENSE`。

当前新增图片依赖事实：`stb` vcpkg port 存在，描述为 public domain header-only
libraries，license 是 `MIT OR CC-PDDC`。正式接入前仍需验证 CMake package /
include 路径和 CI 安装行为。

接入 `stb` 时，`STB_IMAGE_IMPLEMENTATION` 等 implementation macro 只能放在一个
`.cpp` 中，避免 ODR 问题和编译膨胀。若只是嵌入已有 PNG/JPEG，优先保留原始
图片字节并维护 OpenXML package，不要无意义地解码再重编码。
图片 public API 或任务文档必须说明原始图片字节、decoded pixel buffer 和 OpenXML
part state 的内存边界；不要为了 API 易用性把图片处理放进 worksheet row/cell 热路径。

## 验证

- 确认 `vcpkg.json` 与 CMake 中的包名真实有效。
- 图片依赖阶段先确认 `vcpkg install --dry-run --x-feature=planned-image` 可解析。
- 使用目标生成器和 toolchain 配置。
- 添加 `find_package` 和链接关系后必须构建。
- 影响热路径的依赖变更必须补 benchmark，不能凭直觉判断性能。
