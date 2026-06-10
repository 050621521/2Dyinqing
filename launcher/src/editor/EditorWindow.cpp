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
    m_cbDockW->closeDockWidget();

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
    m_bpDockW->closeDockWidget();

    connect(m_bpDockW, &ads::CDockWidget::viewToggled,
            this, [this](bool open) {
        if (!open || !m_blueprintEditor) return;
        const int idx = m_docTabBar->currentIndex();
        const QString path = idx >= 0 ? m_docTabBar->tabData(idx).toString() : QString{};
        m_blueprintEditor->loadLevel(m_openLevels.value(path, nullptr));
    });

    // ── 布局管理器 ────────────────────────────────────────────────────
    m_layoutManager = new LayoutManager(m_dockManager, m_project.path, this);
    QTimer::singleShot(0, this, [this]() {
        m_layoutManager->captureDefault();
    });
}

// ── 5. 窗口菜单 ──────────────────────────────────────────────────────
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

// ── 6. 底部状态栏 ─────────────────────────────────────────────────────
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

    connect(cbToggle, &QPushButton::toggled, this, [this](bool on) {
        if (m_cbDockW) m_cbDockW->toggleView(on);
    });
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
