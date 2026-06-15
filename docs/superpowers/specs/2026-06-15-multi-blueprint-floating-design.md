# 多关卡蓝图 Tab + 多蓝图编辑器独立浮动设计

日期：2026-06-15

## 背景

当前所有蓝图（关卡蓝图 + Actor `.bp` 蓝图）共用**单个** `BlueprintEditor` 实例（`m_blueprintEditor`），放在 `m_bpWrapper` 中，位于中央 `QStackedWidget`（`m_centralStack`）的 index 1。

- 关卡蓝图 tab 用单一标识 `kBlueprintTabData`（`"::blueprint::"`），始终显示「当前活动关卡」的蓝图，切关卡时通过 `loadLevel()` 换数据。
- Actor 蓝图 tab 用 `.bp` 文件路径，切到时 `loadBpClass()` 换数据。
- 浮动：单个 ADS dock `m_bpDockW`，`floatBlueprint()` 把 `m_bpWrapper` 从 stack 搬入 dock，`embedBlueprint()` 搬回；自定义定时器 `m_bpDropCheckTimer` 检测拖回 tab 栏。

两个限制：

1. 不能同时打开多个关卡的关卡蓝图（蓝图 tab 是单例，始终显示当前关卡）。
2. 同一时刻只能浮动一个蓝图编辑器。

## 目标

1. **每个关卡的关卡蓝图各有独立 Tab**，可同时打开、切换互不覆盖。关卡蓝图按 UE 模型：每个关卡内置、唯一、不可重命名、跟随关卡；Tab 标题显示「关卡名 蓝图」。
2. **多个蓝图编辑器可各自独立浮动、同时浮多个**（关卡蓝图与 Actor `.bp` 蓝图统一支持）。

不做（YAGNI）：关卡蓝图重命名、关卡蓝图独立成文件。

## 核心架构变化：单例 → 多实例

每个打开的蓝图各自拥有一个 `BlueprintEditor` 实例，常驻直到关闭。统一管理关卡蓝图和 Actor 蓝图。

```cpp
struct BpInstance {
    BlueprintEditor*  editor   = nullptr;  // 独立编辑器实例
    bool              isLevelBp = false;   // true=关卡蓝图，false=Actor .bp 蓝图
    QString           dataPath;            // 关卡蓝图=关卡绝对路径；Actor 蓝图=.bp 路径
    ads::CDockWidget* dock     = nullptr;  // 非空=已浮动；nullptr=嵌入中央 stack
};
QMap<QString, BpInstance> m_bpInstances;   // key = tabId
```

### Tab 标识（tabId）

- 关卡蓝图：`kBlueprintTabData + 关卡绝对路径`（如 `"::blueprint::/.../单人剧情.level"`）。
  - 新增 helper：`isLevelBlueprintTab(data)` = `data.startsWith(kBlueprintTabData)`；
    `levelPathOfBlueprintTab(data)` = `data.mid(kBlueprintTabData.size())`。
  - 现有所有 `== kBlueprintTabData` 的判断改为 `isLevelBlueprintTab()` 前缀判断。
- Actor 蓝图：沿用 `.bp` 文件路径（`data.endsWith(".bp")`）。

### 嵌入态

每个实例的 `editor` 加入中央 `QStackedWidget`。切到某蓝图 tab → `setCurrentWidget(instance.editor)`。实例常驻，不再每次 `loadLevel`/`loadBpClass` 重载（首次创建实例时加载一次即可）。

> 现有 `m_bpWrapper`（单个包装容器）废弃；改为各实例 editor 直接作为 stack 页。

## 行为细节

### 打开关卡蓝图（`openBlueprintTab`）

针对**当前活动关卡** `m_activeLevelPath`：

1. 计算 `tabId = kBlueprintTabData + m_activeLevelPath`。
2. 若实例已存在：
   - 嵌入态 → 切到其 tab。
   - 浮动态 → `raise()` 其浮动窗口。
3. 若不存在：创建 `BlueprintEditor` 实例，`loadLevel(当前关卡 doc)`，加入中央 stack，新建 tab（标题「关卡名 蓝图」，tooltip 同），登记到 `m_bpInstances`，切过去。

无活动关卡时（`m_activeLevelPath` 为空）不创建，保持现状。

### 打开 Actor 蓝图（`openBpClassTab`）

逻辑同上，`tabId = .bp 路径`，`isLevelBp=false`，创建实例后 `loadBpClass(bc)`。

### 切换 Tab（`onTabChanged`）

蓝图分支（关卡蓝图或 `.bp`）：

1. 取 `m_bpInstances[tabId]`。
2. `m_centralStack->setCurrentWidget(instance.editor)`。
3. 关卡蓝图：把 `m_activeLevelPath` 设为该蓝图所属关卡，并刷新大纲加载该关卡（蓝图操作需看到对应 Actor）。
4. `m_activeUndoStack = instance.editor->bpUndoStack()`。

### 浮动（拖出，可多开）

触发：拖蓝图 tab（关卡蓝图或 `.bp`）出 tab 栏。

- `DocTabBar` 现有 `blueprintDraggedOut` 仅对 `kBlueprintTabData` 触发；扩展为对**任意蓝图 tab**（关卡蓝图前缀或 `.bp` 后缀）触发，信号携带被拖 tab 的 tabId。
- `EditorWindow` 响应：
  1. 取实例，新建独立 `ads::CDockWidget`（标题=tab 文字），把 `instance.editor` 从中央 stack `removeWidget` 后 `setWidget` 进 dock，`addDockWidgetFloating`，记录 `instance.dock`。
  2. 从 tab 栏移除该 tab。
  3. 为该 dock 连接 `viewToggled(false)` / `topLevelChanged(!isTopLevel)` → 触发该实例的 embed（拖回）。
  4. 启动/复用拖回检测定时器。
- 可同时拖出多个 → 多个独立浮动 dock；ADS 原生支持它们互相停靠成 tab 组。

### 拖回（embed）

- 拖回检测定时器泛化为**遍历所有浮动蓝图 dock**（`m_bpInstances` 中 `dock != nullptr` 者）：某 dock 的浮动容器接近 tab 栏且鼠标松开 → 嵌回。
- 嵌回：editor 从 dock 取回 → 加入中央 stack → 重建该蓝图 tab → 关闭并销毁 dock → `instance.dock = nullptr`。
- `viewToggled(false)`（直接关闭浮动窗口）也走嵌回，保证 editor 不丢失。

### 关闭 Tab（`onTabClosed`）

- 关卡蓝图 / Actor 蓝图 tab：关闭蓝图 tab = 销毁该蓝图实例（从中央 stack 移除 editor 并 `delete`，从 `m_bpInstances` 移除）。Actor `.bp` 的 BPClass 数据对象（`m_openBpClasses`）仍按现状在 `closeEvent` 统一释放（ActorBPRuntime 可能引用），此处只销毁 editor 实例。
- 关闭**关卡 tab**：遍历 `m_bpInstances`，销毁所有 `isLevelBp && dataPath == 关卡路径` 的实例（含其浮动 dock），再删除关卡 doc，避免悬空。

### 快捷键（框选全部 / 删除 / 复制节点）

现状由 `EditorWindow` 全局快捷键基于 `m_centralStack` 当前页 `== index 1` 调用 `m_blueprintEditor->xxx()`。

改为：让获得焦点的 `BlueprintEditor` 自身在 `keyPressEvent` 中处理这些操作。这样嵌入态当前 tab、以及浮动窗口聚焦时都能正确响应，不再依赖中央 stack 当前页判断。`EditorWindow` 全局快捷键中蓝图相关分支移除或改为转发给 `qApp->focusWidget()` 链上的 BlueprintEditor。

### 脏标记 / 保存

每个实例的 `documentModified` 信号连接到一个 lambda，标记其所属关卡 doc 脏并刷新对应 tab 标题与保存标签。`bpClassModified` 标记对应 `.bp` 脏。需按实例区分（捕获 tabId）。

### Undo

每实例独立 `bpUndoStack()`；`m_activeUndoStack` 跟随当前聚焦/显示的实例。

## 受影响代码清单（EditorWindow.cpp/.h）

- 删除单例成员：`m_blueprintEditor`、`m_bpWrapper`、`m_bpDockW`（单 dock）→ 改为 `m_bpInstances` + 拖回定时器（泛化）。
- 重写：`onTabChanged`（蓝图/`.bp` 分支）、`openBlueprintTab`、`openBpClassTab`、`floatBlueprint`、`embedBlueprint`、`onTabClosed`、拖回检测定时器、`closeEvent`、`updateTabTooltip`、快捷键分支（114/128/149 行附近）。
- `DocTabBar`：`blueprintDraggedOut` 扩展到任意蓝图 tab，信号携带 tabId。
- 构造期蓝图 dock 初始化（414-428 行）移除单 dock，改为按需创建。

## 验证

1. 打开关卡 A、B，分别打开各自关卡蓝图 → 两个蓝图 tab 并存，切换互不覆盖，各显示自己的节点。
2. 把关卡 A 蓝图拖出浮动，再把关卡 B 蓝图也拖出浮动 → 两个独立浮动窗口同时存在，内容互不干扰。
3. Actor `.bp` 蓝图也能拖出浮动，与关卡蓝图浮动窗口共存。
4. 浮动窗口拖回 tab 栏 → 恢复为嵌入 tab，内容保留。
5. 关闭关卡 A 的关卡 tab → 其关卡蓝图 tab/浮动窗口一并关闭，无崩溃。
6. 在某蓝图（嵌入或浮动）中按快捷键（删除/复制/框选全部）正确作用于该蓝图。
7. 蓝图修改 → 对应关卡标记未保存；保存后清除。

## 实施

规模较大，确认规格后用 writing-plans 出分步实现计划，按步落地并分阶段编译验证。
