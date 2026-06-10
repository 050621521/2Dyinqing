# Docking Panel System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 Qt Advanced Docking System (ADS) 替换编辑器现有的固定面板和自制浮动逻辑，实现类 UE 风格的完整停靠系统（大纲、细节、内容浏览器、蓝图编辑器全部接入），并支持多套命名布局持久化。

**Architecture:** `EditorWindow` 持有 `ads::CDockManager`，五个面板各自包装为 `ads::CDockWidget`；`LayoutManager` 类封装 ADS 状态序列化，读写 `{project}/editor_layouts.json`；布局切换通过"窗口→布局"子菜单操作。

**Tech Stack:** Qt6 Widgets, Qt Advanced Docking System 4.3.1 (FetchContent), C++17, QJsonDocument

---

## 文件清单

| 操作 | 路径 | 职责 |
|------|------|------|
| 修改 | `launcher/CMakeLists.txt` | 添加 ADS FetchContent + link |
| 新建 | `launcher/src/editor/LayoutManager.h` | 布局管理器声明 |
| 新建 | `launcher/src/editor/LayoutManager.cpp` | 布局管理器实现 |
| 修改 | `launcher/src/editor/EditorWindow.h` | 替换旧成员为 ADS 成员 |
| 修改 | `launcher/src/editor/EditorWindow.cpp` | 全面重构停靠逻辑 |
| 修改 | `launcher/resources/styles/launcher.qss` | 追加 ADS QSS 样式 |

---

## Task 1: CMake — 集成 ADS

**Files:**
- Modify: `launcher/CMakeLists.txt`

- [ ] **Step 1: 在 CMakeLists.txt 中添加 FetchContent**

将以下内容插入 `find_package(Qt6 ...)` 之后，`set(SOURCES ...)` 之前：

```cmake
include(FetchContent)
FetchContent_Declare(
    QtADS
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        4.3.1
)
set(ADS_VERSION "4.3.1" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QtADS)
```

将 `target_link_libraries` 行替换为：

```cmake
target_link_libraries(launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets qt6advanceddocking)
```

- [ ] **Step 2: 重新 configure + 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build
cmake .. && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -20
```

预期：第一次会联网拉取 ADS，最终 `[100%] Linking CXX executable launcher.app/Contents/MacOS/launcher`，无错误。

- [ ] **Step 3: 确认 ADS 头文件可用**

```bash
ls /Users/kwy/Documents/2Dyinqing/launcher/build/_deps/qtads-src/src/DockManager.h
```

预期：文件存在。

- [ ] **Step 4: Commit**

```bash
git add launcher/CMakeLists.txt
git commit -m "build: 集成 Qt Advanced Docking System 4.3.1"
```

---

## Task 2: LayoutManager — 新建布局管理器

**Files:**
- Create: `launcher/src/editor/LayoutManager.h`
- Create: `launcher/src/editor/LayoutManager.cpp`
- Modify: `launcher/CMakeLists.txt`

- [ ] **Step 1: 创建 LayoutManager.h**

```cpp
// launcher/src/editor/LayoutManager.h
#pragma once
#include <QObject>
#include <QMap>
#include <QByteArray>
#include <QStringList>

namespace ads { class CDockManager; }

class LayoutManager : public QObject {
    Q_OBJECT
public:
    explicit LayoutManager(ads::CDockManager* dock,
                           const QString& projectPath,
                           QObject* parent = nullptr);

    // 初始布局建好后调用一次，捕获并存储出厂默认状态
    void captureDefault();

    QStringList layoutNames() const;
    QString     currentLayout() const;

    void saveLayout(const QString& name);    // 保存/覆盖
    bool loadLayout(const QString& name);    // 恢复，失败返回 false
    void deleteLayout(const QString& name);  // 不允许删除"默认布局"
    void resetDefault();                     // 恢复到出厂默认

signals:
    void layoutListChanged();
    void currentLayoutChanged(const QString& name);

private:
    void readFile();
    void writeFile();

    ads::CDockManager*     m_dock;
    QString                m_filePath;
    QString                m_current;
    QMap<QString, QString> m_layouts;   // name → base64(ADS state)
    QByteArray             m_defaultState;
};
```

- [ ] **Step 2: 创建 LayoutManager.cpp**

```cpp
// launcher/src/editor/LayoutManager.cpp
#include "LayoutManager.h"
#include <DockManager.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

LayoutManager::LayoutManager(ads::CDockManager* dock,
                              const QString& projectPath,
                              QObject* parent)
    : QObject(parent), m_dock(dock)
    , m_filePath(projectPath + "/editor_layouts.json")
{
    readFile();
}

void LayoutManager::captureDefault() {
    m_defaultState = m_dock->saveState();
    if (!m_layouts.contains("默认布局")) {
        m_layouts["默认布局"] = QString::fromLatin1(m_defaultState.toBase64());
        if (m_current.isEmpty()) m_current = "默认布局";
        writeFile();
        emit layoutListChanged();
    } else {
        // 文件已有布局，恢复上次保存的状态
        loadLayout(m_current.isEmpty() ? "默认布局" : m_current);
    }
}

QStringList LayoutManager::layoutNames() const {
    return m_layouts.keys();
}

QString LayoutManager::currentLayout() const {
    return m_current;
}

void LayoutManager::saveLayout(const QString& name) {
    m_layouts[name] = QString::fromLatin1(m_dock->saveState().toBase64());
    m_current = name;
    writeFile();
    emit layoutListChanged();
    emit currentLayoutChanged(name);
}

bool LayoutManager::loadLayout(const QString& name) {
    if (!m_layouts.contains(name)) return false;
    const QByteArray state =
        QByteArray::fromBase64(m_layouts.value(name).toLatin1());
    if (!m_dock->restoreState(state)) return false;
    m_current = name;
    writeFile();
    emit currentLayoutChanged(name);
    return true;
}

void LayoutManager::deleteLayout(const QString& name) {
    if (name == "默认布局") return;
    m_layouts.remove(name);
    if (m_current == name) {
        m_current = "默认布局";
        loadLayout("默认布局");
    }
    writeFile();
    emit layoutListChanged();
}

void LayoutManager::resetDefault() {
    loadLayout("默认布局");
}

void LayoutManager::readFile() {
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto obj = QJsonDocument::fromJson(f.readAll()).object();
    m_current = obj["current"].toString();
    const auto layouts = obj["layouts"].toObject();
    for (auto it = layouts.begin(); it != layouts.end(); ++it)
        m_layouts[it.key()] = it.value().toString();
}

void LayoutManager::writeFile() {
    QJsonObject layouts;
    for (auto it = m_layouts.begin(); it != m_layouts.end(); ++it)
        layouts[it.key()] = it.value();
    QJsonObject root;
    root["current"] = m_current;
    root["layouts"] = layouts;
    QFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}
```

- [ ] **Step 3: 在 CMakeLists.txt 的 SOURCES/HEADERS 列表中注册新文件**

在 `set(SOURCES ...)` 块的 `src/editor/BPRuntime.cpp` 行后添加：
```cmake
    src/editor/LayoutManager.cpp
```

在 `set(HEADERS ...)` 块的 `src/editor/BPRuntime.h` 行后添加：
```cmake
    src/editor/LayoutManager.h
```

- [ ] **Step 4: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build
cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -10
```

预期：编译成功，无错误。

- [ ] **Step 5: Commit**

```bash
git add launcher/src/editor/LayoutManager.h \
        launcher/src/editor/LayoutManager.cpp \
        launcher/CMakeLists.txt
git commit -m "feat: 新增 LayoutManager — ADS 布局序列化管理器"
```

---

## Task 3: EditorWindow — 全面重构

**Files:**
- Modify: `launcher/src/editor/EditorWindow.h`
- Modify: `launcher/src/editor/EditorWindow.cpp`

### Step 1: 替换 EditorWindow.h

- [ ] **用以下内容完整替换 `launcher/src/editor/EditorWindow.h`**

```cpp
#pragma once
#include "models/ProjectInfo.h"
#include "models/LevelDocument.h"
#include <QMainWindow>
#include <QMap>
#include <QButtonGroup>

class QTabBar;
class QMenu;
class QToolButton;
class QLabel;
class QButtonGroup;
class SceneOutliner;
class DetailsPanel;
class Viewport2D;
class BlueprintEditor;
class BPRuntime;
class LayoutManager;

namespace ads {
    class CDockManager;
    class CDockWidget;
}

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(const ProjectInfo& project, QWidget* parent = nullptr);

signals:
    void editorClosed();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void setupMenuBar();
    void setupDocTabBar();
    void setupMainToolBar();
    void setupCentralArea();
    void setupBottomBar();
    void setupWindowMenu();       // 面板显隐 + 布局子菜单，在 setupCentralArea 之后调用
    void rebuildLayoutMenu();     // LayoutManager::layoutListChanged 时重建子菜单

    QWidget* buildViewportToolBar(QWidget* parent);
    void     openLevelTab(const QString& path);
    void     saveCurrentLevel();
    void     saveAllLevels();
    void     updateTabTitle(int index);
    void     updateSaveLabel();

private slots:
    void onTabChanged(int index);
    void onTabClosed(int index);
    void onProjectSettings();
    void startRuntime();
    void stopRuntime();

private:
    ProjectInfo  m_project;

    // 文档 Tab
    QTabBar* m_docTabBar = nullptr;

    // 视口
    Viewport2D* m_viewport = nullptr;

    // 面板
    SceneOutliner*   m_sceneOutliner = nullptr;
    DetailsPanel*    m_detailsPanel  = nullptr;
    BlueprintEditor* m_blueprintEditor = nullptr;

    // ADS
    ads::CDockManager* m_dockManager    = nullptr;
    ads::CDockWidget*  m_viewportDock   = nullptr;
    ads::CDockWidget*  m_outlineDockW   = nullptr;
    ads::CDockWidget*  m_detailsDockW   = nullptr;
    ads::CDockWidget*  m_cbDockW        = nullptr;
    ads::CDockWidget*  m_bpDockW        = nullptr;

    // 布局管理
    LayoutManager* m_layoutManager = nullptr;
    QMenu*         m_windowMenu    = nullptr;
    QMenu*         m_layoutMenu    = nullptr;

    // 工具栏
    QButtonGroup* m_toolBtnGroup = nullptr;
    QToolButton*  m_runBtn       = nullptr;
    QToolButton*  m_stopBtn      = nullptr;
    QLabel*       m_saveLabel    = nullptr;

    // 运行时
    BPRuntime* m_runtime = nullptr;

    QMap<QString, LevelDocument*> m_openLevels;
    QList<QMetaObject::Connection> m_tabConnections;
};
```

### Step 2: 替换 EditorWindow.cpp

- [ ] **用以下内容完整替换 `launcher/src/editor/EditorWindow.cpp`**

```cpp
#include "EditorWindow.h"
#include "Viewport2D.h"
#include "BlueprintEditor.h"
#include "BPRuntime.h"
#include "SceneOutliner.h"
#include "DetailsPanel.h"
#include "ContentBrowser.h"
#include "LayoutManager.h"
#include "ProjectSettingsDialog.h"
#include "models/LevelDocument.h"
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>
#include <QMetaObject>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QToolBar>
#include <QTabBar>
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QFrame>
#include <QSizePolicy>
#include <QCloseEvent>
#include <QTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include <QButtonGroup>

EditorWindow::EditorWindow(const ProjectInfo& project, QWidget* parent)
    : QMainWindow(parent), m_project(project)
{
    setWindowTitle(project.name + " — 2D引擎编辑器");
    setMinimumSize(1100, 700);
    resize(1440, 900);

    setupMenuBar();
    setupDocTabBar();
    addToolBarBreak();
    setupMainToolBar();
    setupCentralArea();
    setupBottomBar();
    setupWindowMenu();

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &EditorWindow::saveCurrentLevel);

    QString defaultLevel = ProjectSettingsDialog::readDefaultLevel(m_project.path);
    if (defaultLevel.isEmpty())
        defaultLevel = m_project.path + "/Levels/Default.level";
    if (QFileInfo::exists(defaultLevel))
        openLevelTab(defaultLevel);
}

// ── 1. 菜单栏 ────────────────────────────────────────────────────────
void EditorWindow::setupMenuBar() {
    auto* mb = menuBar();
    mb->setNativeMenuBar(false);
    mb->setObjectName("editorMenuBar");

    auto* fileMenu = mb->addMenu("文件");
    auto* projSettingsAct = fileMenu->addAction("项目设置…");
    connect(projSettingsAct, &QAction::triggered, this, &EditorWindow::onProjectSettings);

    for (const QString& n : {"编辑", "工具", "构建", "选择", "Actor", "帮助"})
        mb->addMenu(n);

    m_windowMenu = mb->addMenu("窗口");

    auto* spacer = new QWidget(mb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mb->setCornerWidget(spacer, Qt::TopRightCorner);
}

// ── 2. 文档 Tab 栏 ───────────────────────────────────────────────────
void EditorWindow::setupDocTabBar() {
    auto* tb = new QToolBar(this);
    tb->setObjectName("docTabToolBar");
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setFixedHeight(30);
    tb->setContextMenuPolicy(Qt::PreventContextMenu);

    auto* tabBar = new QTabBar(tb);
    tabBar->setObjectName("docTabBar");
    tabBar->setTabsClosable(true);
    tabBar->setExpanding(false);
    m_docTabBar = tabBar;
    tb->addWidget(tabBar);
    addToolBar(Qt::TopToolBarArea, tb);

    connect(tabBar, &QTabBar::currentChanged,    this, &EditorWindow::onTabChanged);
    connect(tabBar, &QTabBar::tabCloseRequested, this, &EditorWindow::onTabClosed);
}

// ── 3. 主工具栏 ──────────────────────────────────────────────────────
void EditorWindow::setupMainToolBar() {
    auto* tb = new QToolBar(this);
    tb->setObjectName("editorMainToolBar");
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setFixedHeight(38);

    auto tbBtn = [&](const QString& txt, const QString& tip = "") -> QToolButton* {
        auto* b = new QToolButton(tb);
        b->setText(txt); b->setToolTip(tip);
        b->setObjectName("mainTBBtn");
        tb->addWidget(b);
        return b;
    };
    auto dropBtn = [&](const QString& txt) {
        auto* b = new QPushButton(txt, tb);
        b->setObjectName("mainTBDropBtn");
        tb->addWidget(b);
    };

    tbBtn("⊞"); tbBtn("⊡");
    tb->addSeparator();
    dropBtn("选择模式  ▾");
    tb->addSeparator();
    m_runBtn  = tbBtn("▶",  "运行");
    tbBtn("⏸", "暂停");
    m_stopBtn = tbBtn("⏹",  "停止");
    tbBtn("⏭",  "跳帧");
    tb->addSeparator();
    connect(m_runBtn,  &QToolButton::clicked, this, &EditorWindow::startRuntime);
    connect(m_stopBtn, &QToolButton::clicked, this, &EditorWindow::stopRuntime);
    m_stopBtn->setEnabled(false);
    dropBtn("平台  ▾");
    tb->addSeparator();

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    auto* lbl = new QLabel(m_project.name, tb);
    lbl->setObjectName("projNameLabel");
    lbl->setContentsMargins(0, 0, 14, 0);
    tb->addWidget(lbl);

    addToolBar(Qt::TopToolBarArea, tb);
}

// ── 4. 中央区域（ADS） ────────────────────────────────────────────────
void EditorWindow::setupCentralArea() {
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlAutoFormattingEnabled, true);
    m_dockManager = new ads::CDockManager(this);

    // ── 视口（中央固定，不可关闭/浮动/移动）─────────────────────────
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

    // ── 大纲（右上）─────────────────────────────────────────────────
    m_sceneOutliner = new SceneOutliner();
    m_outlineDockW  = new ads::CDockWidget("大纲");
    m_outlineDockW->setWidget(m_sceneOutliner);
    auto* rightArea = m_dockManager->addDockWidget(
        ads::RightDockWidgetArea, m_outlineDockW);

    // ── 细节（右下，与大纲同区域堆叠）──────────────────────────────
    m_detailsPanel = new DetailsPanel();
    m_detailsPanel->setProjectRoot(m_project.path);
    m_detailsDockW = new ads::CDockWidget("细节");
    m_detailsDockW->setWidget(m_detailsPanel);
    m_dockManager->addDockWidget(
        ads::BottomDockWidgetArea, m_detailsDockW, rightArea);

    // ── 内容浏览器（底部，默认隐藏）─────────────────────────────────
    auto* cbContainer = new QWidget();
    auto* cbLay = new QVBoxLayout(cbContainer);
    cbLay->setContentsMargins(0, 0, 0, 0);
    cbLay->setSpacing(0);
    auto* cb = new ContentBrowser(m_project.path, cbContainer);
    connect(cb, &ContentBrowser::levelOpenRequested,
            this, [this](const QString& path) { openLevelTab(path); });
    connect(cb, &ContentBrowser::saveAllRequested,
            this, &EditorWindow::saveAllLevels);
    connect(cb, &ContentBrowser::imageAssignRequested,
            m_detailsPanel, &DetailsPanel::assignSpritePath);
    connect(cb, &ContentBrowser::levelFileDeleted,
            this, [this](const QString& path) {
        for (int i = 0; i < m_docTabBar->count(); ++i) {
            if (m_docTabBar->tabData(i).toString() == path) {
                onTabClosed(i);
                break;
            }
        }
    });
    cbLay->addWidget(cb, 1);

    m_cbDockW = new ads::CDockWidget("内容浏览器");
    m_cbDockW->setWidget(cbContainer);
    m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_cbDockW);
    m_cbDockW->closeDockWidget();  // 默认隐藏

    // ── 关卡蓝图（与视口同区域 Tab，默认隐藏）──────────────────────
    m_blueprintEditor = new BlueprintEditor();
    connect(m_blueprintEditor, &BlueprintEditor::documentModified, this, [this]() {
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });
    m_bpDockW = new ads::CDockWidget("关卡蓝图");
    m_bpDockW->setWidget(m_blueprintEditor);
    m_dockManager->addDockWidget(
        ads::CenterDockWidgetArea, m_bpDockW, centralArea);
    m_bpDockW->closeDockWidget();  // 默认隐藏

    // 切换到关卡蓝图时，加载当前关卡
    connect(m_bpDockW, &ads::CDockWidget::viewToggled,
            this, [this](bool open) {
        if (!open || !m_blueprintEditor) return;
        const int idx = m_docTabBar->currentIndex();
        const QString path = idx >= 0 ? m_docTabBar->tabData(idx).toString() : QString{};
        m_blueprintEditor->loadLevel(m_openLevels.value(path, nullptr));
    });

    // ── 布局管理器（初始布局建好后捕获默认状态）────────────────────
    m_layoutManager = new LayoutManager(m_dockManager, m_project.path, this);
    QTimer::singleShot(0, this, [this]() {
        m_layoutManager->captureDefault();
    });
}

// ── 5. 窗口菜单（setupCentralArea 之后调用）─────────────────────────
void EditorWindow::setupWindowMenu() {
    m_windowMenu->addAction(m_outlineDockW->toggleViewAction());
    m_windowMenu->addAction(m_detailsDockW->toggleViewAction());
    m_windowMenu->addAction(m_cbDockW->toggleViewAction());
    m_windowMenu->addAction(m_bpDockW->toggleViewAction());
    m_windowMenu->addSeparator();

    m_layoutMenu = m_windowMenu->addMenu("布局");
    rebuildLayoutMenu();

    connect(m_layoutManager, &LayoutManager::layoutListChanged,
            this, &EditorWindow::rebuildLayoutMenu);
    connect(m_layoutManager, &LayoutManager::currentLayoutChanged,
            this, [this](const QString&) { rebuildLayoutMenu(); });
}

void EditorWindow::rebuildLayoutMenu() {
    m_layoutMenu->clear();

    auto* saveAct = m_layoutMenu->addAction("保存当前布局…");
    connect(saveAct, &QAction::triggered, this, [this]() {
        bool ok;
        const QString name = QInputDialog::getText(
            this, "保存布局", "布局名称：",
            QLineEdit::Normal, m_layoutManager->currentLayout(), &ok);
        if (ok && !name.trimmed().isEmpty())
            m_layoutManager->saveLayout(name.trimmed());
    });

    m_layoutMenu->addSeparator();

    const QString current = m_layoutManager->currentLayout();
    for (const QString& name : m_layoutManager->layoutNames()) {
        auto* act = m_layoutMenu->addAction(name);
        act->setCheckable(true);
        act->setChecked(name == current);
        connect(act, &QAction::triggered, this, [this, name]() {
            m_layoutManager->loadLayout(name);
        });
    }

    m_layoutMenu->addSeparator();
    auto* resetAct = m_layoutMenu->addAction("重置为默认布局");
    connect(resetAct, &QAction::triggered, this, [this]() {
        m_layoutManager->resetDefault();
    });
}

// ── 辅助：视口次级工具栏 ─────────────────────────────────────────────
QWidget* EditorWindow::buildViewportToolBar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    bar->setObjectName("viewportToolBar");
    bar->setFixedHeight(28);
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(6, 2, 6, 2);
    hl->setSpacing(2);

    auto vBtn = [&](const QString& t, const QString& tip = "") {
        auto* b = new QToolButton(bar);
        b->setText(t); b->setToolTip(tip);
        b->setObjectName("vpTBBtn");
        hl->addWidget(b);
        return b;
    };
    auto sep = [&]() {
        auto* f = new QFrame(bar);
        f->setFrameShape(QFrame::VLine);
        f->setObjectName("vpSep");
        hl->addSpacing(4); hl->addWidget(f); hl->addSpacing(4);
    };
    auto drop = [&](const QString& t) {
        auto* b = new QPushButton(t, bar);
        b->setObjectName("vpDropBtn");
        hl->addWidget(b);
    };

    m_toolBtnGroup = new QButtonGroup(this);
    m_toolBtnGroup->setExclusive(true);

    auto* btnSelect = vBtn("↖", "选择");  btnSelect->setCheckable(true); btnSelect->setChecked(true);
    auto* btnMove   = vBtn("⊕", "移动");  btnMove->setCheckable(true);
    auto* btnRotate = vBtn("↻", "旋转");  btnRotate->setCheckable(true);
    auto* btnScale  = vBtn("⤢", "缩放");  btnScale->setCheckable(true);

    m_toolBtnGroup->addButton(btnSelect, 0);
    m_toolBtnGroup->addButton(btnMove,   1);
    m_toolBtnGroup->addButton(btnRotate, 2);
    m_toolBtnGroup->addButton(btnScale,  3);

    connect(m_toolBtnGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (!m_viewport) return;
        static const Viewport2D::ToolMode modes[] = {
            Viewport2D::ToolMode::Select,
            Viewport2D::ToolMode::Move,
            Viewport2D::ToolMode::Rotate,
            Viewport2D::ToolMode::Scale
        };
        m_viewport->setToolMode(modes[id]);
    });

    sep();
    drop("2D 正交  ▾");
    sep();
    vBtn("⊠", "显示选项"); vBtn("⚙", "视口设置");
    sep();
    auto* bpBtn = vBtn("关卡蓝图", "打开关卡蓝图（可视化脚本）");
    connect(bpBtn, &QToolButton::clicked, this, [this]() {
        if (m_bpDockW) m_bpDockW->toggleView();
    });
    hl->addStretch();
    auto* zl = new QLabel("1×", bar); zl->setObjectName("vpZoomLabel");
    hl->addWidget(zl);
    sep();
    vBtn("☀", "光照");

    return bar;
}

// ── 6. 底部状态栏（使用 Qt QDockWidget，状态栏不需要 ADS 功能）───────
void EditorWindow::setupBottomBar() {
    auto* dock = new QDockWidget(this);
    dock->setObjectName("bottomStatusDock");
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setAllowedAreas(Qt::BottomDockWidgetArea);

    auto* bar = new QWidget(dock);
    bar->setObjectName("editorStatusBar");
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(2);

    auto* cbToggle = new QPushButton("内容浏览器", bar);
    cbToggle->setObjectName("statusBtn");
    cbToggle->setCheckable(true);
    hl->addWidget(cbToggle);

    // 按钮 → 切换 ADS 面板
    connect(cbToggle, &QPushButton::toggled, this, [this](bool on) {
        if (m_cbDockW) m_cbDockW->toggleView(on);
    });
    // ADS 面板（含 × 关闭）→ 同步按钮状态
    connect(m_cbDockW, &ads::CDockWidget::viewToggled,
            cbToggle, &QPushButton::setChecked);

    auto* logBtn = new QPushButton("输出日志", bar);
    logBtn->setObjectName("statusBtn");
    hl->addWidget(logBtn);

    auto* sf = new QFrame(bar);
    sf->setFrameShape(QFrame::VLine);
    sf->setObjectName("statusSep");
    hl->addSpacing(4); hl->addWidget(sf); hl->addSpacing(4);

    auto* cl = new QLabel("Cmd ▶", bar); cl->setObjectName("cmdLabel");
    auto* ce = new QLineEdit(bar);
    ce->setObjectName("cmdEdit");
    ce->setPlaceholderText("输入控制台命令…");
    ce->setFixedWidth(220);
    hl->addWidget(cl); hl->addWidget(ce);
    hl->addStretch();

    m_saveLabel = new QLabel("已保存", bar);
    m_saveLabel->setObjectName("saveLabel");
    hl->addWidget(m_saveLabel);

    auto* versionBtn = new QPushButton("版本控制  ▾", bar);
    versionBtn->setObjectName("statusBtn");
    hl->addWidget(versionBtn);

    dock->setWidget(bar);
    dock->setFixedHeight(30);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

// ── Tab 切换 / 关闭 ───────────────────────────────────────────────────
void EditorWindow::onTabChanged(int index) {
    if (m_runtime) stopRuntime();
    if (!m_sceneOutliner || !m_detailsPanel) return;

    for (auto& conn : m_tabConnections) disconnect(conn);
    m_tabConnections.clear();

    const QString path = m_docTabBar->tabData(index).toString();

    if (path.isEmpty()) {
        m_sceneOutliner->clear();
        m_detailsPanel->clearActor();
        if (m_viewport) m_viewport->loadLevel(nullptr);
        return;
    }

    if (!m_openLevels.contains(path)) {
        auto* doc = new LevelDocument();
        doc->load(path);
        m_openLevels[path] = doc;
    }
    LevelDocument* doc = m_openLevels[path];

    m_sceneOutliner->loadLevel(doc);
    if (m_viewport) m_viewport->loadLevel(doc);

    // 若关卡蓝图面板可见，同步加载
    if (m_bpDockW && !m_bpDockW->isClosed() && m_blueprintEditor)
        m_blueprintEditor->loadLevel(doc);

    m_detailsPanel->clearActor();

    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::actorSelected,
                                this, [this](const ActorData& a) {
        m_detailsPanel->showActor(a);
        if (m_viewport) m_viewport->setSelectedId(a.id);
    });

    if (m_viewport) {
        m_tabConnections << connect(m_viewport, &Viewport2D::actorSelected,
                                    this, [this](const ActorData& a) {
            m_detailsPanel->showActor(a);
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::actorDragging,
                                    m_detailsPanel, &DetailsPanel::showActor);
        m_tabConnections << connect(m_viewport, &Viewport2D::actorTransformed,
                                    this, [this](const ActorData& a) {
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::actorCreated,
                                    this, [this, doc](const ActorData& a) {
            m_sceneOutliner->loadLevel(doc);
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
        });
    }

    m_tabConnections << connect(m_detailsPanel, &DetailsPanel::actorModified,
                                this, [this, doc](const ActorData& a) {
        doc->updateActor(a);
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
        if (m_viewport) m_viewport->update();
        m_sceneOutliner->loadLevel(doc);
    });

    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::actorRemoved,
                                this, [this](const QString&) {
        m_detailsPanel->clearActor();
        if (m_viewport) m_viewport->setSelectedId({});
    });

    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::levelChanged,
                                this, [this]() {
        if (m_viewport) m_viewport->update();
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });
}

void EditorWindow::onTabClosed(int index) {
    const QString path = m_docTabBar->tabData(index).toString();
    LevelDocument* doc = m_openLevels.value(path);

    if (doc && doc->isDirty()) {
        const QString name = QFileInfo(path).baseName();
        const auto ret = QMessageBox::question(this, "未保存的更改",
            QString("关卡「%1」有未保存的更改，是否保存？").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) return;
        if (ret == QMessageBox::Save) doc->save();
    }

    if (!path.isEmpty())
        delete m_openLevels.take(path);

    for (auto& conn : m_tabConnections) disconnect(conn);
    m_tabConnections.clear();

    m_docTabBar->removeTab(index);

    if (m_docTabBar->count() == 0) {
        m_sceneOutliner->clear();
        m_detailsPanel->clearActor();
        if (m_viewport) m_viewport->loadLevel(nullptr);
    }
    updateSaveLabel();
}

void EditorWindow::openLevelTab(const QString& path) {
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == path) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(path).baseName());
        m_docTabBar->setTabData(idx, path);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}

void EditorWindow::closeEvent(QCloseEvent* e) {
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        const QString path = m_docTabBar->tabData(i).toString();
        LevelDocument* doc = m_openLevels.value(path);
        if (doc && doc->isDirty()) {
            const QString name = QFileInfo(path).baseName();
            const auto ret = QMessageBox::question(this, "未保存的更改",
                QString("关卡「%1」有未保存的更改，是否保存？").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (ret == QMessageBox::Cancel) { e->ignore(); return; }
            if (ret == QMessageBox::Save) doc->save();
        }
    }
    qDeleteAll(m_openLevels);
    m_openLevels.clear();
    emit editorClosed();
    e->accept();
}

void EditorWindow::saveCurrentLevel() {
    const int index = m_docTabBar->currentIndex();
    if (index < 0) return;
    const QString path = m_docTabBar->tabData(index).toString();
    LevelDocument* doc = m_openLevels.value(path);
    if (doc && doc->isDirty()) {
        doc->save();
        updateTabTitle(index);
        updateSaveLabel();
    }
}

void EditorWindow::saveAllLevels() {
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        const QString path = m_docTabBar->tabData(i).toString();
        LevelDocument* doc = m_openLevels.value(path);
        if (doc && doc->isDirty()) {
            doc->save();
            updateTabTitle(i);
        }
    }
    updateSaveLabel();
}

void EditorWindow::updateTabTitle(int index) {
    if (index < 0 || index >= m_docTabBar->count()) return;
    const QString path = m_docTabBar->tabData(index).toString();
    const QString baseName = QFileInfo(path).baseName();
    LevelDocument* doc = m_openLevels.value(path);
    const bool dirty = doc && doc->isDirty();
    m_docTabBar->setTabText(index, dirty ? "● " + baseName : "  " + baseName);
}

void EditorWindow::updateSaveLabel() {
    if (!m_saveLabel) return;
    int dirtyCount = 0;
    for (LevelDocument* doc : m_openLevels)
        if (doc->isDirty()) ++dirtyCount;
    m_saveLabel->setText(dirtyCount == 0 ? "已保存" : QString("%1 未保存").arg(dirtyCount));
}

void EditorWindow::onProjectSettings() {
    auto* dlg = new ProjectSettingsDialog(m_project, this);
    dlg->exec();
    dlg->deleteLater();
}

void EditorWindow::startRuntime() {
    if (m_runtime) return;
    const int index = m_docTabBar->currentIndex();
    if (index < 0) return;
    const QString path = m_docTabBar->tabData(index).toString();
    LevelDocument* doc = m_openLevels.value(path);
    if (!doc || !m_viewport) return;

    m_runtime = new BPRuntime(doc, this);
    connect(m_runtime, &BPRuntime::stateChanged, this, [this]() {
        if (!m_runtime || !m_viewport) return;
        m_viewport->updateRuntimeActors(m_runtime->actors());
        m_viewport->syncPrintLog(m_runtime->printLog());
    });
    connect(m_viewport, &Viewport2D::keyPressed, m_runtime, &BPRuntime::triggerKeyDown);

    m_viewport->setRuntimeMode(true, m_runtime->actors());
    m_runtime->triggerBeginPlay();
    m_viewport->updateRuntimeActors(m_runtime->actors());
    m_viewport->syncPrintLog(m_runtime->printLog());

    if (m_runBtn)  m_runBtn->setEnabled(false);
    if (m_stopBtn) m_stopBtn->setEnabled(true);
}

void EditorWindow::stopRuntime() {
    if (!m_runtime) return;
    disconnect(m_runtime, nullptr, this, nullptr);
    if (m_viewport) disconnect(m_viewport, &Viewport2D::keyPressed, m_runtime, nullptr);
    delete m_runtime;
    m_runtime = nullptr;

    if (m_viewport) {
        m_viewport->setRuntimeMode(false);
        m_viewport->clearPrintLog();
    }
    if (m_runBtn)  m_runBtn->setEnabled(true);
    if (m_stopBtn) m_stopBtn->setEnabled(false);
}
```

- [ ] **Step 3: 编译**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3
cd /Users/kwy/Documents/2Dyinqing/launcher/build
cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E "error:|warning:|Linking|100%"
```

预期：无编译错误，`Linking CXX executable` 成功。

- [ ] **Step 4: 运行验证基本布局**

```bash
open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

验证以下行为：
- 编辑器正常启动，大纲在右上、细节在右下
- 内容浏览器默认不显示，状态栏按钮可以切换显示/隐藏
- "窗口"菜单中有大纲/细节/内容浏览器/关卡蓝图四个可勾选项
- 视口工具栏"关卡蓝图"按钮点击后蓝图面板作为 Tab 出现在视口旁
- 面板可以拖拽停靠、浮动、合并为 Tab

- [ ] **Step 5: Commit**

```bash
git add launcher/src/editor/EditorWindow.h \
        launcher/src/editor/EditorWindow.cpp
git commit -m "feat: 用 ADS 重构 EditorWindow 停靠系统，移除旧浮动面板逻辑"
```

---

## Task 4: QSS — ADS 样式

**Files:**
- Modify: `launcher/resources/styles/launcher.qss`

- [ ] **Step 1: 在 launcher.qss 末尾追加以下 ADS 样式**

```css
/* ── ADS Docking System ───────────────────────────────────────────── */

/* 停靠区标题栏 */
ads--CDockAreaTitleBar {
    background-color: #252525;
    border-bottom: 1px solid #2a2a2a;
    padding: 0 4px;
    min-height: 24px;
}

/* 停靠区 Tab */
ads--CDockWidgetTab {
    background-color: #252525;
    border: none;
    padding: 0 10px;
    min-width: 60px;
    min-height: 24px;
    color: #888;
}
ads--CDockWidgetTab[activeTab="true"] {
    background-color: #1a1a1a;
    color: #ddd;
    border-top: 2px solid #0078d7;
}
ads--CDockWidgetTab:hover {
    background-color: #2a2a2a;
    color: #ccc;
}

/* Tab 关闭按钮 */
ads--CDockWidgetTab > QAbstractButton {
    background: transparent;
    border: none;
    color: #666;
    padding: 0 2px;
}
ads--CDockWidgetTab > QAbstractButton:hover {
    color: #ccc;
}

/* 停靠区容器 */
ads--CDockAreaWidget {
    background-color: #1a1a1a;
    border: none;
}

/* 分割线 */
ads--CDockSplitter::handle {
    background-color: #2a2a2a;
}
ads--CDockSplitter::handle:horizontal {
    width: 3px;
}
ads--CDockSplitter::handle:vertical {
    height: 3px;
}

/* 浮动窗口 */
ads--CFloatingDockContainer {
    background-color: #1a1a1a;
    border: 1px solid #3a3a3a;
}
ads--CFloatingDockContainer QLabel#floatingTitleLabel {
    color: #ccc;
    padding: 0 8px;
}

/* 拖拽目标高亮覆盖层 */
ads--CDockOverlay {
    background-color: rgba(0, 120, 215, 0.15);
    border: 2px solid #0078d7;
}
ads--CDockOverlayCross > * {
    background-color: #0078d7;
}
```

- [ ] **Step 2: 编译 + 运行，验证样式**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3
cd /Users/kwy/Documents/2Dyinqing/launcher/build
cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

验证以下外观：
- 停靠 Tab 颜色与现有深色主题一致（背景 `#252525`，激活时顶部蓝线）
- 拖拽时出现蓝色半透明吸附预览
- 分割线细而不突兀（3px `#2a2a2a`）
- 浮动窗口有深色边框

- [ ] **Step 3: Commit**

```bash
git add launcher/resources/styles/launcher.qss
git commit -m "style: 为 ADS 停靠系统添加深色主题 QSS 样式"
```

---

## 完成检查清单

- [ ] 大纲、细节、内容浏览器、关卡蓝图均可拖拽停靠/浮动/合并 Tab
- [ ] 拖拽时显示蓝色吸附提示
- [ ] 内容浏览器状态栏按钮与 `CDockWidget::viewToggled` 信号双向同步
- [ ] 关卡蓝图切换时正确加载当前关卡文档
- [ ] "窗口→布局"子菜单：保存、切换、重置均正常
- [ ] 布局持久化：重启编辑器后恢复上次布局
- [ ] 关闭编辑器时未保存提示正常弹出
- [ ] 运行/停止按钮状态正常
