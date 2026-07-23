---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、Doxygen、状态与性能边界。"
---
# FastXLSX API Design Docs

先标记 Streaming write/Streaming read/Patch/In-memory 与 public facade，再定义输入、所有权、状态、失败、保存、内存和 OpenXML side effects。

Public Doxygen 必须写清：
- 顺序/随机访问与生命周期。
- `has_pending_changes()` 和 `has_unsaved_changes()` 的区别。
- Save Doxygen 必须区分 internal stage、package write 与 public state commit，并说明失败时 dirty/pending/unsaved/last-error 不变量。
- In-memory strict/lossy projection，默认 strict，lossy 显式 opt-in。
- Structured diagnostics 只暴露稳定 category/context，不泄漏 XML、parser 或 package internal 状态；typed exception 保持 `FastXlsxError` catch compatibility。
- preserve/audit/fail/edit 和 non-goals。
- Hyperlink 必须区分 internal worksheet-local XML 与 external `.rels` relationship mutation；写清 duplicate/range、XML escaping、cell/style、formula/definedName 和 linked-object side effects，不能把 internal hyperlink 扩大为完整 hyperlink 编辑。
- Data validation 必须写清 shared owning rule、single-/multi-range `sqref`、formula1/formula2 shape、prompt/error escaping、container/count/schema-order guardrail，以及不创建关系、不求值/请求重算、不随 structural mutation 同步的边界；不能扩大为完整 validation 对象模型。
- Auto-filter 必须区分 worksheet-root element 与 table-local filter part；写清 whole-element set/clear、旧 criteria/sort metadata 丢弃、clear absent no-op、single range、existing ref/duplicate/schema guardrail、optional-range diagnostic，以及不创建关系/content type/calc metadata、不随 structural mutation 同步的边界；不能扩大为完整 filter 对象模型。
- Merged-cell 必须写清 multi-cell `CellRange`、merge duplicate/overlap fail、unmerge exact-only、partial overlap fail、absent disjoint no-op，以及 container/count/direct-child/ref/schema guardrail；同时声明 metadata-only、保留非左上角 cell record/value/style/formula、relationships、content types、tables、`calcPr`/`calcChain`，且不随 structural mutation 同步，不能扩大为完整 merged-cell 对象模型。
- Freeze-pane 必须写清 primary `workbookViewId="0"` direct pane ownership、row/column frozen count、`(0,0)` clear、单轴 active pane、topLeftCell 上界、missing/self-closing view expansion，以及 split/frozenSplit/pivotSelection/失效 selection fail；同时声明保留其他 view、合法 selection、cells、relationships、content types、tables 与 calc metadata，不能扩大为完整 worksheet view 对象模型。
- Bounded worksheet reader 必须写清 forward-only row/cell source order、每个 borrowed field 的 callback lifetime、formula/cached split、sharedStrings/style 是 opaque index 还是 resolved value、table-count validation 边界、XML window/active-cell text 上限、stored/DEFLATE entry ownership与 callback exception retry；声明 read-only、no seek/DOM/dense matrix/CellStore/Patch handoff，并对 rich/formula metadata 等 unsupported projection 明确 fail。
- Bounded simple sharedStrings companion 必须与 worksheet index projection 显式分离，写清 zero-based source order、borrowed text lifetime、simple/empty/entity decode、root count validation 边界、XML window/active-item text 上限、unique internal relationship + normalized target + content-type audit、stored/DEFLATE ownership与 callback retry；rich/phonetic/extension/extra metadata 默认 fail，禁止完整 table、自动 worksheet index resolution 或 Patch/In-memory handoff。
- Bounded rich sharedStrings run companion 必须独立于 strict simple projection，写清 item start/run/end order、失败前 partial callbacks、run text borrowed lifetime、simple 单 run compatibility、zero-based item/run index、owning bold/italic/direct-ARGB、XML window/item/run/runs/nesting guardrail 与 OPC audit。Fixed default font metadata 可 accept；mixed shape、phonetic/extension、其他 font/theme/tint/property 明确 fail。禁止完整 table、format inheritance、自动 worksheet index resolution 或 Patch/In-memory handoff。
- Bounded cell-formats companion 必须与 worksheet style index 显式分离，写清 custom format-code borrowed lifetime、zero-based cellXfs、opaque number-format/font/fill references、apply/alignment owning values、container count 与 duplicate-id 边界、XML window/format-code/nesting/custom-id count guardrail、unique internal styles relationship + normalized target + content-type audit、stored/DEFLATE ownership与 callback retry；border/base-style/protection/extension 和其他 xf/alignment metadata 必须明确 accept-no-op 或 fail，禁止完整 registry、自动 worksheet index resolution 或 Patch/In-memory handoff。
- Bounded style-components companion 必须独立遍历并写清 zero-based font/fill index、owning bold/italic/direct-ARGB 与 fill-pattern values、container count、XML window/nesting/component-count guardrail、styles relationship/target/content-type audit 和 callback retry。只无损投影当前 writer-compatible subset；non-default font/theme、其他 color/pattern/gradient 明确 fail，禁止完整 registry、自动 cellXfs/worksheet-index resolution、theme inheritance 或 existing-style rewrite。
- Bounded worksheet metadata companion 必须独立于 row/cell read 与 Patch mutation，写清 primary frozen pane、worksheet-root auto-filter、zero-based merged range 的 source order、owning lifetime 和 partial-callback completion signal；XML window/nesting/reference/view/retained-merge guardrail、schema/QName/count/overlap/unsupported pane fail 必须显式。其他 view 只审计、table-local filter 不读取、OPC state 无副作用，禁止 DOM/dense matrix/CellStore 或 Patch/In-memory handoff。

禁止把 internal 类型、preservation evidence、公式文本或窄图片能力扩大为 public/full support。
