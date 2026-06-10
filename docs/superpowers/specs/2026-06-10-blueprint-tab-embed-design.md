# 蓝图编辑器嵌入文档Tab栏 设计文档

**日期**: 2026-06-10  
**状态**: 已批准

---

## 需求

- 打开关卡蓝图时，蓝图编辑器以 Tab 形式嵌入顶部文档Tab栏（与关卡 Tab 同排）
- 左键按住蓝图 Tab 拖拽 > 40px 后，蓝图编辑器脱离 Tab 栏变为浮动窗口
- 关闭浮动窗口后，蓝图 Tab 自动回到 Tab 栏

---

## 架构

### 状态机

BlueprintEditor 在两种互斥状态间切换：

```
[嵌入态]  ←──────────────────────── 关闭浮动窗口
    │                                        ↑
    └── 拖拽 Tab > 40px ──────→ [浮动态]
```

**嵌入态**：
- `m_blueprintEditor` 的 parent 是 `m_centralStack`（第 1 页）
- `m_docTabBar` 含有 `关卡蓝图 ×` Tab（tabData = `"::blueprint::"`）
- `m_bpFloatWin` 隐藏

**浮动态**：
- `m_blueprintEditor` 的 parent 是 `m_bpFloatWin`（通过 reparent 移动）
- `m_docTabBar` 不含蓝图 Tab
- `m_bpFloatWin` 可见，为独立的 `QMainWindow`

---

## 组件变更

### 1. QStackedWidget 替换视口区域

`setupCentralArea()` 中，将 `m_viewportDock` 的 widget 改为 `m_centralStack`（`QStackedWidget`）：

```
m_centralStack
  ├── [0] viewportPage  (leftWrap：视口工具栏 + Viewport2D)
  └── [1] m_blueprintEditor  (嵌入态时的位置)
```

`onTabChanged` 根据 Tab 类型切换 `m_centralStack` 的当前页：
- 关卡 Tab → 切到第 0 页（视口）
- 蓝图 Tab → 切到第 1 页（蓝图编辑器），同时调用 `m_blueprintEditor->loadLevel(doc)`

### 2. DocTabBar（QTabBar 子类）

新增 `DocTabBar`，替换原来的 `QTabBar`。

需要添加的字段：
```cpp
bool    m_bpDragActive = false;
QPoint  m_bpDragStart;
int     m_bpTabIndex   = -1;  // 蓝图 Tab 的 index，-1 表示不存在
```

重写：
- `mousePressEvent`：若点击的是蓝图 Tab，记录起始位置，设 `m_bpDragActive = true`
- `mouseMoveEvent`：若 `m_bpDragActive` 且偏移 > 40px，发射 `blueprintDraggedOut(QPoint globalPos)` 信号
- `mouseReleaseEvent`：重置拖拽状态

信号：
```cpp
signals:
    void blueprintDraggedOut(QPoint globalPos);
```

### 3. BlueprintFloatWindow（QMainWindow 子类）

新增 `BlueprintFloatWindow`（`m_bpFloatWin`），轻量浮动容器：

- 构造时设置窗口标题 `"关卡蓝图"`、初始大小 `900×600`
- `closeEvent`：发射 `closed()` 信号，不直接 delete
- 窗口标题栏有标准关闭按钮

信号：
```cpp
signals:
    void closed();
```

### 4. EditorWindow 新增方法

```cpp
void openBlueprintTab();   // 打开/切换到蓝图 Tab（嵌入态）
void floatBlueprint(QPoint pos);   // 嵌入态 → 浮动态
void embedBlueprint();     // 浮动态 → 嵌入态
```

`openBlueprintTab()`：
1. 若已有蓝图 Tab，切换到该 Tab，返回
2. 若处于浮动态，raise 浮动窗口，返回
3. 否则：将 `m_blueprintEditor` reparent 到 `m_centralStack`（若不在），调用 `m_centralStack->addWidget(m_blueprintEditor)`，添加蓝图 Tab，切换到该 Tab

`floatBlueprint(pos)`：
1. 从 `m_docTabBar` 移除蓝图 Tab
2. 从 `m_centralStack` 取出 `m_blueprintEditor`（`m_centralStack->removeWidget`）
3. `m_bpFloatWin->setCentralWidget(m_blueprintEditor)` — reparent
4. `m_bpFloatWin->move(pos)` / `show()`
5. 切换 `m_centralStack` 到第 0 页（视口）

`embedBlueprint()`：
1. 从 `m_bpFloatWin` 取回 `m_blueprintEditor`（`takeCentralWidget()`）
2. `m_centralStack->addWidget(m_blueprintEditor)` — 重新加入 stack
3. 添加蓝图 Tab 到 `m_docTabBar`，切换到该 Tab

### 5. 视口工具栏「关卡蓝图」按钮行为变更

原逻辑（ADS float）→ 改为调用 `openBlueprintTab()`。

### 6. 移除 m_bpDockW

`m_bpDockW`（ADS CDockWidget）不再需要，从 `EditorWindow` 中移除。`setupWindowMenu` 中对应的 toggleViewAction 也一并移除。

---

## 数据流

### 关卡切换时同步蓝图

`onTabChanged` 已有逻辑，改写为：

```cpp
// 若蓝图处于嵌入态（当前 Tab 是蓝图 Tab）
if (path == "::blueprint::") {
    m_blueprintEditor->loadLevel(currentDoc);
}
// 若蓝图处于浮动态（窗口可见）
if (m_bpFloatWin->isVisible()) {
    m_blueprintEditor->loadLevel(currentDoc);
}
```

### 蓝图 Tab 的 tabData 约定

```
tabData = QString("::blueprint::")
```

---

## 文件变更清单

| 文件 | 变更 |
|---|---|
| `EditorWindow.h` | 新增 `m_centralStack`、`m_bpFloatWin`、3 个方法；移除 `m_bpDockW` |
| `EditorWindow.cpp` | 改写 `setupCentralArea`、`onTabChanged`、视口工具栏按钮逻辑；新增 3 个方法 |
| `DocTabBar.h` / `.cpp` | 新增文件：QTabBar 子类，含拖拽检测 |
| `BlueprintFloatWindow.h` / `.cpp` | 新增文件：浮动容器 QMainWindow |
| `CMakeLists.txt` | 添加 4 个新文件到 SOURCES / HEADERS |

---

## 不受影响的部分

- 关卡 Tab 的打开、关闭、保存、脏标记逻辑
- ADS 大纲 / 细节 / 内容浏览器面板
- LayoutManager
- BlueprintEditor 内部逻辑（无改动）
