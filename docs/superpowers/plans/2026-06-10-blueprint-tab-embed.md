# 蓝图编辑器嵌入文档Tab栏 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将蓝图编辑器嵌入顶部文档Tab栏，支持拖拽Tab浮起、关闭浮动窗口后回嵌。

**Architecture:** 新增 `DocTabBar`（QTabBar子类，检测拖拽手势）和 `BlueprintFloatWindow`（QMainWindow，浮动容器）。`EditorWindow` 中央区域改为 `QStackedWidget`，page 0 是视口，page 1 是蓝图编辑器。蓝图编辑器通过 reparent 在两种状态间切换。

**Tech Stack:** C++17, Qt6 Widgets, Qt Advanced Docking System 4.3.1

---

## 文件清单

| 操作 | 文件 |
|---|---|
| 新建 | `launcher/src/editor/DocTabBar.h` |
| 新建 | `launcher/src/editor/DocTabBar.cpp` |
| 新建 | `launcher/src/editor/BlueprintFloatWindow.h` |
| 新建 | `launcher/src/editor/BlueprintFloatWindow.cpp` |
| 修改 | `launcher/src/editor/EditorWindow.h` |
| 修改 | `launcher/src/editor/EditorWindow.cpp` |
| 修改 | `launcher/CMakeLists.txt` |

---

### Task 1: 新增 DocTabBar

**Files:**
- Create: `launcher/src/editor/DocTabBar.h`
- Create: `launcher/src/editor/DocTabBar.cpp`

- [ ] **Step 1: 创建 DocTabBar.h**

```cpp
// launcher/src/editor/DocTabBar.h
#pragma once
#include <QTabBar>

class DocTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit DocTabBar(QWidget* parent = nullptr);

    static const QString kBlueprintTabData;  // "::blueprint::"

signals:
    void blueprintDraggedOut(QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    bool   m_bpDragActive = false;
    QPoint m_bpDragStart;
};
```

- [ ] **Step 2: 创建 DocTabBar.cpp**

```cpp
// launcher/src/editor/DocTabBar.cpp
#include "DocTabBar.h"
#include <QMouseEvent>

const QString DocTabBar::kBlueprintTabData = QStringLiteral("::blueprint::");

DocTabBar::DocTabBar(QWidget* parent) : QTabBar(parent) {}

void DocTabBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int idx = tabAt(e->pos());
        if (idx >= 0 && tabData(idx).toString() == kBlueprintTabData) {
            m_bpDragActive = true;
            m_bpDragStart  = e->globalPosition().toPoint();
        }
    }
    QTabBar::mousePressEvent(e);
}

void DocTabBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_bpDragActive) {
        const QPoint delta = e->globalPosition().toPoint() - m_bpDragStart;
        if (delta.manhattanLength() > 40) {
            m_bpDragActive = false;
            emit blueprintDraggedOut(e->globalPosition().toPoint());
            return;
        }
    }
    QTabBar::mouseMoveEvent(e);
}

void DocTabBar::mouseReleaseEvent(QMouseEvent* e) {
    m_bpDragActive = false;
    QTabBar::mouseReleaseEvent(e);
}
```

- [ ] **Step 3: 编译验证（仅 CMake configure，新文件还未加入 CMakeLists）**

在 Task 3 之前先确认文件语法正确，继续下一个 Task。

---

### Task 2: 新增 BlueprintFloatWindow

**Files:**
- Create: `launcher/src/editor/BlueprintFloatWindow.h`
- Create: `launcher/src/editor/BlueprintFloatWindow.cpp`

- [ ] **Step 1: 创建 BlueprintFloatWindow.h**

```cpp
// launcher/src/editor/BlueprintFloatWindow.h
#pragma once
#include <QMainWindow>

class BlueprintFloatWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BlueprintFloatWindow(QWidget* parent = nullptr);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* e) override;
};
```

- [ ] **Step 2: 创建 BlueprintFloatWindow.cpp**

```cpp
// launcher/src/editor/BlueprintFloatWindow.cpp
#include "BlueprintFloatWindow.h"
#include <QCloseEvent>

BlueprintFloatWindow::BlueprintFloatWindow(QWidget* parent)
    : QMainWindow(parent, Qt::Window)
{
    setWindowTitle("关卡蓝图");
    resize(900, 600);
}

void BlueprintFloatWindow::closeEvent(QCloseEvent* e) {
    emit closed();
    e->accept();
}
```

---

### Task 3: 更新 CMakeLists.txt

**Files:**
- Modify: `launcher/CMakeLists.txt`

- [ ] **Step 1: 在 SOURCES 列表中追加两个新 .cpp 文件**

在 `src/editor/LayoutManager.cpp` 之后加入：

```cmake
    src/editor/DocTabBar.cpp
    src/editor/BlueprintFloatWindow.cpp
```

- [ ] **Step 2: 在 HEADERS 列表中追加两个新 .h 文件**

在 `src/editor/LayoutManager.h` 之后加入：

```cmake
    src/editor/DocTabBar.h
    src/editor/BlueprintFloatWindow.h
```

- [ ] **Step 3: 编译确认两个新文件无语法错误**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -20
```

期望：编译成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add launcher/CMakeLists.txt \
        launcher/src/editor/DocTabBar.h launcher/src/editor/DocTabBar.cpp \
        launcher/src/editor/BlueprintFloatWindow.h launcher/src/editor/BlueprintFloatWindow.cpp
git commit -m "feat: 新增 DocTabBar 和 BlueprintFloatWindow"
```

---

### Task 4: 修改 EditorWindow.h

**Files:**
- Modify: `launcher/src/editor/EditorWindow.h`

- [ ] **Step 1: 替换头文件，更新成员变量和方法声明**

完整新版 `EditorWindow.h`（只列出改动点）：

1. 添加 include：
```cpp
#include <QStackedWidget>
```

2. 前向声明部分，新增：
```cpp
class DocTabBar;
class BlueprintFloatWindow;
```

3. 将 `QTabBar* m_docTabBar` 改为：
```cpp
DocTabBar* m_docTabBar = nullptr;
```

4. 在 private 成员中，**删除**：
```cpp
ads::CDockWidget*  m_bpDockW      = nullptr;
```

5. 在 private 成员中，**新增**：
```cpp
QStackedWidget*       m_centralStack = nullptr;
QWidget*              m_viewportPage = nullptr;
BlueprintFloatWindow* m_bpFloatWin   = nullptr;
QString               m_activeLevelPath;
```

6. 在 private 方法中，**新增**：
```cpp
void openBlueprintTab();
void floatBlueprint(QPoint globalPos);
void embedBlueprint();
```

7. **删除** `#include <QTabBar>` 改为 forward declare（QTabBar 已通过 DocTabBar 间接引入）。实际上保留 `#include <QTabBar>` 也没问题，无需删除。

- [ ] **Step 2: 确认头文件能通过 MOC**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E "error:|DocTabBar|BlueprintFloat" | head -20
```

期望：无 error。

---

### Task 5: 改写 setupCentralArea

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp`

这是最大的改动。将 `setupCentralArea()` 中与 `m_bpDockW` 相关的代码替换，并把视口 wrap 改为 `QStackedWidget`。

- [ ] **Step 1: 在 EditorWindow.cpp 顶部添加两个新 include**

在 `#include "LayoutManager.h"` 之后追加：

```cpp
#include "DocTabBar.h"
#include "BlueprintFloatWindow.h"
```

- [ ] **Step 2: 替换 setupCentralArea 中视口 CDockWidget 的创建代码**

找到以下原始代码段（约第 161-173 行）：

```cpp
    auto* leftWrap = new QWidget();
    leftWrap->setObjectName("viewportWrap");
    auto* leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);
    leftLay->addWidget(buildViewportToolBar(leftWrap));
    m_viewport = new Viewport2D(leftWrap);
    leftLay->addWidget(m_viewport, 1);

    m_viewportDock = new ads::CDockWidget("视口");
    m_viewportDock->setWidget(leftWrap);
    m_viewportDock->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    auto* centralArea = m_dockManager->setCentralWidget(m_viewportDock);
```

替换为：

```cpp
    auto* leftWrap = new QWidget();
    leftWrap->setObjectName("viewportWrap");
    auto* leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);
    leftLay->addWidget(buildViewportToolBar(leftWrap));
    m_viewport = new Viewport2D(leftWrap);
    leftLay->addWidget(m_viewport, 1);
    m_viewportPage = leftWrap;

    m_blueprintEditor = new BlueprintEditor();
    connect(m_blueprintEditor, &BlueprintEditor::documentModified, this, [this]() {
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });

    m_centralStack = new QStackedWidget();
    m_centralStack->addWidget(m_viewportPage);     // index 0
    m_centralStack->addWidget(m_blueprintEditor);  // index 1

    m_viewportDock = new ads::CDockWidget("视口");
    m_viewportDock->setWidget(m_centralStack);
    m_viewportDock->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    auto* centralArea = m_dockManager->setCentralWidget(m_viewportDock);
```

- [ ] **Step 3: 删除 m_bpDockW 相关代码块**

找到并**完整删除**以下代码（约第 218-235 行）：

```cpp
    // ── 关卡蓝图（浮动窗口，默认隐藏）──────────────────────
    m_blueprintEditor = new BlueprintEditor();
    connect(m_blueprintEditor, &BlueprintEditor::documentModified, this, [this]() {
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });
    m_bpDockW = new ads::CDockWidget("关卡蓝图");
    m_bpDockW->setWidget(m_blueprintEditor);
    m_dockManager->addDockWidgetFloating(m_bpDockW);
    m_bpDockW->closeDockWidget();

    connect(m_bpDockW, &ads::CDockWidget::viewToggled,
            this, [this](bool open) {
        if (!open || !m_blueprintEditor) return;
        const int idx = m_docTabBar->currentIndex();
        const QString path = idx >= 0 ? m_docTabBar->tabData(idx).toString() : QString{};
        m_blueprintEditor->loadLevel(m_openLevels.value(path, nullptr));
    });
```

- [ ] **Step 4: 在 m_cbDockW 创建之后（LayoutManager 之前）添加浮动窗口初始化**

在 `// ── 布局管理器` 注释行之前，添加：

```cpp
    // ── 蓝图浮动窗口 ──────────────────────────────────────────────────
    m_bpFloatWin = new BlueprintFloatWindow(this);
    connect(m_bpFloatWin, &BlueprintFloatWindow::closed,
            this, &EditorWindow::embedBlueprint);
```

- [ ] **Step 5: 编译确认**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -30
```

期望：编译成功。若有 `m_bpDockW` 残留引用错误，找到对应行删除。

---

### Task 6: 更新 setupDocTabBar、setupWindowMenu、视口工具栏按钮

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp`

- [ ] **Step 1: setupDocTabBar — 将 QTabBar 换为 DocTabBar，连接拖拽信号**

找到（约第 93 行）：

```cpp
    auto* tabBar = new QTabBar(tb);
```

替换为：

```cpp
    auto* tabBar = new DocTabBar(tb);
```

在 `connect(tabBar, &QTabBar::tabCloseRequested, ...)` 之后追加：

```cpp
    connect(tabBar, &DocTabBar::blueprintDraggedOut,
            this, &EditorWindow::floatBlueprint);
```

- [ ] **Step 2: setupWindowMenu — 删除 m_bpDockW 的菜单项**

找到并**删除**这一行：

```cpp
    m_windowMenu->addAction(m_bpDockW->toggleViewAction());
```

- [ ] **Step 3: buildViewportToolBar — 替换「关卡蓝图」按钮逻辑**

找到（约第 351-360 行）：

```cpp
    connect(bpBtn, &QToolButton::clicked, this, [this]() {
        if (!m_bpDockW) return;
        if (m_bpDockW->isClosed()) {
            m_bpDockW->toggleView(true);
            if (!m_bpDockW->isFloating())
                m_bpDockW->setFloating();
        } else {
            m_bpDockW->toggleView(false);
        }
    });
```

替换为：

```cpp
    connect(bpBtn, &QToolButton::clicked, this, &EditorWindow::openBlueprintTab);
```

- [ ] **Step 4: 编译确认**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -20
```

期望：无编译错误。

---

### Task 7: 实现三个新方法

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp`

在 `EditorWindow::stopRuntime()` 函数之后（文件末尾）追加三个方法：

- [ ] **Step 1: 追加 openBlueprintTab**

```cpp
void EditorWindow::openBlueprintTab() {
    // 已嵌入：切换到蓝图 Tab
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == DocTabBar::kBlueprintTabData) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }
    // 已浮动：置顶浮动窗口
    if (m_bpFloatWin && m_bpFloatWin->isVisible()) {
        m_bpFloatWin->raise();
        m_bpFloatWin->activateWindow();
        return;
    }
    // 首次打开：确保蓝图编辑器在 stack 中
    if (m_centralStack->indexOf(m_blueprintEditor) < 0)
        m_centralStack->addWidget(m_blueprintEditor);

    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  关卡蓝图");
        m_docTabBar->setTabData(idx, DocTabBar::kBlueprintTabData);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}
```

- [ ] **Step 2: 追加 floatBlueprint**

```cpp
void EditorWindow::floatBlueprint(QPoint globalPos) {
    // 移除 Tab
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == DocTabBar::kBlueprintTabData) {
            QSignalBlocker b(m_docTabBar);
            m_docTabBar->removeTab(i);
            break;
        }
    }
    // 切换视口
    m_centralStack->setCurrentWidget(m_viewportPage);
    // 将蓝图编辑器从 stack 移入浮动窗口
    m_centralStack->removeWidget(m_blueprintEditor);
    m_bpFloatWin->setCentralWidget(m_blueprintEditor);
    m_bpFloatWin->move(globalPos - QPoint(50, 10));
    m_bpFloatWin->show();
    m_bpFloatWin->raise();
}
```

- [ ] **Step 3: 追加 embedBlueprint**

```cpp
void EditorWindow::embedBlueprint() {
    // 从浮动窗口取回蓝图编辑器（reparent 到 nullptr）
    QWidget* bpWidget = m_bpFloatWin->takeCentralWidget();
    if (!bpWidget) bpWidget = m_blueprintEditor;

    // 加回 stack
    m_centralStack->addWidget(bpWidget);

    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  关卡蓝图");
        m_docTabBar->setTabData(idx, DocTabBar::kBlueprintTabData);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}
```

- [ ] **Step 4: 编译确认**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -20
```

期望：无编译错误。

---

### Task 8: 更新 onTabChanged 和 onTabClosed

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp`

- [ ] **Step 1: onTabChanged — 在函数开头 path 变量之后，添加蓝图 Tab 的分支**

找到（约第 433 行）：

```cpp
    const QString path = m_docTabBar->tabData(index).toString();

    if (path.isEmpty()) {
```

在 `if (path.isEmpty())` 之前插入：

```cpp
    // 蓝图 Tab
    if (path == DocTabBar::kBlueprintTabData) {
        if (m_centralStack->indexOf(m_blueprintEditor) < 0)
            m_centralStack->addWidget(m_blueprintEditor);
        m_centralStack->setCurrentWidget(m_blueprintEditor);
        LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
        if (m_blueprintEditor) m_blueprintEditor->loadLevel(doc);
        return;
    }

    // 切换到视口
    if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);
```

- [ ] **Step 2: onTabChanged — 在加载关卡后记录 m_activeLevelPath，并同步浮动蓝图**

找到（约第 447 行）：

```cpp
    LevelDocument* doc = m_openLevels[path];

    m_sceneOutliner->loadLevel(doc);
    if (m_viewport) m_viewport->loadLevel(doc);

    if (m_bpDockW && !m_bpDockW->isClosed() && m_blueprintEditor)
        m_blueprintEditor->loadLevel(doc);
```

替换为：

```cpp
    LevelDocument* doc = m_openLevels[path];
    m_activeLevelPath = path;

    m_sceneOutliner->loadLevel(doc);
    if (m_viewport) m_viewport->loadLevel(doc);

    if (m_bpFloatWin && m_bpFloatWin->isVisible() && m_blueprintEditor)
        m_blueprintEditor->loadLevel(doc);
```

- [ ] **Step 3: onTabClosed — 在函数开头添加蓝图 Tab 的特殊处理**

找到（约第 508 行）：

```cpp
void EditorWindow::onTabClosed(int index) {
    const QString path = m_docTabBar->tabData(index).toString();
    LevelDocument* doc = m_openLevels.value(path);
```

在 `LevelDocument* doc = ...` 之前插入：

```cpp
    if (path == DocTabBar::kBlueprintTabData) {
        m_docTabBar->removeTab(index);
        if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);
        return;
    }
```

- [ ] **Step 4: 编译并启动**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

期望：编译成功，应用正常启动。

- [ ] **Step 5: 手动验证**

1. 打开一个项目 → 进入编辑器
2. 点击视口工具栏「关卡蓝图」按钮 → 顶部 Tab 栏出现「关卡蓝图」Tab，中央区域显示蓝图画布
3. 切换到关卡 Tab → 中央区域切回视口
4. 切换回蓝图 Tab → 再次显示蓝图画布
5. 按住蓝图 Tab 向下拖拽 > 40px → Tab 消失，蓝图编辑器以浮动窗口出现
6. 关闭浮动窗口 → 蓝图 Tab 重新出现在 Tab 栏

- [ ] **Step 6: 提交**

```bash
git add launcher/src/editor/EditorWindow.h launcher/src/editor/EditorWindow.cpp
git commit -m "feat: 蓝图编辑器嵌入文档Tab栏，支持拖拽浮起"
```
