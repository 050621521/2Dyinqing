# DocTabBar 标签栏完整名称显示设计

日期：2026-06-15

## 背景

当前编辑器顶部的关卡/文档标签栏（`DocTabBar`，继承 `QTabBar`）存在两个问题：

1. 标签文字被 `QTabBar` 默认 `elideMode` 截断成「单人…」「登录…」「角色…」，连「游戏视图」也被截成「游戏…」。
2. 没有 tooltip，鼠标悬停也看不到完整名称；多个同名前缀的关卡（多个「单人…」「登录…」）完全无法区分。

## 目标

- 标签按文字内容自适应宽度，完整显示名称，不再截断。
- 鼠标悬停标签时显示完整名称 / 文件路径，便于区分同名关卡。
- 标签总宽超出工具栏时出现左右滚动按钮。
- 支持拖动标签重新排序。

不做（YAGNI）：标签下拉列表、标签最大宽度限制。

## 改动点

均在现有文件内完成，无新增文件。

### 1. `EditorWindow::setupDocTabBar()`（`src/editor/EditorWindow.cpp`）

给 `tabBar` 追加配置：

- `setElideMode(Qt::ElideNone)` — 标签按文字自适应宽度，不再省略。
- `setUsesScrollButtons(true)` — 总宽超出时出现左右滚动箭头。
- `setMovable(true)` — 允许拖动排序。

### 2. 悬停提示 tooltip

在所有设置标签文字的位置补上 `setTabToolTip(idx, ...)`：

- 关卡 tab：tooltip 为**完整文件路径**（`tabData` 存的绝对路径）。
- 蓝图 / 游戏视图 / UI tab：tooltip 为完整中文名（「关卡蓝图」「游戏视图」等）。

收敛策略：在 `addTab` 后、以及 `updateTabTitle`（脏标记刷新 `setTabText`）处统一补 tooltip，避免遗漏。

### 3. 拖动排序与蓝图拖出浮动的交互（`DocTabBar`）

`setMovable(true)` 后，`QTabBar` 自带的横向拖动排序由基类 `mouseMoveEvent` 处理。`DocTabBar` 现有的蓝图 tab「拖出浮动」逻辑（`blueprintDraggedOut`）必须保留：

- 普通关卡 tab：交给基类处理 → 正常排序。
- 蓝图 tab：仍由 `DocTabBar` 拦截，移动超过阈值时 `emit blueprintDraggedOut` 并 `return`（不调用基类），floatBlueprint 浮动。

现有代码已是「蓝图 tab 拦截、其余调用基类」的结构，开启 `setMovable` 后此结构天然兼容，无需改 `DocTabBar`。

### 4. 索引稳定性

各处通过 `tabData()`（路径 / 特殊标识）查找标签，而非硬编码索引，因此拖动排序不会破坏标签跟踪逻辑。特殊 tab（蓝图 / 游戏视图）允许被排序到任意位置，属可接受的自由排序行为。

### 5. QSS（`resources/styles/launcher.qss`）

`#docTabBar::tab` 现有 `padding: 4px 16px 4px 12px` 保留，宽度交由文字决定，无需 min/max 宽度限制。

## 验证

编译后打开多个同名前缀关卡：

1. 标签显示完整名称，不再出现「…」。
2. 悬停关卡标签显示完整路径，可区分同名关卡。
3. 标签多到放不下时出现左右滚动箭头。
4. 拖动普通关卡标签可重新排序；拖动蓝图标签仍触发浮动。
