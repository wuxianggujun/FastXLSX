# FastXLSX 架构

## 核心判断

FastXLSX 共享 OpenXML/OPC package 底座，但不会用一个内部模型覆盖所有场景：

```text
Streaming  -> 新建大文件，按行增量输出
Patch      -> 编辑已有文件，part-level copy/rewrite/remove
In-memory  -> 小文件稀疏随机编辑，再 handoff 给 Patch
```

当前 public、internal、planned 和 non-goal 状态见 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)。

## 模块边界

### Public Facade

- `Workbook`：小型新 workbook convenience API。
- `WorkbookWriter`：large ordered export 的 Streaming facade。
- `WorkbookEditor`：existing-workbook Patch facade。
- `WorksheetEditor`：small-file In-memory borrowed handle。

### OpenXML 语义层

- workbook、worksheet、styles、sharedStrings、formula、metadata、image 和 document properties 语义。
- 负责 Excel/OpenXML 规则、索引和跨 part side effect。
- 不能外包给 ZIP/XML 库，也不能把 internal helper 直接暴露为 public API。

### OPC Package 层

- package entry 读取与写入。
- `[Content_Types].xml`、relationships、part index 和 dependency audit。
- unchanged part copy-original，changed part rewrite/remove。
- unknown part 默认保留。

### 基础设施层

- production 默认 minizip-ng backend，并保留显式 stored-only bootstrap profile。
- stb 图片 metadata/decoding。
- 规划使用的 Expat/pugixml 等只承担通用 XML 能力。

## 数据流

### Streaming

```text
用户 row/cell input
  -> Cell/row validation
  -> hot-path XML encoding
  -> worksheet temporary/file-backed entry
  -> workbook/package metadata
  -> ZIP package close
```

大型 worksheet 不进入完整 DOM，也不构建 dense cell matrix。

### Patch

```text
source XLSX
  -> PackageReader / OPC index
  -> public edit request
  -> dependency audit + internal EditPlan
  -> compressed strict cell replace: one inflate -> owned temp -> target scan -> file ranges
  -> copy-original / stream-rewrite / local-DOM-rewrite / remove
  -> save_as(new path)
```

`EditPlan` 是 internal traceability，不是 public package mutation surface。Targeted strict replace 的临时文件只保存解压后的 worksheet bytes，不构建 worksheet DOM 或 dense cell map；文件所有权随 staged transaction 发布，失败前由 RAII 清理。Minizip writer 对同路径 ranges 复用输入句柄，避免每个 replacement range 重开文件。

### In-memory

```text
source worksheet
  -> guarded sparse materialization
  -> WorksheetEditor random edits
  -> dirty session diagnostics
  -> Patch handoff during save_as()
```

该路径的内存随 sparse cells、字符串和公式 owning values 增长。超过 `max_cells` 或 `memory_budget_bytes` 必须在状态污染前失败。

## DOM 使用边界

允许局部 DOM 的典型小型 part：

- `workbook.xml`
- relationship files
- `[Content_Types].xml`
- `docProps/*.xml`
- 较小的 styles、drawing、comments 或 table metadata part

禁止完整 DOM 的典型热路径：

- 大型 `worksheet.xml`
- 大型 `sharedStrings.xml`
- 批量新建 workbook
- 大型模板 sheet rewrite

原则是“大 part 流式，小 part 可局部 DOM”，而不是“全部 streaming”或“全部 DOM”。

## 文件职责

- `src/streaming_writer.cpp` 负责 Streaming 协调和热路径入口，不应无限承载每个 feature 的全部 serializer。
- Feature 已拥有独立状态、XML、验证和大量测试时，应拆分内部实现与 feature-specific tests。
- `src/package_editor.cpp`、`src/package_reader.cpp` 和 OPC helpers 是 existing-file foundation，不是 public facade。
- 新增源码或测试文件必须同步 CMake。

## 架构约束

- public 易用性不能迫使 large worksheet DOM 化或持有完整 cell map。
- Patch 必须显式说明 preserve/audit/fail/edit 策略及 relationship/content-type side effect。
- In-memory 只承担 small-file random editing，不替代未来 C5 large worksheet streaming rewrite。
- 公式、复杂对象和图片能力必须按窄边界描述，不能从 preservation 推导语义支持。
- 性能声明必须由 benchmark 和兼容性 QA 支撑。
