# 移除大纲/细节面板移动功能 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 删除大纲和细节面板的拖拽、浮动、停靠、合并 Tab、还原布局功能，使两个面板固定在右侧上下布局，不可移动。

**Architecture:** 保留 `QDockWidget` 基础结构和 `splitDockWidget` 垂直分割，通过 `NoDockWidgetFeatures` + 空 titleBarWidget 锁定面板；移除 `AllowTabbedDocks` 全局选项、`fixDockTabExpand` 事件监听、`childEvent` 样式补丁、`resetLayout` 按钮及函数、`m_defaultLayoutState` 捕获逻辑。

**Tech Stack:** C++17, Qt6 Widgets, CMake

---

### Task 1：锁定两个面板为不可移动

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp:324-341`

- [ ] **Step 1：在 `setupCentralArea()` 的 `addDockWidget` 之后添加两行锁定语句**

找到 `setupCentralArea()` 中 `addDockWidget(Qt::RightDockWidgetArea, m_outlineDock);`（约 L330），在该行后插入：

```cpp
m_outlineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
m_outlineDock->setTitleBarWidget(new QWidget(m_outlineDock));
```

找到 `splitDockWidget(m_outlineDock, m_detailsDock, Qt::Vertical);`（约 L337）前的 details dock 创建块，在 `m_detailsDock` 的 `setAllowedAreas` 下方插入：

```cpp
m_detailsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
m_detailsDock->setTitleBarWidget(new QWidget(m_detailsDock));
```

- [ ] **Step 2：移除两处 `setAllowedAreas`（允许移到左侧的配置）**

删除：
```cpp
m_outlineDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
```
删除：
```cpp
m_detailsDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
```

- [ ] **Step 3：删除 `setTabPosition` 行（仅服务于合并后 Tab 位置）**

删除：
```cpp
// 用户手动拖拽合并后，Tab 显示在顶部
setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
```

---

### Task 2：移除全局 AllowTabbedDocks 和 fixDockTabExpand 块

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp:46-89`

- [ ] **Step 1：修改构造函数中的第二处 `setDockOptions` 调用**

找到（约 L50）：
```cpp
setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);  // 之后再允许用户拖拽合并
```
改为（移除 AllowTabbedDocks，保留 AnimatedDocks）：
```cpp
setDockOptions(QMainWindow::AnimatedDocks);
```

- [ ] **Step 2：删除 `m_defaultLayoutState = saveState();`（约 L49）**

删除：
```cpp
m_defaultLayoutState = saveState();  // 捕获正确的垂直分割布局
```

- [ ] **Step 3：删除整个 `kDockTabStyle` + `fixDockTabExpand` lambda + 5 个 connect（约 L52-89）**

删除以下全部内容（从 `const char* kDockTabStyle =` 到 `connect(this, &QMainWindow::tabifiedDockWidgetActivated, this, fixDockTabExpand);` 的闭合 `};` 后一行）：

```cpp
const char* kDockTabStyle =
    "QTabBar { background: transparent; }"
    // ... 整段 kDockTabStyle 字符串定义

// Tab 紧凑：Dock 状态变化时找到所有 Dock Tab 栏并关闭撑满 + 靠左对齐 + 强制编辑器样式
auto fixDockTabExpand = [this, kDockTabStyle]() {
    // ... 整个 lambda
};
connect(m_outlineDock,  &QDockWidget::dockLocationChanged, this, fixDockTabExpand);
connect(m_detailsDock,  &QDockWidget::dockLocationChanged, this, fixDockTabExpand);
connect(m_outlineDock,  &QDockWidget::topLevelChanged,     this, fixDockTabExpand);
connect(m_detailsDock,  &QDockWidget::topLevelChanged,     this, fixDockTabExpand);
connect(this, &QMainWindow::tabifiedDockWidgetActivated,   this, fixDockTabExpand);
```

---

### Task 3：删除"还原布局"按钮和 resetLayout 函数

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp:177-178` 和 `951-955`

- [ ] **Step 1：删除 `setupMainToolBar()` 中的 resetBtn（约 L177-178）**

删除：
```cpp
auto* resetBtn = tbBtn("还原布局", "还原默认面板布局");
connect(resetBtn, &QToolButton::clicked, this, &EditorWindow::resetLayout);
```

- [ ] **Step 2：删除 `resetLayout()` 函数体（约 L951-955）**

删除整个函数：
```cpp
void EditorWindow::resetLayout() {
    restoreState(m_defaultLayoutState);
    if (m_outlineDock) m_outlineDock->show();
    if (m_detailsDock) m_detailsDock->show();
}
```

---

### Task 4：删除 childEvent 函数

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp:988-1022`

- [ ] **Step 1：删除整个 `childEvent()` 函数（约 L988-1022）**

删除以下全部内容：
```cpp
// 当 QMainWindow 动态创建 Dock Tab 栏时，自动关闭 Tab 均分拉伸、靠左对齐、强制编辑器样式
void EditorWindow::childEvent(QChildEvent* e) {
    QMainWindow::childEvent(e);
    if (e->added()) {
        if (auto* tb = qobject_cast<QTabBar*>(e->child())) {
            if (tb != m_docTabBar) {
                QTimer::singleShot(50, [tb] {
                    // ... 全部内容
                });
            }
        }
    }
}
```

---

### Task 5：清理头文件声明和成员变量

**Files:**
- Modify: `launcher/src/editor/EditorWindow.h:31,43,85`
- Modify: `launcher/src/editor/EditorWindow.cpp:1-33`（includes 行）

- [ ] **Step 1：删除 `EditorWindow.h` 中的三处声明**

删除：
```cpp
void childEvent(QChildEvent* e) override;
```
删除：
```cpp
void resetLayout();
```
删除：
```cpp
QByteArray   m_defaultLayoutState;
```

- [ ] **Step 2：删除 `EditorWindow.cpp` 中的 `#include <QChildEvent>`**

删除：
```cpp
#include <QChildEvent>
```

---

### Task 6：编译验证

**Files:** 无修改，仅执行构建命令

- [ ] **Step 1：编译并启动**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

期望：0 error，0 warning（或仅有已有的 IDE 误报）。应用正常启动。

- [ ] **Step 2：回归检查清单**

布局检查：
- 大纲面板固定右侧上方，无可拖拽 title bar
- 细节面板固定右侧下方，无可拖拽 title bar
- 两面板无法拖出、无法浮动、无法合并 Tab
- 主工具栏"还原布局"按钮已消失

功能回归：
- 顶部 docTabBar 关卡 Tab 正常切换/关闭
- 蓝图编辑器浮动弹窗正常（bpPanel 拖拽、停靠 Tab、再浮出）
- 内容浏览器 toggle 显示/隐藏正常
- 大纲选中 Actor → 细节面板更新
- 细节面板修改 → 视口刷新
- 视口拖拽 Actor → 细节坐标同步
- 关卡保存（Ctrl+S）、脏标记、Tab 标题 `●` 前缀
- 运行/停止按钮功能正常
- 关闭编辑器时未保存提示弹窗正常

- [ ] **Step 3：如编译报错，定位行号修复后重新编译**
