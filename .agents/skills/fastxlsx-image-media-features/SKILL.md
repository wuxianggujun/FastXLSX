---
name: fastxlsx-image-media-features
description: "规划或实现 FastXLSX 图片读取/插入、stb 图片解码、media parts、drawing XML、drawing relationships、worksheet relationships、content types、anchors 和图片保真验证。用于 Phase 5 图片功能、现有 workbook 图片 passthrough、图片尺寸读取、vcpkg planned-image/stb 接入，或判断图片能力是否会破坏 OPC/streaming 边界。"
---

# FastXLSX Image Media Features

## 必读文件

- `docs/DEPENDENCIES.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/TESTING_WORKFLOW.md`
- `include/fastxlsx/detail/opc.hpp`
- `src/opc.cpp`
- `vcpkg.json`

先用 `rg` 确认符号是否真实存在。当前没有图片读取/插入 API，没有 drawing
writer，没有 media part allocator，也没有已有 XLSX 图片编辑或保真复制闭环。

## 当前事实

- 图片功能属于 Phase 5 计划。
- `vcpkg.json` 只把 `stb` 记录在 `planned-image` feature；它尚未接入 CMake。
- 本机 vcpkg 已验证 port 名称是 `stb`，描述为 public domain header-only
  libraries，license 是 `MIT OR CC-PDDC`。
- `stb` 只负责图片解码、尺寸、通道数和像素读取。
- FastXLSX 自己负责 OpenXML / OPC 语义：media part、drawing part、drawing
  `.rels`、worksheet `.rels`、content type override/default、anchors 和 package
  preservation。

## 推荐流程

1. 先判断任务是图片解码、图片插入、图片读取，还是已有 workbook 图片保真。
2. 如果只是依赖规划，更新 `vcpkg.json`、`docs/DEPENDENCIES.md`、任务文档和本 skill；
   不要添加 `find_package` 或 public API。
3. 真正接入 `stb` 前，验证 vcpkg feature、include 路径、license、CI 安装耗时和
   CMake package/include 行为。
4. 图片插入可以基于当前内部 OPC graph / relationship id 分配基础设计，但仍需
   media/drawing part allocator 和 worksheet relationship wiring。
5. 写出图片时同步维护 media part name、drawing part、drawing rels、worksheet rels、
   content types 和 worksheet drawing reference。
6. 读取或编辑已有 workbook 图片时，先证明未知和未修改 media/drawing/chart/VBA parts
   能保留。
7. 第一切片优先保持窄范围：new workbook only、PNG/JPEG、明确一种 anchor 策略；
   不支持 crop、rotation、effects、group shape 或 existing drawing mutation，除非
   单独任务设计和验证。

## 必须拆开的边界

- `stb`：解码、尺寸、通道、像素。
- Package writer/reader：ZIP entry 读写和未修改 part 保留。
- OPC graph：part 索引、relationship id、target、content type。
- Drawing writer：anchors、drawing XML、drawing rels。
- Worksheet writer：worksheet `<drawing r:id="...">` 引用。

不要把这些边界合并成一个便利 API，也不要把图片逻辑放进 worksheet row/cell 热路径。

## 禁止事项

- 不要把 `stb` 可用写成图片功能已实现。
- 不要把 `stb` 复制进 `src` 或 `include`；优先使用 vcpkg manifest。
- 不要引入 OpenCV、FreeImage、Qt 等重型图像框架，除非后续任务有单独证据和审批。
- 不要只因为已有 RelationshipGraph/PartIndex 就实现 public 图片 API；还必须有
  media part、drawing XML、worksheet rels、content types、anchors 和 Excel 验证。
- 不要从已知 part 全量重建 package 导致已有图片、drawing、chart、VBA 或未知扩展丢失。
- 不要承诺图片编辑、裁剪、压缩、格式转换或 chart/VBA support，除非代码和测试已经覆盖。

## 验证

- 依赖阶段：`vcpkg install --dry-run --x-feature=planned-image` 可解析。
- CMake 接入阶段：VS2026/NMake preset configure/build/test 通过，且 CI 通过。
- 结构测试：检查 `xl/media/*`、`xl/drawings/drawing*.xml`、
  `xl/drawings/_rels/*.rels`、`xl/worksheets/_rels/*.rels`、content types 和 worksheet
  drawing reference。
- Excel 可视化验证：打开样例，确认无修复弹窗且图片显示位置/尺寸符合预期。
- 参考对比：用 Excel 或 Python XLSX 库创建参考 workbook，拆包对比 XML 语义。
- 保真测试：编辑无关 part 后，确认未修改 media/drawing/chart/VBA/unknown parts 仍存在且
  relationships 有效。
