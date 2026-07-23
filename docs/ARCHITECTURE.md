# FastXLSX 架构

## 核心判断

FastXLSX 共享 OpenXML/OPC package 底座，但不会用一个内部模型覆盖所有场景：

```text
Streaming  -> 新建大文件按行增量输出，或对已有 worksheet 做有界顺序读取
Patch      -> 编辑已有文件，part-level copy/rewrite/remove
In-memory  -> 小文件稀疏随机编辑，再 handoff 给 Patch
```

当前 public、internal、planned 和 non-goal 状态见 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)。

## 模块边界

### Public Facade

- `Workbook`：小型新 workbook convenience API。
- `WorkbookWriter`：large ordered export 的 Streaming facade。
- `WorkbookReader`：existing-workbook forward-only bounded read facade。
- `WorkbookEditor`：existing-workbook Patch facade。
- `WorksheetEditor`：small-file In-memory borrowed handle。

### OpenXML 语义层

- workbook、worksheet、styles、sharedStrings、formula、metadata、image 和 document properties 语义。
- 负责 Excel/OpenXML 规则、索引和跨 part side effect。
- 不能外包给 ZIP/XML 库，也不能把 internal helper 直接暴露为 public API。

### OPC Package 层

- package entry 读取与写入。
- `[Content_Types].xml`、relationships、part index 和 dependency audit。
- unchanged part copy-original，changed part rewrite/remove；production minizip-ng 可对 method 匹配的 unchanged entry 从 source file range 复制 exact compressed payload。
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
  -> bounded worksheet body batching
  -> worksheet temporary/file-backed entry
  -> workbook/package metadata
  -> ZIP package close
```

Worksheet body batching 以 256 KiB 为当前上限，减少逐 row 文件写调用而不累积完整 worksheet。同步 `close()` 成功后立即关闭并删除 worksheet/image 临时文件，清空 row/body buffer、sharedStrings 与 styles；写包失败保留构建状态以便 retry。

大型 worksheet 不进入完整 DOM，也不构建 dense cell matrix。

### Streaming Read

```text
source XLSX
  -> PackageReader / workbook catalog
  -> fresh target entry chunk source
  -> worksheet: bounded WorksheetEventReader -> active row/cell projection
  -> worksheet: bounded WorksheetEventReader -> root metadata projection/audit
  -> sharedStrings: bounded XML scanner -> active simple item projection
  -> sharedStrings: bounded XML scanner -> active simple/rich run projection
  -> styles: bounded XML scanner -> custom numFmt / cellXfs projection
  -> styles: bounded XML scanner -> active narrow font/fill projection
  -> synchronous public callbacks
```

`WorkbookReader` 只在 `open()` 保留小型 package/workbook catalog；每次 traversal 独占一个 stored/DEFLATE entry source，完成或异常退出后立即释放。Worksheet projector 只保留当前 row/cell；metadata projector 只保留 bounded element/view-id stack 与受 count guardrail 限制的 merged ranges，用于结束前的 overlap audit；strict sharedStrings projector 只保留当前 item；run projector 只保留当前 item/run text、format 与 bounded element stack；cell-format projector 只保留当前 custom format/cellXfs record、bounded nesting stack 与 bounded `numFmtId` 去重集合；style-component projector 只保留当前 font/fill value 与 bounded nesting stack。各自 XML/text/nesting/count 上限由 public options 控制。`read_worksheet()` 的 sharedStrings/style 仍只暴露 workbook-local index；五个 companion 都不自动做 index/object handoff。六条读取路径都不加载完整 sharedStrings/styles、不构建 DOM/dense matrix/CellStore，也不进入 Patch plan 或 In-memory session。

### Patch

```text
source XLSX
  -> PackageReader / OPC index
  -> public edit request
  -> dependency audit + internal EditPlan
  -> compressed strict cell replace: one inflate -> owned temp -> target scan -> file ranges
  -> raw compressed-payload copy / logical copy / stream-rewrite / local-DOM-rewrite / remove
  -> save_as(new path)
```

`EditPlan` 是 internal traceability，不是 public package mutation surface。Output plan 只在 production minizip-ng 且 source/output compression method 匹配时为 unchanged entry 选择 raw source descriptor；writer 从 source `data_offset + compressed_size` file range 直接写入 minizip raw entry，并使用 source method/CRC/uncompressed size 完成新 package record。它保留 compressed payload，不复制 local header、central directory、extra fields 或 package layout；rewritten/method-changing/stored 路径仍按 logical payload 编码。

Targeted strict replace 的临时文件只保存解压后的 worksheet bytes，再以 direct file ranges replay；fallback 在单次 source-order scan 中完成 cell transform、dimension 与 relationship audit，并以 file ranges + bounded memory dimension chunk staging。Fallback 的 internal opt-in reader 会把 bounded window 内结构完整的 numeric、simple inline-string 与 formula cell 合并为 callback-lifetime exact-byte span；rich metadata、unsupported nested markup 与跨窗口 cell 保留详细事件流，formula audit 通过显式 metadata 保持可见。事件输出由 256 KiB owned buffer 合并后再做增量 relationship scan 和 temporary-file write；transformer 已解析的 cell coordinate 随 action 传递，有序 upsert 使用单游标推进且仅乱序 source 使用 set fallback。两条路径都不构建 worksheet DOM 或 dense cell map。文件所有权随 staged transaction 发布，失败前由 RAII 清理；后续 rewrite 提交时立即删除不再被 replacement 引用的旧临时文件。Minizip writer 对同路径 ranges 复用输入句柄，file-chunk scratch buffer 也跨 entry 懒分配复用；Windows production 对至少 4 MiB 的 staged file chunk 以两个固定 512 KiB buffer 做 overlapped read-ahead，并在当前 buffer 同步写入 target entry 时读取下一块。该状态只存在于单次 entry write，退出前取消并等待未完成 IO；小 chunk、raw-copy、stored-only 与非 Windows 路径保持同步读取。Internal writer option 只允许 64 KiB–4 MiB 的二次幂输入 buffer，production 默认由隔离升降序 profile 选为 512 KiB；这不是 public API。

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
- `src/worksheet_reader.cpp` 负责 public bounded read projector；internal raw XML events 和 OPC types 不进入 public header。
- Feature 已拥有独立状态、XML、验证和大量测试时，应拆分内部实现与 feature-specific tests。
- `src/package_editor.cpp`、`src/package_reader.cpp` 和 OPC helpers 是 existing-file foundation，不是 public facade。
- 新增源码或测试文件必须同步 CMake。

## 架构约束

- public 易用性不能迫使 large worksheet DOM 化或持有完整 cell map。
- Patch 必须显式说明 preserve/audit/fail/edit 策略及 relationship/content-type side effect。
- In-memory 只承担 small-file random editing，不替代未来 C5 large worksheet streaming rewrite。
- 公式、复杂对象和图片能力必须按窄边界描述，不能从 preservation 推导语义支持。
- 性能声明必须由 benchmark 和兼容性 QA 支撑。
