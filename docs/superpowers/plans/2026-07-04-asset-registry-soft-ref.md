# Asset Registry Soft Ref Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 2D 引擎新增轻量资产注册表、软引用解析和删除前依赖保护，让资产可以更安全地任意目录创建、移动、重命名和引用。

**Architecture:** 资产注册表扫描项目目录并维护 `assetId -> path/type/name` 索引；`SoftAssetRef` 兼容旧字符串路径并由 `AssetResolver` 按 ID 优先、路径兜底解析；依赖扫描器读取数据表、蓝图、UI、关卡中的引用，在内容浏览器删除资产前给出中文引用诊断。

**Tech Stack:** C++17、Qt6 Widgets、QJsonDocument、CMake。

## Global Constraints

- 项目沟通、文档、注释、界面文案和交付说明使用中文；代码标识符可保留英文。
- 新增 `.cpp` 或 `.h` 文件必须同步更新 `launcher/CMakeLists.txt`。
- 修改 C++ 后必须执行 `cmake --build` 并启动 `launcher.app` 验证。
- 旧资产必须自动迁移读取，不要求用户手动重建。
- 当前工作区已有未提交修改，不回退无关改动。

---

### Task 1: 资产注册表模型

**Files:**
- Create: `launcher/src/models/AssetRegistry.h`
- Create: `launcher/src/models/AssetRegistry.cpp`
- Modify: `launcher/CMakeLists.txt`

**Interfaces:**
- Produces: `AssetRegistry::rebuild()`、`AssetRegistry::findById()`、`AssetRegistry::findByPath()`、`AssetRegistry::ensureAssetId()`
- Produces: `AssetRecord { QString id, path, type, name, suffix }`

- [ ] 新增 `AssetRegistry`，扫描 `Levels/`、`Blueprints/`、`Effects/`、`Components/`、`UI/`、`Animations/`、`DataTables/`、`Enums/`。
- [ ] 对 JSON 资产读取或生成 `assetId`，必要时写回文件。
- [ ] 按 `assetId` 和规范化项目相对路径建立索引。
- [ ] 更新 CMake。

### Task 2: 软引用和解析器升级

**Files:**
- Modify: `launcher/src/models/AssetRef.h`
- Modify: `launcher/src/models/AssetRef.cpp`
- Modify: `launcher/src/editor/DataTableRuntimeService.cpp`
- Modify: `launcher/src/editor/EffectRuntime.cpp`
- Modify: `launcher/src/editor/UIRuntime.cpp`

**Interfaces:**
- Produces: `SoftAssetRef::fromVariant(QJsonValue, QString expectedType)` 和 `SoftAssetRef::fromString(QString, QString expectedType)`
- Updates: `AssetResolver` 支持 `AssetRegistry*`，优先按 ID 解析，失败再按路径解析。

- [ ] 新增 `SoftAssetRef`，兼容旧字符串和 `{ id, path, type }` 对象。
- [ ] `AssetResolver` 增加注册表指针和类型校验。
- [ ] 数据表 effect、效果蓝图、UI 引用解析改为使用软引用入口。
- [ ] 解析失败输出中文诊断，不崩溃。

### Task 3: 依赖扫描和删除保护

**Files:**
- Create: `launcher/src/models/AssetDependencyScanner.h`
- Create: `launcher/src/models/AssetDependencyScanner.cpp`
- Modify: `launcher/src/editor/ContentBrowser.cpp`
- Modify: `launcher/src/editor/ContentBrowser.h`
- Modify: `launcher/CMakeLists.txt`

**Interfaces:**
- Produces: `AssetDependencyScanner::findReferences(const AssetRecord&) -> QList<AssetDependency>`
- Produces: `AssetDependency { QString sourcePath, sourceName, sourceType, detail }`

- [ ] 扫描数据表字段、蓝图节点属性、组件挂载、UI/关卡 JSON 里的路径或软引用对象。
- [ ] 内容浏览器删除资产前查询依赖。
- [ ] 有引用时用中文提示引用来源并阻止删除。
- [ ] 无引用时保留现有删除行为。

### Task 4: 验证

**Files:**
- No source files.

- [ ] 运行 `cmake --build`。
- [ ] 启动 `launcher.app`。
- [ ] 验证 JSON 资产可解析。
- [ ] 静态检查旧 `cardbp/effectbp/componentbp` 内部类型不恢复。
- [ ] 检查新源码已加入 CMake。
