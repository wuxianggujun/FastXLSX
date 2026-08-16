# API 设计与文档规则

## 事实与入口

- 当前能力唯一事实源：[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)。
- 当前执行入口：[TASK_BREAKDOWN.md](TASK_BREAKDOWN.md)。
- 只有 `include/fastxlsx/` 中明确暴露的符号才能描述为 public API。
- `include/fastxlsx/detail/`、`src/` 和测试 hook 属于 internal foundation。

## API 模式门禁

设计 API 前必须声明所属模式：

- **Streaming write**：新建 XLSX、大数据导出、row-order append。
- **Streaming read**：已有 XLSX、forward-only row/cell callback、bounded active state。
- **Patch**：existing-file part-level rewrite、preservation 和 audit。
- **In-memory**：small-file sparse random editing。

跨模式 API 必须写清入口、数据流、状态所有权、保存行为和性能后果。

## Public / Internal 边界

- public facade 保持面向 workbook/worksheet/cell 的用户语义。
- `PackageReader`、`PackageEditor`、`EditPlan`、dependency analysis 和 relationship graph 不公开。
- 测试覆盖、internal hook 或 preservation fixture 不能自动成为用户承诺。
- planned 能力必须标记为 planned，不写成路线图保证或当前支持。

## 设计原则

- 易用 API 不能让 large worksheet 隐式进入 DOM、dense matrix 或无界 cell map。
- `Cell` / `CellValue` 可以作为 owning 边界值和 small-file 存储，不作为 million-row 热路径长期模型。
- Public bounded reader 必须把 formula text 与 cached scalar 分离，逐字段声明 callback lifetime；sharedStrings/style 必须写清返回 index 还是 resolved value、是否校验 table count。XML window、active-cell text、entry source 的上限/释放/异常重试必须是契约，不能暴露 internal event/OPC 类型，也不能用 worksheet DOM、dense matrix 或 `CellStore` 冒充 Streaming read。
- Bounded simple sharedStrings companion 必须与 worksheet index projection 显式分离，写清 zero-based source order、borrowed text lifetime、simple/empty/entity decode、root count validation 边界、XML window/active-item text 上限、unique internal relationship + normalized target + content-type audit，以及 stored/DEFLATE ownership与 callback retry；rich/phonetic/extension/extra metadata 默认 fail，禁止完整 table、自动 worksheet index resolution 或 Patch/In-memory handoff。
- Bounded rich sharedStrings run companion 必须独立于 strict simple projection，写清 item start/run/item end 顺序、失败前可能已发出的 partial callbacks、run text borrowed lifetime、simple-item 单 run 映射、zero-based item/run index 和 owning format。XML window、active item/run text、runs-per-item 与 nesting 必须有 guardrail；只无损投影 bold/italic/direct ARGB 与固定默认 font metadata，phonetic/extension、simple/rich 混合、theme/tint inheritance 和其他 run property 明确 fail。禁止完整 table、worksheet index 自动关联、format inheritance 或 Patch/In-memory handoff。
- Bounded cell-formats companion 必须与 worksheet style index 显式分离，写清 custom format-code borrowed lifetime、cellXfs zero-based index、opaque number-format/font/fill references、apply/alignment value ownership、container count 边界、XML window/format-code/nesting/custom-id guardrail、unique internal styles relationship + normalized target + content-type audit，以及 stored/DEFLATE ownership与 callback retry。Border/base-style/protection/extension 和未投影 alignment 必须明确 accept-no-op 或 fail，禁止完整 styles registry、自动 worksheet index resolution 或 Patch/In-memory handoff。
- Bounded style-components companion 必须保持独立 traversal，写清 zero-based font/fill index、owning scalar values、direct ARGB representation、font/fill container count 与 XML window/nesting/component-count guardrail，以及与 cellXfs/worksheet style index 的 caller-owned join。只允许无损投影当前 narrow writer-compatible bold/italic/default-font/direct-color 与 none/gray125/solid-fill 语义；theme/tint inheritance、其他 font properties、gradient/pattern 扩展必须 fail，禁止完整 registry、自动 resolution 或 existing-style rewrite。
- Bounded worksheet metadata companion 必须与 row/cell traversal 和 Patch mutation 分离，写清 primary `workbookViewId="0"` frozen pane、worksheet-root auto-filter、zero-based merged ranges 的 source order 与 owning lifetime，以及 parser failure 前可能已有 partial callbacks。XML window、nesting、reference bytes、sheetView count 与 retained merge count 必须有 guardrail；duplicate/nesting/QName/schema/count/range overlap 与 unsupported split/pivot state明确 fail。必须声明其他 view 只审计、table-local filter 不读取、relationships/content types/manifest 无副作用，并禁止 DOM/dense matrix/CellStore、Patch/In-memory 隐式 handoff或完整 worksheet-view/filter 对象模型。
- Bounded worksheet data-validation companion 必须独立于通用 metadata read 与 Patch mutation，写清 zero-based source order、owning `vector<CellRange> + DataValidationRule`、失败前 partial callbacks，以及 shared rule 只代表 writer-compatible semantic projection。XML window、nesting、validation/range count、`sqref`、formula 和 prompt/error text 必须有 guardrail；container count/direct child/QName/schema、boolean/enum/entity decode 与 formula1/formula2 shape 必须严格审计。`imeMode`、target 内 foreign/extension metadata 和 non-default prompt-window container metadata 明确 fail；target 外 foreign extension local-name 不得误识别为 validation。禁止公式求值、cell-value validation、overlap repair、relationship/content-type/manifest side effect、DOM/CellStore 或 Patch/In-memory handoff。
- Bounded worksheet hyperlink companion 必须独立于通用 metadata read 与 Patch mutation，写清 zero-based source order、owning cell/range/kind/location/external-target/display/tooltip、失败前 partial callbacks，以及 relationship id 保持 package-local internal。Internal `location` 不要求 `.rels`；external target 必须通过当前 worksheet owner-local relationship 解析，并审计标准 type、`TargetMode="External"` 与非空 target。XML window、nesting、hyperlink/ref/id/target/metadata-text count 必须有 guardrail；唯一 container/direct child、QName/namespace scope、schema、恰好一个 location/id、unsupported metadata 与 duplicate/range overlap 必须明确 fail。禁止 target reachability validation、relationship create/repair/prune/rewrite、content-type/manifest/cell/style side effect、DOM/CellStore 或 Patch/In-memory handoff。
- Bounded worksheet conditional-formatting companion 必须是独立 public traversal，不与通用 metadata read 或 Patch mutation 隐式 handoff；写清 zero-based source order、owning multi-range `sqref`、priority、writer-compatible rule payload、失败前 partial callbacks，以及共享 writer rule 类型只代表窄 projection。XML window、nesting、format/range count 与 decoded `sqref` 必须有 guardrail；唯一 container/direct child、QName/namespace scope、schema order、单个 direct `cfRule`、priority、rule kind/operator、`cfvo`/color/icon threshold shape 与 ARGB 必须严格审计。Target 外 foreign extension local-name 不得误识别为 conditional formatting；advanced/custom icon sets、advanced data bars、`dxf`/styles object model、formula/cellIs rule family、multiple-rule container 和 unsupported metadata 必须明确 fail。禁止公式求值、style/dxf repair、relationship/content-type/manifest side effect、DOM/CellStore 或 Patch/In-memory implicit handoff；Patch add 使用独立 structural/priority audit，不复用或放宽 reader projection。
- Bounded worksheet table companion 必须独立于通用 metadata read，写清 `<tableParts>` zero-based source order、owning table id/range/name/displayName/basic columns、writer-compatible totals function/label、table-local auto-filter 与 style flags、失败前 partial callbacks，以及 relationship id/part path 保持 internal。Worksheet tableParts count/direct child/QName/relationship namespace/schema order 必须审计；每个引用必须解析为 owner-local、标准 type、internal、percent-decoded/normalized、unique target，并通过 part presence 与标准 content type 检查。XML window、nesting、table count、relationship id/target、table/column name、每表 column count 与 range bytes 必须有 guardrail。Table root/header/totals/autoFilter/style/schema/QName/count 严格审计；calculated/other totals formula、完整 filter/sort criteria、extensions 与其他未投影 semantics 必须 fail。禁止 cell-payload inference、reader 自身的 workbook-wide uniqueness claim、relationship/content-type/manifest mutation、table DOM/CellStore 或 In-memory handoff；Patch 只能显式复用成功的 bounded writer-compatible projection 做 source audit。
- Bounded worksheet image companion 必须独立于 row/cell/metadata/table read 与 Patch mutation，写清 drawing zero-based source order、owning `editAs`/from/to marker/EMU offset/transform extent/name/description/format/encoded size、失败前 partial callbacks，以及 worksheet/drawing relationship id 和 part path 保持 internal。Worksheet 只允许唯一 direct standard drawing reference；每个 direct `xdr:twoCellAnchor` 必须解析为 writer-compatible picture，并经 drawing-local standard internal relationship、percent-decoded normalized target、part presence 与 PNG/JPEG content type/signature 审计。Worksheet/drawing XML window、nesting、image count、relationship id/target、name/description、numeric text 与 unique media bytes 必须有 guardrail；unique media 每次调用只完整流过一次以验证 ZIP size/CRC，不得保留 payload 或解码 pixels。`xdr:oneCellAnchor` / `xdr:absoluteAnchor` 元素、chart/shape/group/connector、crop/rotation/position transform、picture hyperlink 与其他未投影 semantics 必须 fail。禁止 relationship/content-type/manifest mutation、完整 drawing DOM/object model、CellStore 或 Patch/In-memory handoff；no-images build 保留 symbol 但调用抛错。
- Bounded worksheet comments companion 必须独立于 row/cell/metadata/image read 与 Patch mutation，写清 comments-part zero-based source order、owning one-based row/column、resolved author/simple text、失败前 partial callbacks 和 callback retry。Worksheet 只接受唯一 standard internal comments relationship；target 必须 percent-decoded/normalized，并通过 part presence 与标准 content type 审计。Author table、comment/author count、单 author/aggregate author/comment text、cell ref、authorId、shapeId、relationship id/target 与 XML window/nesting 必须有 guardrail；single-cell uppercase A1、authorId bounds、duplicate ref 与 optional numeric shapeId 必须审计。Optional direct `legacyDrawing` 只审计 owner-local standard internal VML relationship、target、part/content type 与 ZIP-entry presence；VML payload/visibility/shape 不投影。Rich/phonetic/extension、worksheet-local threaded-comment relationship 与 unsupported metadata 必须 fail；persons part 不跟随、不投影。禁止 comments/VML/relationship/content-type/manifest mutation、完整 comments DOM、row/cell attachment、CellStore 或 Patch/In-memory handoff。
- Patch edit 必须明确 copy/rewrite/remove/audit/fail 行为，以及 sharedStrings、styles、formulas、relationships、content types 和 calc metadata 策略。
- Existing-workbook hyperlink API 已区分 worksheet-local internal target 与 external target：internal hyperlink 只改 worksheet XML，不伪造 `.rels`；external hyperlink 同时 staging worksheet XML 与 worksheet `.rels` relationship id/`TargetMode="External"`，保留既有关系并在失败时恢复调用前状态。两者都声明 duplicate/range、XML escaping、cell/style、formula/definedName 与 linked-object non-goals。
- Existing-workbook data-validation API 复用 Streaming 的 owning `DataValidationRule`，但必须单独声明 multi-range `sqref`、formula1/formula2 shape、prompt/error escaping、已有 container/count/schema-order guardrail，以及不创建 `.rels`/content type、不求值/请求重算、不随 structural mutation 同步的边界。共享 rule 类型不代表共享 worksheet DOM 或完整 validation 对象模型。
- Existing-workbook conditional-format add API 必须写清 `add_conditional_color_scale()` / `add_conditional_data_bar()` / `add_conditional_icon_set()` 的 overload 与 writer-compatible new-rule 边界、独立 source structural/priority audit、effective priority `max + 1`、planned rename/added worksheet、`conditional_format_count` 诊断和 failure-before-state-change/retry。Doxygen 必须声明只追加 worksheet-local `<conditionalFormatting>`，不改 cells、relationships、content types、styles/dxf、calc metadata 或 linked objects；advanced/custom/dxf/formula/cellIs/multiple-rule source payload 原样保留且不投影/接管，结构或 priority 异常 fail，并明确窄 add 不等于完整 existing-workbook conditional-formatting 对象模型。
- Existing-workbook table lifecycle API 复用 Streaming 的 owning `TableOptions`、validator 与 serializer，但必须把 worksheet `<tableParts>`、owner `.rels`、`xl/tables/tableN.xml`、content types、manifest/EditPlan、public diagnostics 与 pending/watermark 作为事务。Doxygen 必须声明 source/planned-rename/same-session-added worksheet 与 lifecycle overlay；workbook-wide ASCII-insensitive name、id/part uniqueness和 same-sheet range non-overlap；source table 的 bounded writer-compatible projection、无 child relationship 与 normalized exclusive inbound ownership。`update_table()` 保留 id/relationship/part/order，identical replacement clean no-op；`remove_table()` 关闭完整 package graph，最后一项删除 container，name/range 可复用但 id/part 本 session 不复用。还必须明确不读取或修改 header/data/totals cell payload、不求值、不生成 cached result、不随 structural mutation 同步，并区分窄 metadata lifecycle 与完整 table object model；失败在发布前不得污染 package/public state 且可 retry。
- Existing-workbook auto-filter API 必须区分 worksheet-root `<autoFilter>` 与 table-local filter part。`set_auto_filter()` 是 whole-element replace，旧 filter criteria/sort metadata 会丢弃；`clear_auto_filter()` 在 absent 时 clean no-op。Doxygen 必须声明单一 `CellRange`、existing ref/duplicate/schema guardrail、optional-range diagnostic、planned rename/added worksheet、无 relationship/content-type/calc side effect，以及不随 structural mutation 同步；不能描述成完整 filter criteria 对象模型。
- Existing-workbook merged-cell API 必须声明 multi-cell `CellRange`、merge duplicate/overlap rejection、unmerge exact-only、partial-overlap rejection 与 absent-disjoint clean no-op。Doxygen 还必须写清已有/self-closing container、`count`/direct-child/ref/schema guardrail、addition/removal diagnostics、planned rename/added worksheet，以及 metadata-only 路径保留非左上角 cell record/value/style/formula、relationships、content types、tables、`calcPr` 和 `calcChain` 且不随 structural mutation 同步；不能描述成完整 merged-cell 对象模型或 Excel 式 cell payload 清理。
- Existing-workbook freeze-pane API 必须声明 primary `workbookViewId="0"` direct pane ownership、row/column split 是冻结数量、`(0,0)` clear、单轴 `activePane`、合法 `topLeftCell` 上界，以及 missing/self-closing view metadata expansion。Doxygen 还必须写清 ordinary split/frozenSplit/pivotSelection/失效 pane selection fail、其他 workbook view 与合法 selection preservation、final split diagnostics、planned rename/added worksheet、无 cell/relationship/content-type/table/calc side effect；不能描述成完整 worksheet view 对象模型。
- Existing-workbook worksheet lifecycle API 必须明确 source catalog 与 planned catalog 的区别，并把 workbook XML、workbook relationships、content types、worksheet part、manifest、public diagnostics 和 pending/watermark 作为同一事务边界。`add_worksheet()` 只承诺 generated empty worksheet；`remove_worksheet()` 只承诺 relationship-closed deletion，并在 active/definedName/formula/materialized/linked semantics 不可同步时 fail。不能把 add 描述为 clone，也不能把删除描述为语义 repair。
- In-memory API 必须提供 cell count、内存估算和 guardrail，并定义失败前状态不污染。
- Existing-workbook style-only API 在 style registry/migration contract 建立前，只能复用同一 materialized workbook 中已校验的 source StyleId 或清除现有句柄；range mutation 必须定义 sparse mapping、missing-target、overlap snapshot 与 batch preflight 语义。不得接受任意 caller non-default StyleId，并必须声明 styles.xml 是 preserve 而不是 edit。
- Style-only move 必须明确 source-clear 与 destination-overlay 的顺序：从 pre-edit snapshot 冻结 optional StyleId，在候选 CellStore 中先清除 represented sources、再覆盖全部已表示的 mapped targets，比较最终状态并通过 guardrail 后一次发布；不能先修改 active source，也不能借 move 合成 cell、移动 CellValue 或引入 style table migration。
- Cross-worksheet In-memory API 必须验证 borrowed handle 属于同一当前 `WorkbookEditor`，明确 source/destination dirty ownership、live snapshot 时点、同坐标行为和目标 guardrail；不得把 same-workbook sparse copy 描述为 worksheet clone、cross-workbook migration 或 linked-object copy。
- Value-only copy 必须明确 source StyleId 被忽略、existing destination StyleId 从 pre-edit snapshot 保留、missing destination 插入 unstyled，以及公式仍按 source-to-target delta 平移；不得用 full-cell copy 或 style migration 语义替代。
- Value-only move 必须同时定义 source 与 destination 的样式所有权：source 采用 `clear_cell_value()` 的显式 blank 并保留 source StyleId，destination 保留 pre-edit StyleId，missing destination 插入 unstyled；跨 worksheet 发布必须使用双 CellStore candidate + noexcept commit。
- Row/column structural edit 必须将 formula-cell 坐标移动与 formula-reference 重写分开：所有 surviving formulas 都按插入/删除轴做 structural rewrite，不能因公式记录自身被移动就退化为 copy/move delta translation；`$` 只保留标记，不阻止结构调整。
- Cross-worksheet move 是双状态 mutation：实现必须在 active sessions 外构造 source-removal 与 destination-overlay candidates，验证两边 guardrail 后只以 noexcept commit 发布，并覆盖 destination preflight failure、save failure retry 与 reopen；禁止先删 source 再尝试写 destination。
- Cross-worksheet style-only mapping 还必须要求 mapped target 已表示，先完成 source optional StyleId snapshot 与全目标 preflight，再发布 destination-only batch；source gaps 不得合成 target，unstyled source 的 clear 语义和 styles.xml preserve 边界必须显式记录。
- Cross-worksheet style-only move 属于双 session mutation：必须在 active state 外完成 source-clear 与 destination-overlay candidates 及两边 guardrail，随后只以 noexcept swap 发布，并按每个 session 的最终差异独立标记 dirty；same-coordinate cross-sheet 不能误判为 no-op，失败不得泄漏半边样式更新。
- Public structured diagnostics 只暴露稳定业务/语义分类与调用方可理解的上下文；XML token、parser state、part path、relationship id 和 internal type 不得成为 public contract。Typed exception 应保留 `FastXlsxError` 基类兼容性，并明确哪些相邻失败仍是通用错误。
- 数值写入必须拒绝非 finite 值，不能序列化 `nan`、`inf` 或 `-inf`。
- 第三方库只承担 ZIP、XML、图片等通用能力；XLSX 语义留在 FastXLSX。

## Doxygen 要求

Public API 注释至少说明：

- API 所属模式和适用数据规模。
- 参数生命周期、所有权及 `std::span`/view 的有效期。
- 顺序要求和随机访问限制。
- 内存随 row、cell、string、style、rule、range、image bytes 或 decoded pixels 的增长关系。
- OpenXML side effect：worksheet `.rels`、drawing `.rels`、content types、styles、sharedStrings、docProps、calc metadata 等。
- 错误类型、失败是否发生在状态变更前、失败后是否可重试。
- 若错误提供 typed diagnostic，逐字段说明索引基数、可选条件、稳定性和 text message 的非契约属性。
- 不支持项和容易被误解的边界。

发现 Doxygen 缺口时在 `TASK_BREAKDOWN.md` 的 C0/C3 记录任务；本文件不维护逐方法覆盖流水。

## Public API 提案模板

每个提案必须回答：

1. 属于 Streaming write、Streaming read、Patch 还是 In-memory？
2. public facade 和 internal implementation 分别是什么？
3. 输入、输出、所有权和错误契约是什么？
4. 内存随什么增长，是否触碰热路径？
5. 涉及哪些 OpenXML part、relationship 和 content type？
6. Existing-file 场景采用 preserve、audit、fail 还是 edit？
7. 需要哪些 unit、OpenXML、Office、preservation 或 benchmark 证据？
8. 哪些能力明确不在本次范围？

## Wording Gate

- “高性能”“低内存”必须附 benchmark 数据集与内存口径。
- “支持对象”必须区分创建、读取、保留、审计、替换和语义编辑。
- “支持图片”必须区分 existing-workbook bounded read projection、new-workbook insertion、existing media bytes replacement 与完整 drawing 编辑。
- “支持公式”必须区分文本、审计、重写、重算请求和求值。
- “保存”必须区分 `save()`、`close()` 和 non-atomic `save_as()`；`WorkbookEditor::save_as()` 不是 commit/close，成功后仍可能保留 staged Patch state。ZIP compression 选项必须说明 backend 可用性、stored/DEFLATE、输出大小/CPU 后果，并区分 logical preservation、exact compressed payload copy 与完整 ZIP record/package byte preservation；三者不能互换表述。

## 验证清单

- public 名称可在 public headers 中找到。
- internal 名称只出现在明确的 internal/架构语境。
- 没有把 planned 或 preservation 写成当前 public support。
- README、事实源、架构和任务入口没有复制互相漂移的长矩阵。
- 文档链接、UTF-8 和 `git diff --check` 通过。
