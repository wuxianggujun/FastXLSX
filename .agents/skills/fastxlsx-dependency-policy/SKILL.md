---
name: fastxlsx-dependency-policy
description: "应用 FastXLSX vcpkg、linkage、license 与 export 边界。"
---
# FastXLSX Dependency Policy

- 默认 features：`runtime-minizip`、`images`。
- `runtime-minizip` 是当前 production ZIP backend。
- `images` 可关闭；关闭时不安装/查找 stb。
- `planned-xml` 当前未链接；`planned-dev` 当前未自动接线。
- Reference libraries 只用于 benchmark。

使用 manifest mode + `find_package`/`find_path`；记录 feature、linkage、license、export/consumer 影响。Manifest presence 不等于 linked capability。