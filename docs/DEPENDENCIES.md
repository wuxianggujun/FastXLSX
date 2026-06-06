# 依赖策略

## 核心原则

FastXLSX 不追求全部自研，也不应该把大型第三方 XLSX 库作为底座。

依赖边界固定为：

```text
通用底层能力：使用成熟库
XLSX 语义层：FastXLSX 自研
性能热路径：FastXLSX 自研
参考库：只参考，不依赖
```

这样可以避免两个问题：

- 全部自研会把大量时间浪费在 ZIP、DEFLATE、通用 XML 和测试框架上。
- 直接依赖 `OpenXLSX` 或 `xlnt` 会被它们的架构限制，无法形成 FastXLSX 自己的性能主线。

## 第一阶段运行依赖

### 1. minizip-ng

用途：

- ZIP container 读写。
- XLSX package entry 读写。
- Zip64 支持。
- 流式写入 ZIP entry。

选择原因：

- XLSX 本质是 OPC package，底层就是 ZIP。
- 大文件需要 Zip64 和稳定的 entry streaming。
- 比自己实现 ZIP 中央目录、CRC、压缩 entry 更可靠。

边界：

- 只使用 XLSX 需要的 ZIP 能力。
- 不启用加密、BZIP2、LZMA、ZSTD 等非必要功能。
- ZIP entry 的 OpenXML 语义由 FastXLSX 自己管理。

### 2. zlib-ng / zlib

用途：

- DEFLATE 压缩。
- ZIP entry 压缩后端。

选择策略：

- 优先支持 `zlib-ng`。
- 保留 `zlib` fallback。

边界：

- 压缩等级必须可配置。
- 默认压缩等级应偏向导出性能，而不是最小文件体积。
- 第一阶段不把 `libdeflate` 作为主压缩路径。

### 3. Expat

用途：

- 大型 XML part 的流式读取。
- 大型 `worksheet.xml` 读取。
- 模板 sheet 流式替换。
- 大型 `sharedStrings.xml` 扫描或重写。

选择原因：

- FastXLSX 需要 SAX / event 风格 parser。
- 大型 worksheet 不能进入 DOM。
- Expat 足够成熟，依赖少，适合封装成 `EventReader`。

边界：

- Expat 只负责 XML token 和事件。
- Cell、row、formula、style id 等 XLSX 语义由 FastXLSX 自己解析。
- 不把 Expat 的 callback 直接暴露给高层 API。

### 4. pugixml

用途：

- 小型 XML part 的局部 DOM 编辑。

适用 part：

- `workbook.xml`
- `workbook.xml.rels`
- `[Content_Types].xml`
- `docProps/*.xml`
- 小型 `styles.xml`
- 小型 table / comments / drawing part

选择原因：

- DOM 不应该用于大型 worksheet。
- 但完全拒绝 DOM 会让小型 XML 修改变得复杂。
- pugixml 足够快，接口简单，适合做 `OptionalDomDocument`。

边界：

- 禁止在大型 `worksheet.xml` 主路径使用 pugixml。
- 禁止在百万行导出路径间接触发 DOM。
- 是否允许 DOM 应该由 part 大小、part 类型和调用路径共同决定。

## Phase 5 图片依赖

### stb

用途：

- 图片读取和解码。
- 获取图片尺寸、通道数和像素数据。
- 后续图片插入、图片读取或图片验证任务中的轻量解码层。

选择原因：

- `stb` 是 header-only 库，适合 FastXLSX 保持轻量依赖边界。
- 本机 vcpkg metadata 已确认 port 名称为 `stb`，描述为 public domain
  header-only libraries，license 为 `MIT OR CC-PDDC`。
- 本机 vcpkg usage 显示 CMake 用法是 `find_package(Stb REQUIRED)` 和
  `${Stb_INCLUDE_DIR}`，当前未见 imported target。
- 图片功能需要解码层，但不需要引入 OpenCV、FreeImage、Qt 等重型图像框架。

边界：

- `stb` 只负责图片文件解码、尺寸和像素读取。
- OpenXML media part、drawing XML、drawing relationships、worksheet
  relationships、content types、anchor 计算和 package preservation 由
  FastXLSX 自己实现。
- 不要因为接入 `stb` 就宣称完整图片插入、图片编辑、drawing 编辑或已有文件图片保真
  已完成；当前只可宣称 `WorksheetWriter::add_image()` 的 streaming-only
  new-workbook PNG/JPEG 基础插入切片。
- 不要把图片解码放进 worksheet row/cell 热路径。
- `STB_IMAGE_IMPLEMENTATION` 等 implementation macro 只能放在一个 `.cpp` 中。
- 如果只是嵌入已有 PNG/JPEG，优先保留原始图片字节并正确写 OpenXML package；
  不要无意义地解码再重编码。

P17 图片任务的依赖接入应按阶段推进：

1. 依赖发现阶段验证 `planned-image` / `stb` 的 vcpkg feature、include 路径、
   license、CMake package/include 行为和 CI 安装成本。当前 P17a 已把
   `FASTXLSX_ENABLE_STB=ON` 作为 opt-in CMake 接入口，用于 `read_image_info()`
   PNG/JPEG 元数据 helper。
2. API 设计阶段先写清 Streaming / Patch / In-memory 模式、原始图片字节和 decoded
   pixel buffer 的内存边界、OpenXML package 副作用，以及为什么不会破坏 worksheet
   streaming 热路径。
3. New-workbook 插入阶段必须把 media part、drawing XML、drawing relationships、
   worksheet relationships、worksheet `<drawing>` 引用和 content types 一起验证；
   当前 P17b 已有 `WorksheetWriter::add_image(path, anchor)` 基础切片，会保留原始
   PNG/JPEG 字节为 file-backed media entry，并写出生成的 drawing/media package 结构。
   不能只凭 `stb` 解码可用就宣称更广泛的图片 OpenXML 支持。
4. Existing-workbook 图片读取、编辑或保真复制必须等 package reader/writer 和保真
   fixtures 证明未修改 media/drawing/chart/VBA/unknown parts 能保留后再推进。

## 开发依赖

### 1. Catch2

用途：

- 单元测试。
- XML 编码边界测试。
- worksheet writer / rewriter 行为测试。
- OPC package 结构测试。

要求：

- 测试必须能在 60s 超时限制内跑完核心集。
- 大型性能测试不要混入普通单元测试。

### 2. Google Benchmark

用途：

- 写入吞吐量基准。
- XML escape 基准。
- cell reference 编码基准。
- 数字转字符串基准。
- ZIP 压缩等级基准。

基准对象：

- `FastXLSX`
- `OpenXLSX`
- `xlnt` streaming writer
- 旧 `FastExcel`

## 后期可选依赖

### 1. fast_float

用途：

- 大量数字读取。
- 文本数字快速解析。

策略：

- 第一阶段写入数字优先使用 `std::to_chars`。
- 读取路径如果 benchmark 证明瓶颈明显，再引入 `fast_float`。

### 2. simdutf

用途：

- 高速 UTF-8 校验。
- UTF-16 / UTF-8 转换。
- 后续跨平台路径和字符串输入增强。

策略：

- 第一阶段内部统一 UTF-8。
- 只有当编码校验或转码成为明确需求时再加入。

### 3. fmt

用途：

- 非热路径日志。
- 调试输出。
- 错误信息格式化。

策略：

- 不进入 cell XML 写入热路径。
- 如果 `std::format` 在目标 MSVC 2026 环境表现足够稳定，可以不引入 `fmt`。

## 明确不作为依赖

### OpenXLSX

用途定位：

- 对标对象。
- API 体验参考。
- 兼容性样例参考。

不作为依赖的原因：

- 核心是 DOM 编辑优先。
- 大型 worksheet 路径不符合 FastXLSX 的流式主线。

### xlnt

用途定位：

- producer / consumer 分层参考。
- streaming reader / writer API 参考。
- benchmark 对比对象。

不作为依赖的原因：

- 常规 API 仍以完整 workbook / worksheet 内存模型为中心。
- 直接依赖会限制 FastXLSX 的大文件主路径设计。

### libxlsxwriter

用途定位：

- 可作为纯写入库的对比参考。

不作为依赖的原因：

- 重点是生成，不是编辑已有 XLSX。
- 不满足 FastXLSX 的 part-level rewrite 和局部 DOM 编辑目标。

### QXlsx

用途定位：

- Qt 生态参考。

不作为依赖的原因：

- 引入 Qt 依赖过重。
- 不符合 FastXLSX 的独立 C++ 库定位。

## 依赖数量目标

第一阶段控制在：

```text
运行依赖：4 个
开发依赖：2 个
后期可选依赖：按 benchmark 和真实需求决定
```

也就是：

```text
minizip-ng
zlib-ng / zlib
Expat
pugixml
Catch2
Google Benchmark
```

这比 `xlnt` 更克制，同时比 `OpenXLSX` 更适合大文件流式处理。

## 包管理策略

推荐使用 `vcpkg` manifest mode 管理依赖。

原则：

- 项目根目录用 `vcpkg.json` 作为依赖入口。
- 正式接入的直接依赖应通过 baseline 锁定版本。
- CMake 侧优先使用 `find_package`。
- 默认不通过 `FetchContent` 在配置阶段自动拉取核心依赖。
- 不把第三方源码直接复制进 `src` 或 `include`。

当前仓库已有保守 `vcpkg.json`。

这个 manifest 的边界是：

- 默认 `dependencies` 为空。
- 默认 CMake 配置和 CI 不安装、不链接任何外部 vcpkg 包。
- `planned-runtime` 现在是 opt-in 运行依赖 feature：当前源码只使用其中的
  `minizip-ng[core,zlib]` 作为 package writer backend；`zlib-ng`、`expat`
  和 `pugixml` 仍是后续 ZIP/XML reader/editor 工作的计划依赖。
- `planned-image` 记录图片解码依赖：`stb`。当前通过
  `FASTXLSX_ENABLE_STB=ON` 接入 PNG/JPEG `read_image_info()` 元数据 helper，并支撑
  `WorksheetWriter::add_image()` 的 streaming-only new-workbook PNG/JPEG 基础插入切片；
  它不属于默认构建，也不代表 existing-workbook 图片保真或完整 drawing 编辑。
- `planned-dev` 记录计划中的开发依赖：`catch2`、`benchmark`。
- 本机已用 `vcpkg search` 确认上述 port 名称存在。
- 本机已用 `vcpkg install --dry-run --x-feature=planned-runtime`、
  `--x-feature=planned-image` 和 `--x-feature=planned-dev` 确认可选
  feature 可解析到依赖图。
- 已验证 `FASTXLSX_ENABLE_MINIZIP_NG=ON` 时 CMake 使用
  `find_package(minizip-ng CONFIG REQUIRED)` 并链接 `MINIZIP::minizip-ng`。
  默认无 vcpkg 构建仍不会触发该依赖。

### 当前 P2/P4 dependency and backend 记录

本机 clean manifest 配置、构建和 CTest 已确认以下事实：

- `minizip-ng` 当前 vcpkg ports tree 中可见版本为 `4.1.0`，license 为
  `Zlib`。其 `zlib` feature 依赖 vcpkg `zlib` port；当前 portfile 还显式禁用
  `ZLIBNG` 查找。这意味着 `minizip-ng[zlib]` 并不等同于“minizip-ng 已使用
  zlib-ng”。
- `minizip-ng[core,zlib]` clean install 后提供 CMake target：
  `find_package(minizip-ng CONFIG REQUIRED)` / `MINIZIP::minizip-ng`。
- `CMakeLists.txt` 通过 `FASTXLSX_ENABLE_MINIZIP_NG` opt-in 开关接入该 target，
  并定义 `FASTXLSX_HAS_MINIZIP_NG` 选择 minizip-ng DEFLATE backend。
- `zlib` 当前可见版本为 `1.3.2#1`，license 为 `Zlib`。vcpkg usage 明确给出
  `find_package(ZLIB REQUIRED)` 和 `ZLIB::ZLIB`。
- `zlib-ng` clean install 提示 target 为 `zlib-ng::zlib`，但当前 FastXLSX
  源码尚未链接或调用 zlib-ng。
- `expat` clean install 提示 target 为 `expat::expat`，但当前 FastXLSX 源码
  尚未接入 XML reader。
- `pugixml` clean install 提示 target 为 `pugixml::shared` 和
  `pugixml::pugixml`，但当前 FastXLSX 源码尚未接入 DOM editing。
- Windows 下 `minizip-ng` portfile 有 static-only 约束；发布包需要保留 Zlib
  license notice。

当前 preset 边界：

- `windows-nmake-release`：默认无 vcpkg、stored ZIP bootstrap。
- `windows-nmake-release-vcpkg`：只配置 vcpkg toolchain，不启用 minizip backend。
- `windows-nmake-release-minizip`：启用 `FASTXLSX_ENABLE_MINIZIP_NG=ON` 和
  `VCPKG_MANIFEST_FEATURES=planned-runtime`，验证 opt-in minizip backend。

本机已验证命令：

```powershell
cmake -S . -B build/windows-nmake-release-vcpkg-local -G "NMake Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DFASTXLSX_BUILD_TESTS=ON `
  -DFASTXLSX_ENABLE_MINIZIP_NG=ON `
  -DCMAKE_TOOLCHAIN_FILE=D:/Programs/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_MANIFEST_FEATURES=planned-runtime
cmake --build build/windows-nmake-release-vcpkg-local
ctest --test-dir build/windows-nmake-release-vcpkg-local --output-on-failure --timeout 60
```

生成的 representative `.xlsx` 中 ZIP central directory method 为 `8`
（DEFLATE），证明测试覆盖了 minizip backend，而不是误走 stored fallback。

后续 CI 接入仍需单独验证 GitHub Actions 上的 vcpkg 缓存、安装耗时和失败行为。

对于 `stb`，已知 vcpkg CMake 用法目前是 `find_package(Stb REQUIRED)` 加
`${Stb_INCLUDE_DIR}`，而不是 imported target。正式接入时要以本机和 CI 的实际
toolchain 输出为准。

暂不在叙述文档里硬编码具体版本号。
版本应由 `vcpkg.json` baseline 和 CI 环境共同锁定。

## 自研范围

必须由 FastXLSX 自己实现：

- `PackageReader`
- `PackageWriter`
- `PartIndex`
- `RelationshipGraph`
- `FastXmlWriter`
- `EventReader` 封装层
- `RowStreamWriter`
- `RowStreamReader`
- `WorksheetRewriter`
- `CellEncoder`
- `DimensionTracker`
- `StyleRegistry`
- `SharedStringTable`
- `InlineStringPolicy`

当前状态不同步等同：`PartIndex` / `RelationshipGraph` 已有内部基础；
新建 workbook 输出已有 opt-in minizip package writer backend；`PackageReader`、
已有文件编辑、unknown part preservation 和 public `PackageWriter` 仍是计划，
不能从自研范围列表推导为已有文件编辑已实现。

其中 `FastXmlWriter`、`CellEncoder`、`RowStreamWriter` 是最重要的性能热路径。
这些模块不应该交给通用 XML serializer。

## 许可证记录

FastXLSX 项目本身使用 MIT License，见根目录 `LICENSE`。

初始依赖均应选择宽松许可证。

当前记录：

- `minizip-ng`：zlib license。
- `zlib-ng` / `zlib`：zlib license。
- `pugixml`：MIT License。
- `Expat`：MIT License。
- `stb`：MIT OR CC-PDDC。
- `Catch2`：Boost Software License。
- `Google Benchmark`：Apache License 2.0。
- `fast_float`：Apache License 2.0 / MIT / Boost 三选一授权。
- `simdutf`：Apache License 2.0 / MIT 双授权。
- `fmt`：MIT License。

正式发布前需要生成第三方依赖清单，并在发行包中保留对应 license 文本。
