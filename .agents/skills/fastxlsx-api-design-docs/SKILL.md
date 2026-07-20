---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、Doxygen、状态与性能边界。"
---
# FastXLSX API Design Docs

先标记 Streaming/Patch/In-memory 与 public facade，再定义输入、所有权、状态、失败、保存、内存和 OpenXML side effects。

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

禁止把 internal 类型、preservation evidence、公式文本或窄图片能力扩大为 public/full support。
