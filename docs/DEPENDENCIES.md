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
- `planned-runtime` 记录计划中的运行依赖：
  `minizip-ng[zlib]`、`zlib-ng`、`expat`、`pugixml`。
- `planned-dev` 记录计划中的开发依赖：`catch2`、`benchmark`。
- 本机已用 `vcpkg search` 确认上述 port 名称存在。
- 本机已用 `vcpkg install --dry-run --x-feature=planned-runtime`
  和 `--x-feature=planned-dev` 确认可选 feature 可解析到依赖图。
- 尚未验证这些 port 对应的 CMake package 名称和 imported target 名称。
- 因此当前不在 `CMakeLists.txt` 中添加 `find_package` 或链接关系。

后续真正接入依赖时，必须先验证：

1. vcpkg port 名称、features 和目标 triplet。
2. CMake config package 名称和 imported target 名称。
3. MSVC 2026 / NMake preset 下的配置、构建和测试。
4. CI 上的安装耗时、缓存策略和失败行为。

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
- `Catch2`：Boost Software License。
- `Google Benchmark`：Apache License 2.0。
- `fast_float`：Apache License 2.0 / MIT / Boost 三选一授权。
- `simdutf`：Apache License 2.0 / MIT 双授权。
- `fmt`：MIT License。

正式发布前需要生成第三方依赖清单，并在发行包中保留对应 license 文本。
