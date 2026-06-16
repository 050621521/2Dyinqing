#include "EditorWindow.h"
#include "Viewport2D.h"
#include "GameViewport.h"
#include "BlueprintEditor.h"
#include "BPRuntime.h"
#include "UIRuntime.h"
#include "UIEditor.h"
#include "models/UIDocument.h"
#include "SceneOutliner.h"
#include "DetailsPanel.h"
#include "ContentBrowser.h"
#include "GlobalVarPanel.h"
#include "EnumEditor.h"
#include "LayoutManager.h"
#include "DocTabBar.h"
#include "ProjectSettingsDialog.h"
#include "models/LevelDocument.h"
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>
#include <FloatingDockContainer.h>
#include <QGuiApplication>
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
#include <QDir>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include "UndoCommands.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QTextEdit>

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

    {
        m_ppu = ProjectSettingsDialog::readPixelsPerUnit(m_project.path);
        m_viewport->setPixelsPerUnit(m_ppu);
        m_gameViewport->setPixelsPerUnit(m_ppu);
    }

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &EditorWindow::saveCurrentLevel);

    auto* undoSc = new QShortcut(QKeySequence::Undo, this);
    connect(undoSc, &QShortcut::activated, this, [this]() {
        if (m_activeUndoStack) m_activeUndoStack->undo();
    });
    auto* redoSc1 = new QShortcut(QKeySequence::Redo, this);
    connect(redoSc1, &QShortcut::activated, this, [this]() {
        if (m_activeUndoStack) m_activeUndoStack->redo();
    });

    // ── 全局编辑器快捷键（无论哪个子面板有焦点均生效）──────────────────

    // Q/W/E/R 工具模式切换（仅视口 tab）
    auto* qSc = new QShortcut(QKeySequence("Q"), this);
    connect(qSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Select);
    });
    auto* wSc = new QShortcut(QKeySequence("W"), this);
    connect(wSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Move);
    });
    auto* eSc = new QShortcut(QKeySequence("E"), this);
    connect(eSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Rotate);
    });
    auto* rSc = new QShortcut(QKeySequence("R"), this);
    connect(rSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Scale);
    });

    // F 帧居中（视口：聚焦选中；蓝图：居中所有节点）
    // ApplicationShortcut：浮动蓝图窗口聚焦时也能触发，路由由 activeBpEditor 决定
    auto* fSc = new QShortcut(QKeySequence("F"), this);
    fSc->setContext(Qt::ApplicationShortcut);
    connect(fSc, &QShortcut::activated, this, [this]() {
        if (auto* be = activeBpEditor()) { be->frameAll(); return; }
        if (m_centralStack->currentIndex() == 0 && m_viewport) m_viewport->frameSelected();
    });

    // Escape 取消选中（视口；蓝图内的弹窗关闭由 BlueprintEditor 内部处理）
    auto* escSc = new QShortcut(Qt::Key_Escape, this);
    connect(escSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->clearSelection();
    });

    // Delete / Backspace 删除（按聚焦/当前 tab 路由）
    auto deleteAction = [this]() {
        if (isTextInputFocused()) return;
        if (auto* be = activeBpEditor()) { be->deleteSelected(); return; }
        if (m_centralStack->currentIndex() == 0 && m_viewport) m_viewport->deleteSelected();
        else if (m_centralStack->currentWidget() == m_uiEditor) m_uiEditor->onDeleteSelected();
    };
    auto* delSc = new QShortcut(QKeySequence::Delete, this);
    delSc->setContext(Qt::ApplicationShortcut);
    connect(delSc, &QShortcut::activated, this, deleteAction);
    auto* bsSc = new QShortcut(Qt::Key_Backspace, this);
    bsSc->setContext(Qt::ApplicationShortcut);
    connect(bsSc, &QShortcut::activated, this, deleteAction);

    // Ctrl+A 全选
    auto* selectAllSc = new QShortcut(QKeySequence::SelectAll, this);
    connect(selectAllSc, &QShortcut::activated, this, [this]() {
        const int idx = m_centralStack->currentIndex();
        if (idx == 0 && m_viewport) m_viewport->selectAll();
    });

    // Ctrl+D 原位复制
    auto* dupSc = new QShortcut(QKeySequence("Ctrl+D"), this);
    dupSc->setContext(Qt::ApplicationShortcut);
    connect(dupSc, &QShortcut::activated, this, [this]() {
        if (isTextInputFocused()) return;
        if (auto* be = activeBpEditor()) { be->duplicateSelectedNode(); return; }
        if (m_centralStack->currentIndex() == 0 && m_viewport) m_viewport->duplicateSelected();
        else if (m_centralStack->currentWidget() == m_uiEditor) m_uiEditor->duplicateSelected();
    });

    // Ctrl+C 复制
    auto* copySc = new QShortcut(QKeySequence::Copy, this);
    connect(copySc, &QShortcut::activated, this, [this]() {
        if (isTextInputFocused()) return;
        const int idx = m_centralStack->currentIndex();
        if (idx == 3 && m_uiEditor) m_uiEditor->copySelected();
    });

    // Ctrl+V 粘贴
    auto* pasteSc = new QShortcut(QKeySequence::Paste, this);
    connect(pasteSc, &QShortcut::activated, this, [this]() {
        if (isTextInputFocused()) return;
        const int idx = m_centralStack->currentIndex();
        if (idx == 3 && m_uiEditor) m_uiEditor->paste();
    });

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

    auto* tabBar = new DocTabBar(tb);
    tabBar->setObjectName("docTabBar");
    tabBar->setTabsClosable(true);
    tabBar->setExpanding(false);
    tabBar->setElideMode(Qt::ElideNone);   // 不截断，标签按文字自适应宽度
    tabBar->setUsesScrollButtons(true);    // 放不下时出现左右滚动按钮
    tabBar->setMovable(true);              // 允许拖动排序
    m_docTabBar = tabBar;
    tb->addWidget(tabBar);
    addToolBar(Qt::TopToolBarArea, tb);

    connect(tabBar, &QTabBar::currentChanged,    this, &EditorWindow::onTabChanged);
    connect(tabBar, &QTabBar::tabCloseRequested, this, &EditorWindow::onTabClosed);
    connect(tabBar, &DocTabBar::blueprintDraggedOut,
            this, &EditorWindow::floatBp);
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
    m_pauseBtn = tbBtn("⏸", "暂停");
    m_pauseBtn->setCheckable(true);
    m_stopBtn = tbBtn("⏹",  "停止");
    tbBtn("⏭",  "跳帧");
    tb->addSeparator();
    connect(m_runBtn,   &QToolButton::clicked, this, [this]() {
        m_levelNavStack.clear();   // 全新运行会话：清空关卡历史栈（跳转内部的重启不清）
        m_globalVars.clear();      // 全新一局：清空全局变量
        startRuntime();
    });
    connect(m_pauseBtn, &QToolButton::clicked, this, &EditorWindow::togglePauseRuntime);
    connect(m_stopBtn,  &QToolButton::clicked, this, &EditorWindow::stopRuntime);
    m_pauseBtn->setEnabled(false);
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
    m_dockManager->setStyleSheet("");  // 清除 ADS 内置浅色主题，让 qApp 深色样式表生效

    // ── 视口（中央固定，不可关闭/浮动/移动）─────────────────────────
    auto* leftWrap = new QWidget();
    leftWrap->setObjectName("viewportWrap");
    auto* leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);
    leftLay->addWidget(buildViewportToolBar(leftWrap));
    m_viewport = new Viewport2D(leftWrap);
    leftLay->addWidget(m_viewport, 1);
    m_viewportPage = leftWrap;

    // 蓝图编辑器实例按需创建（见 ensureBpInstance），不再有单例编辑器。

    m_centralStack = new QStackedWidget();
    m_centralStack->addWidget(m_viewportPage);  // index 0；蓝图实例页按需动态加入

    // ── 游戏视图页面 ──────────────────────────────────────────────────────
    auto* gvWrap = new QWidget();
    gvWrap->setObjectName("gameViewWrap");
    auto* gvLay = new QVBoxLayout(gvWrap);
    gvLay->setContentsMargins(0, 0, 0, 0);
    gvLay->setSpacing(0);
    gvLay->addWidget(buildGameViewToolBar(gvWrap));
    m_gameViewport = new GameViewport(gvWrap);
    gvLay->addWidget(m_gameViewport, 1);
    m_gameViewPage = gvWrap;
    m_centralStack->addWidget(m_gameViewPage);  // index 2

    m_uiEditor = new UIEditor(this);
    m_uiEditor->setProjectRoot(m_project.path);
    m_centralStack->addWidget(m_uiEditor);  // index 3

    m_enumEditor = new EnumEditor(this);    // index 4：枚举编辑页
    m_centralStack->addWidget(m_enumEditor);
    connect(m_enumEditor, &EnumEditor::changed, this, &EditorWindow::reloadGlobalVarDefs);

    connect(m_uiEditor, &UIEditor::documentModified, this, [this]() {
        const int cur = m_docTabBar->currentIndex();
        if (cur < 0) return;
        const QString path = m_docTabBar->tabData(cur).toString();
        if (!m_openUIDocs.contains(path)) return;
        const bool dirty = m_openUIDocs[path]->isDirty();
        const QString base = QFileInfo(path).baseName();
        m_docTabBar->setTabText(cur, dirty ? "● " + base : "  " + base);
        updateSaveLabel();
    });

    connect(m_uiEditor, &UIEditor::previewLevelChanged, this, [this](const QString& name) {
        LevelDocument* doc = nullptr;
        if (name != "关闭") {
            for (auto it = m_openLevels.begin(); it != m_openLevels.end(); ++it) {
                if (QFileInfo(it.key()).baseName() == name) { doc = it.value(); break; }
            }
        }
        m_uiEditor->setPreviewLevel(doc, m_ppu);
    });

    // 在 DocTabBar 末尾固定添加「游戏视图」Tab（不可关闭）
    {
        QSignalBlocker b(m_docTabBar);
        const int gvIdx = m_docTabBar->addTab("  游戏视图");
        m_docTabBar->setTabData(gvIdx, DocTabBar::kGameViewTabData);
        m_docTabBar->setTabButton(gvIdx, QTabBar::RightSide, nullptr);
        m_docTabBar->setTabButton(gvIdx, QTabBar::LeftSide,  nullptr);
        updateTabTooltip(gvIdx);
    }

    m_viewportDock = new ads::CDockWidget("视口");
    m_viewportDock->setWidget(m_centralStack);
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
    connect(m_detailsPanel, &DetailsPanel::editBpClassRequested,
            this, [this](const QString& bpClass) { openBpClassTab(bpClass); });
    m_detailsDockW = new ads::CDockWidget("细节");
    m_detailsDockW->setWidget(m_detailsPanel);
    m_dockManager->addDockWidget(
        ads::BottomDockWidgetArea, m_detailsDockW, rightArea);

    // ── 内容浏览器（底部，默认隐藏）─────────────────────────────────
    auto* cbContainer = new QWidget();
    auto* cbLay = new QVBoxLayout(cbContainer);
    cbLay->setContentsMargins(0, 0, 0, 0);
    cbLay->setSpacing(0);
    m_contentBrowser = new ContentBrowser(m_project.path, cbContainer);
    auto* cb = m_contentBrowser;
    connect(cb, &ContentBrowser::levelOpenRequested,
            this, [this](const QString& path) { openLevelTab(path); });
    connect(cb, &ContentBrowser::bpClassOpenRequested,
            this, [this](const QString& path) { openBpClassTab(path); });
    connect(cb, &ContentBrowser::uiDocOpenRequested,
            this, [this](const QString& path) { openUIDocTab(path); });
    connect(cb, &ContentBrowser::enumOpenRequested,
            this, [this](const QString& path) { openEnumTab(path); });
    connect(cb, &ContentBrowser::enumFileDeleted, this, [this]() { reloadGlobalVarDefs(); });
    connect(cb, &ContentBrowser::enumFileRenamed, this,
            [this](const QString& oldN, const QString& newN) {
        const QString oldT = "enum:" + oldN, newT = "enum:" + newN;
        QList<GlobalVarDef> vars = GlobalVars::load(m_project.path);
        for (GlobalVarDef& v : vars) if (v.type == oldT) v.type = newT;
        GlobalVars::save(m_project.path, vars);
        for (LevelDocument* doc : m_openLevels.values())
            if (doc)
                for (const BPNode& n : doc->bpNodes())
                    if (n.type == "Flow.Switch" && n.params.value("enum") == oldN) {
                        BPNode nn = n; nn.params["enum"] = newN; doc->updateBPNode(nn);
                    }
        for (BPClass* bc : m_openBpClasses.values())
            if (bc)
                for (BPNode& n : bc->nodes)
                    if (n.type == "Flow.Switch" && n.params.value("enum") == oldN)
                        n.params["enum"] = newN;
        reloadGlobalVarDefs();
        if (m_globalVarPanel) m_globalVarPanel->setProjectRoot(m_project.path);
    });
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

    // ── 全局变量面板 ──────────────────────────────────────────────────────
    m_globalVarDefs = GlobalVars::load(m_project.path);
    m_enumDefs      = Enums::loadAll(m_project.path);
    m_globalVarPanel = new GlobalVarPanel();
    m_globalVarPanel->setProjectRoot(m_project.path);
    connect(m_globalVarPanel, &GlobalVarPanel::changed,
            this, &EditorWindow::reloadGlobalVarDefs);
    // 改名同步：已打开的关卡/蓝图里引用该变量的节点跟着改
    connect(m_globalVarPanel, &GlobalVarPanel::varRenamed, this,
            [this](const QString& oldN, const QString& newN) {
        auto isGlobalRef = [&](const BPNode& n) {
            return (n.type == "Global.Get" || n.type == "Global.Set")
                && n.params.value("varName") == oldN;
        };
        for (LevelDocument* doc : m_openLevels.values())
            if (doc)
                for (const BPNode& n : doc->bpNodes())
                    if (isGlobalRef(n)) { BPNode nn = n; nn.params["varName"] = newN; doc->updateBPNode(nn); }
        for (BPClass* bc : m_openBpClasses.values())
            if (bc)
                for (BPNode& n : bc->nodes)
                    if (isGlobalRef(n)) n.params["varName"] = newN;
        for (auto it = m_bpInstances.begin(); it != m_bpInstances.end(); ++it)
            if (it.value().editor) it.value().editor->update();
    });
    auto* gvDock = new ads::CDockWidget("全局变量");
    gvDock->setWidget(m_globalVarPanel);
    // 停靠在画布左侧，像虚幻"我的蓝图"常驻可见
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, gvDock);
    if (m_windowMenu) m_windowMenu->addAction(gvDock->toggleViewAction());

    // ── 蓝图浮动窗口：每个实例按需创建独立 Dock（见 floatBp）──────────────

    // ── 拖回 Tab 栏检测定时器（遍历所有浮动蓝图实例）────────────────────
    m_bpDropCheckTimer = new QTimer(this);
    m_bpDropCheckTimer->setInterval(50);
    connect(m_bpDropCheckTimer, &QTimer::timeout, this, [this]() {
        // 收集仍在浮动的蓝图实例
        QStringList floating;
        for (auto it = m_bpInstances.begin(); it != m_bpInstances.end(); ++it)
            if (it.value().dock && it.value().dock->isFloating())
                floating << it.key();
        if (floating.isEmpty()) { m_bpDropCheckTimer->stop(); return; }

        // 等待 floatBp 触发的那次初始鼠标释放，避免立即误嵌
        if (!m_bpDropFirstDone) {
            if (QGuiApplication::mouseButtons() == Qt::NoButton)
                m_bpDropFirstDone = true;
            return;
        }

        auto* tb = findChild<QToolBar*>("docTabToolBar");
        if (!tb) return;
        const QRect tbGlobal(tb->mapToGlobal(QPoint(0, 0)), tb->size());

        QString hitId;          // 鼠标松开且覆盖 Tab 栏的实例
        bool    anyNear = false;
        for (const QString& id : floating) {
            auto* container = m_bpInstances[id].dock->floatingDockContainer();
            if (!container) continue;
            const QRect cRect = container->frameGeometry();
            // 给 Tab 栏上下各留 20px 容差
            if (cRect.intersects(tbGlobal.adjusted(0, -20, 0, 20))) {
                anyNear = true;
                if (QGuiApplication::mouseButtons() == Qt::NoButton) { hitId = id; break; }
            }
        }

        // 高亮反馈
        if (tb->property("bpDropHighlight").toBool() != anyNear) {
            tb->setProperty("bpDropHighlight", anyNear);
            tb->style()->unpolish(tb);
            tb->style()->polish(tb);
            tb->update();
        }

        // 鼠标已松开且覆盖 Tab 栏 → 嵌回该实例
        if (!hitId.isEmpty()) {
            tb->setProperty("bpDropHighlight", false);
            tb->style()->unpolish(tb);
            tb->style()->polish(tb);
            tb->update();
            QTimer::singleShot(0, this, [this, hitId]() { embedBp(hitId); });
        }
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
    // 同步 Q/W/E/R 快捷键触发的工具切换 → 更新按钮组
    connect(m_viewport, &Viewport2D::toolModeChanged, this, [this](Viewport2D::ToolMode mode) {
        QSignalBlocker b(m_toolBtnGroup);
        if (auto* btn = m_toolBtnGroup->button(static_cast<int>(mode)))
            btn->setChecked(true);
    });

    sep();
    drop("2D 正交  ▾");
    sep();
    vBtn("⊠", "显示选项"); vBtn("⚙", "视口设置");
    sep();
    auto* bpBtn = vBtn("关卡蓝图", "打开关卡蓝图（可视化脚本）");
    connect(bpBtn, &QToolButton::clicked, this, &EditorWindow::openBlueprintTab);

    // ── 网格吸附 ──
    sep();
    auto* snapGridBtn = new QToolButton(bar);
    snapGridBtn->setText("⊞"); snapGridBtn->setToolTip("网格吸附");
    snapGridBtn->setObjectName("vpTBBtn");
    snapGridBtn->setCheckable(true); snapGridBtn->setChecked(true);
    hl->addWidget(snapGridBtn);

    auto* snapGridCombo = new QComboBox(bar);
    snapGridCombo->setObjectName("vpSnapCombo");
    snapGridCombo->addItems({"25", "50", "100", "250"});
    snapGridCombo->setCurrentIndex(1);
    snapGridCombo->setFixedWidth(48);
    hl->addWidget(snapGridCombo);

    // ── 旋转吸附 ──
    auto* snapRotBtn = new QToolButton(bar);
    snapRotBtn->setText("⟳"); snapRotBtn->setToolTip("旋转吸附");
    snapRotBtn->setObjectName("vpTBBtn");
    snapRotBtn->setCheckable(true); snapRotBtn->setChecked(true);
    hl->addWidget(snapRotBtn);

    auto* snapRotCombo = new QComboBox(bar);
    snapRotCombo->setObjectName("vpSnapCombo");
    snapRotCombo->addItems({"5°", "15°", "45°", "90°"});
    snapRotCombo->setCurrentIndex(1);
    snapRotCombo->setFixedWidth(44);
    hl->addWidget(snapRotCombo);

    connect(snapGridBtn, &QToolButton::toggled, this, [this, snapGridCombo](bool on) {
        snapGridCombo->setEnabled(on);
        if (m_viewport) {
            static const float sizes[] = {25, 50, 100, 250};
            m_viewport->setGridSnap(on, sizes[snapGridCombo->currentIndex()]);
        }
    });
    connect(snapGridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, snapGridBtn](int idx) {
        if (m_viewport && snapGridBtn->isChecked()) {
            static const float sizes[] = {25, 50, 100, 250};
            m_viewport->setGridSnap(true, sizes[idx]);
        }
    });
    connect(snapRotBtn, &QToolButton::toggled, this, [this, snapRotCombo](bool on) {
        snapRotCombo->setEnabled(on);
        if (m_viewport) {
            static const float angles[] = {5, 15, 45, 90};
            m_viewport->setRotSnap(on, angles[snapRotCombo->currentIndex()]);
        }
    });
    connect(snapRotCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, snapRotBtn](int idx) {
        if (m_viewport && snapRotBtn->isChecked()) {
            static const float angles[] = {5, 15, 45, 90};
            m_viewport->setRotSnap(true, angles[idx]);
        }
    });

    // ── 对齐按钮（多选 ≥2 时启用）──
    sep();
    static const struct { const char* icon; const char* tip; int type; } kAlignDefs[] = {
        {"←|", "左对齐",   0}, {"|·|", "水平居中", 2}, {"|→", "右对齐",   1},
        {"↑—", "上对齐",   3}, {"—·—","垂直居中", 5}, {"—↓", "下对齐",   4}
    };
    m_viewportAlignBtns.clear();
    for (const auto& d : kAlignDefs) {
        auto* b = new QToolButton(bar);
        b->setText(QString::fromUtf8(d.icon));
        b->setToolTip(QString::fromUtf8(d.tip));
        b->setObjectName("vpTBBtn");
        b->setEnabled(false);
        const int alignType = d.type;
        connect(b, &QToolButton::clicked, this, [this, alignType]() {
            if (m_viewport) m_viewport->alignSelected(alignType);
        });
        hl->addWidget(b);
        m_viewportAlignBtns << b;
    }

    hl->addStretch();
    auto* zl = new QLabel("1×", bar); zl->setObjectName("vpZoomLabel");
    hl->addWidget(zl);
    sep();
    vBtn("☀", "光照");

    return bar;
}

QWidget* EditorWindow::buildGameViewToolBar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    bar->setObjectName("viewportToolBar");
    bar->setFixedHeight(28);
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(6, 2, 6, 2);
    hl->setSpacing(6);

    m_gvCamNameLabel = new QLabel("摄像机：—", bar);
    m_gvCamNameLabel->setObjectName("vpZoomLabel");
    hl->addWidget(m_gvCamNameLabel);

    auto* sep1 = new QFrame(bar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setObjectName("vpSep");
    hl->addSpacing(4); hl->addWidget(sep1); hl->addSpacing(4);

    m_gvResLabel = new QLabel("—", bar);
    m_gvResLabel->setObjectName("vpZoomLabel");
    hl->addWidget(m_gvResLabel);

    hl->addStretch();

    auto* fsBtn = new QToolButton(bar);
    fsBtn->setText("⛶");
    fsBtn->setToolTip("最大化窗口");
    fsBtn->setObjectName("vpTBBtn");
    hl->addWidget(fsBtn);
    connect(fsBtn, &QToolButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });

    return bar;
}

void EditorWindow::updateGameViewToolbar(LevelDocument* doc) {
    if (!m_gvCamNameLabel || !m_gvResLabel) return;
    if (!doc) {
        m_gvCamNameLabel->setText("摄像机：—");
        m_gvResLabel->setText("—");
        return;
    }
    for (const ActorData& a : doc->actors()) {
        if (a.components.contains("摄像机组件") && a.cameraIsMain) {
            m_gvCamNameLabel->setText("摄像机：" + a.name);
            m_gvResLabel->setText(QString("%1×%2").arg(a.cameraResW).arg(a.cameraResH));
            return;
        }
    }
    m_gvCamNameLabel->setText("摄像机：无");
    m_gvResLabel->setText("—");
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
    const QString path = m_docTabBar->tabData(index).toString();
    if (m_runtime && path != DocTabBar::kGameViewTabData)
        stopRuntime();
    if (!m_sceneOutliner || !m_detailsPanel) return;

    for (auto& conn : m_tabConnections) disconnect(conn);
    m_tabConnections.clear();

    // 蓝图 Tab（关卡蓝图 或 Actor .bp 蓝图）——显示对应实例页
    if (isAnyBlueprintTab(path)) {
        BlueprintEditor* ed = ensureBpInstance(path);
        m_centralStack->setCurrentWidget(ed);
        // 关卡蓝图：上下文关卡跟随，刷新大纲（蓝图需看到对应 Actor）
        if (isLevelBlueprintTab(path)) {
            const QString levelPath = levelPathOfBlueprintTab(path);
            if (m_openLevels.contains(levelPath)) {
                m_activeLevelPath = levelPath;
                m_sceneOutliner->loadLevel(m_openLevels.value(levelPath));
            }
        }
        m_activeUndoStack = ed->bpUndoStack();
        return;
    }

    // 游戏视图 Tab
    if (path == DocTabBar::kGameViewTabData) {
        if (m_centralStack) m_centralStack->setCurrentWidget(m_gameViewPage);
        LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
        if (m_gameViewport) {
            m_gameViewport->loadLevel(doc);
            updateGameViewToolbar(doc);
        }
        m_activeUndoStack = nullptr;
        return;
    }

    // .ui 文件
    if (path.endsWith(".ui")) {
        UIDocument* doc = m_openUIDocs.value(path, nullptr);
        if (!doc) {
            if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);
            return;
        }
        m_uiEditor->loadDocument(doc);
        LevelDocument* previewLevel = m_openLevels.value(m_activeLevelPath, nullptr);
        m_uiEditor->setPreviewLevel(previewLevel, m_ppu);
        // 填充可用关卡名：扫描项目 Levels 目录
        QStringList levelNames;
        const QDir levelsDir(m_project.path + "/Levels");
        for (const QString& f : levelsDir.entryList({"*.level"}, QDir::Files))
            levelNames << QFileInfo(f).baseName();
        const QString activeLevelName = previewLevel ? QFileInfo(m_activeLevelPath).baseName() : QString();
        m_uiEditor->setAvailableLevels(levelNames, activeLevelName);
        m_uiEditor->setUndoStack(doc->undoStack(), nullptr);
        if (m_centralStack) m_centralStack->setCurrentWidget(m_uiEditor);
        m_activeUndoStack = doc->undoStack();
        return;
    }

    // .enum 枚举资产
    if (path.endsWith(".enum")) {
        if (m_enumEditor) m_enumEditor->load(path);
        if (m_centralStack) m_centralStack->setCurrentWidget(m_enumEditor);
        m_activeUndoStack = nullptr;
        return;
    }

    // 切换到视口
    if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);

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
    m_activeLevelPath = path;
    m_activeUndoStack = doc->undoStack();

    // 设置 refresh 回调（outliner + viewport 重建）
    auto levelRefresh = [this, doc]() {
        m_sceneOutliner->loadLevel(doc);
        if (m_viewport) m_viewport->update();
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    };
    if (m_viewport) m_viewport->setUndoStack(doc->undoStack(), levelRefresh);
    m_sceneOutliner->setUndoStack(doc->undoStack(), [this, doc]() {
        m_sceneOutliner->loadLevel(doc);
        if (m_viewport) m_viewport->update();
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });

    m_sceneOutliner->loadLevel(doc);
    if (m_viewport) m_viewport->loadLevel(doc);

    m_detailsPanel->clearActor();
    for (auto* btn : m_viewportAlignBtns) btn->setEnabled(false);

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
        m_tabConnections << connect(m_viewport, &Viewport2D::selectionChanged,
                                    this, [this](QStringList ids) {
            const int n = ids.size();
            for (auto* btn : m_viewportAlignBtns)
                btn->setEnabled(n >= 2);
            if (n == 0)
                m_detailsPanel->clearActor();
            else if (n > 1)
                m_detailsPanel->showMultiSelection(n);
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::actorsAligned,
                                    this, [this](QList<ActorData> actors) {
            LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
            if (!doc) return;
            for (const ActorData& a : actors) doc->updateActor(a);
            if (m_viewport) m_viewport->update();
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
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
        m_tabConnections << connect(m_viewport, &Viewport2D::actorRemoved,
                                    this, [this, doc](const QString&) {
            m_sceneOutliner->loadLevel(doc);
            m_detailsPanel->clearActor();
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
        });
    }

    m_tabConnections << connect(m_detailsPanel, &DetailsPanel::actorModified,
                                this, [this, doc](const ActorData& after) {
        // 找出修改前的状态
        ActorData before;
        for (const ActorData& a : doc->actors())
            if (a.id == after.id) { before = a; break; }
        // 主摄像机互斥（不纳入 undo，属于约束逻辑）
        if (after.components.contains("摄像机组件") && after.cameraIsMain) {
            for (const ActorData& other : doc->actors()) {
                if (other.id != after.id && other.components.contains("摄像机组件")
                        && other.cameraIsMain) {
                    ActorData updated = other;
                    updated.cameraIsMain = false;
                    doc->updateActor(updated);
                }
            }
        }
        auto doRefresh = [this, doc]() {
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
            if (m_viewport) m_viewport->update();
            if (m_gameViewport) { m_gameViewport->update(); updateGameViewToolbar(doc); }
            m_sceneOutliner->loadLevel(doc);
        };
        doc->undoStack()->push(new ActorModifyCmd(doc, before, after, doRefresh));
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

    if (path == DocTabBar::kGameViewTabData) return;

    // 蓝图 Tab（关卡蓝图 或 Actor .bp）——销毁该编辑器实例
    if (isAnyBlueprintTab(path)) {
        // Actor 蓝图：关闭前保存（BPClass 对象本身在 closeEvent 统一释放，
        // 因 ActorBPRuntime 可能仍引用它，此处只销毁 editor 实例）
        if (path.endsWith(".bp")) {
            BlueprintEditor* ed = m_bpInstances.value(path).editor;
            if (ed) ed->saveBpClass();
        }
        destroyBpInstance(path);
        m_docTabBar->removeTab(index);
        if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);
        return;
    }

    // .ui 文件
    if (path.endsWith(".ui")) {
        UIDocument* doc = m_openUIDocs.value(path, nullptr);
        if (doc && doc->isDirty()) {
            const auto ret = QMessageBox::question(this, "保存",
                QString("UI「%1」有未保存的修改，是否保存？").arg(doc->name()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (ret == QMessageBox::Cancel) return;
            if (ret == QMessageBox::Yes) doc->save();
        }
        m_openUIDocs.remove(path);
        delete doc;
        m_docTabBar->removeTab(index);
        if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);
        updateSaveLabel();
        return;
    }

    LevelDocument* doc = m_openLevels.value(path);

    if (doc && doc->isDirty()) {
        const QString name = QFileInfo(path).baseName();
        const auto ret = QMessageBox::question(this, "未保存的更改",
            QString("关卡「%1」有未保存的更改，是否保存？").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) return;
        if (ret == QMessageBox::Save) doc->save();
    }

    // 连带关闭该关卡的关卡蓝图（嵌入 Tab 或浮动窗口），避免悬空 doc
    {
        const QString bpTabId = DocTabBar::kBlueprintTabData + path;
        for (int i = 0; i < m_docTabBar->count(); ++i) {
            if (m_docTabBar->tabData(i).toString() == bpTabId) {
                QSignalBlocker b(m_docTabBar);
                m_docTabBar->removeTab(i);
                break;
            }
        }
        destroyBpInstance(bpTabId);  // 销毁实例（含其浮动 dock）
    }

    if (!path.isEmpty())
        delete m_openLevels.take(path);

    for (auto& conn : m_tabConnections) disconnect(conn);
    m_tabConnections.clear();

    // 蓝图 Tab 可能已被移除导致索引偏移，按 path 重新定位关卡 Tab
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == path) { index = i; break; }
    }
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
        updateTabTooltip(idx);
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
    for (const QString& bpPath : m_dirtyBpClasses) {
        BPClass* bc = m_openBpClasses.value(bpPath);
        if (!bc) continue;
        const QString name = QFileInfo(bpPath).baseName();
        const auto ret = QMessageBox::question(this, "未保存的更改",
            QString("蓝图类「%1」有未保存的更改，是否保存？").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) { e->ignore(); return; }
        if (ret == QMessageBox::Save) bc->save();
    }
    m_dirtyBpClasses.clear();
    qDeleteAll(m_openLevels);
    m_openLevels.clear();
    qDeleteAll(m_openBpClasses);
    m_openBpClasses.clear();
    qDeleteAll(m_openUIDocs);
    m_openUIDocs.clear();
    emit editorClosed();
    e->accept();
}

void EditorWindow::saveCurrentLevel() {
    const int index = m_docTabBar->currentIndex();
    if (index < 0) return;
    const QString path = m_docTabBar->tabData(index).toString();

    if (path.endsWith(".ui")) {
        UIDocument* doc = m_openUIDocs.value(path);
        if (doc && doc->isDirty()) {
            doc->save();
            updateTabTitle(index);
            updateSaveLabel();
        }
        return;
    }

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
    for (auto it = m_openUIDocs.begin(); it != m_openUIDocs.end(); ++it) {
        if (it.value()->isDirty())
            it.value()->save();
    }
    updateSaveLabel();
}

void EditorWindow::updateTabTitle(int index) {
    if (index < 0 || index >= m_docTabBar->count()) return;
    const QString data = m_docTabBar->tabData(index).toString();
    // 关卡蓝图 tab：标题「关卡名 蓝图」，脏标记取所属关卡
    if (isLevelBlueprintTab(data)) {
        const QString levelPath = levelPathOfBlueprintTab(data);
        LevelDocument* doc = m_openLevels.value(levelPath, nullptr);
        const bool dirty = doc && doc->isDirty();
        const QString name = QFileInfo(levelPath).baseName() + " 蓝图";
        m_docTabBar->setTabText(index, dirty ? "● " + name : "  " + name);
        return;
    }
    const QString baseName = QFileInfo(data).baseName();
    LevelDocument* doc = m_openLevels.value(data);
    const bool dirty = doc && doc->isDirty();
    m_docTabBar->setTabText(index, dirty ? "● " + baseName : "  " + baseName);
}

// 根据 tabData 设置标签的悬停提示：特殊 tab 显示中文名，关卡/文档 tab 显示完整路径
void EditorWindow::updateTabTooltip(int index) {
    if (index < 0 || index >= m_docTabBar->count()) return;
    const QString data = m_docTabBar->tabData(index).toString();
    QString tip;
    if (isLevelBlueprintTab(data))
        tip = QFileInfo(levelPathOfBlueprintTab(data)).baseName() + " 蓝图";
    else if (data == DocTabBar::kGameViewTabData)
        tip = "游戏视图";
    else
        tip = data;  // 文件绝对路径（含 .bp / .ui / 关卡）
    m_docTabBar->setTabToolTip(index, tip);
}

// ── 多蓝图实例 helper ─────────────────────────────────────────────────
bool EditorWindow::isLevelBlueprintTab(const QString& tabData) {
    return tabData.startsWith(DocTabBar::kBlueprintTabData);
}
QString EditorWindow::levelPathOfBlueprintTab(const QString& tabData) {
    return tabData.mid(DocTabBar::kBlueprintTabData.size());
}
bool EditorWindow::isAnyBlueprintTab(const QString& tabData) {
    return isLevelBlueprintTab(tabData) || tabData.endsWith(".bp");
}

// 取得或创建某蓝图 tabId 对应的编辑器实例（首次创建时加载数据并入中央 stack）
BlueprintEditor* EditorWindow::ensureBpInstance(const QString& tabId) {
    auto it = m_bpInstances.find(tabId);
    if (it != m_bpInstances.end()) return it.value().editor;

    BpInstance inst;
    inst.isLevelBp = isLevelBlueprintTab(tabId);
    inst.editor = new BlueprintEditor();
    inst.editor->setProjectRoot(m_project.path);
    inst.editor->setGlobalVarDefs(m_globalVarDefs);
    inst.editor->setEnumDefs(m_enumDefs);

    if (inst.isLevelBp) {
        inst.dataPath = levelPathOfBlueprintTab(tabId);
        inst.editor->loadLevel(m_openLevels.value(inst.dataPath, nullptr));
        const QString levelPath = inst.dataPath;
        connect(inst.editor, &BlueprintEditor::documentModified, this, [this, levelPath]() {
            updateSaveLabel();
            const QString bpTabId = DocTabBar::kBlueprintTabData + levelPath;
            for (int i = 0; i < m_docTabBar->count(); ++i) {
                const QString d = m_docTabBar->tabData(i).toString();
                if (d == bpTabId || d == levelPath) updateTabTitle(i);
            }
        });
    } else {
        inst.dataPath = tabId;  // .bp 路径
        inst.editor->loadBpClass(m_openBpClasses.value(tabId, nullptr));
        const QString bpPath = tabId;
        connect(inst.editor, &BlueprintEditor::bpClassModified, this, [this, bpPath]() {
            m_dirtyBpClasses.insert(bpPath);
        });
    }

    m_centralStack->addWidget(inst.editor);
    m_bpInstances.insert(tabId, inst);
    return inst.editor;
}

// 当前应响应快捷键的蓝图编辑器：优先焦点链（浮动窗口），否则中央 stack 当前蓝图页
BlueprintEditor* EditorWindow::activeBpEditor() const {
    QWidget* w = QApplication::focusWidget();
    while (w) {
        if (auto* be = qobject_cast<BlueprintEditor*>(w)) return be;
        w = w->parentWidget();
    }
    QWidget* cur = m_centralStack ? m_centralStack->currentWidget() : nullptr;
    for (const auto& inst : m_bpInstances)
        if (inst.editor == cur) return inst.editor;
    return nullptr;
}

// 销毁某蓝图实例（嵌入或浮动），释放其编辑器
void EditorWindow::destroyBpInstance(const QString& tabId) {
    auto it = m_bpInstances.find(tabId);
    if (it == m_bpInstances.end()) return;
    BpInstance inst = it.value();
    m_bpInstances.erase(it);
    if (inst.dock) {
        inst.dock->setWidget(new QWidget());  // 占位，避免 ADS 持有悬空指针
        inst.dock->closeDockWidget();
        inst.dock->deleteLater();
    } else if (m_centralStack) {
        m_centralStack->removeWidget(inst.editor);
    }
    delete inst.editor;
}

void EditorWindow::updateSaveLabel() {
    if (!m_saveLabel) return;
    int dirtyCount = 0;
    for (LevelDocument* doc : m_openLevels)
        if (doc->isDirty()) ++dirtyCount;
    for (UIDocument* ud : m_openUIDocs)
        if (ud->isDirty()) ++dirtyCount;
    m_saveLabel->setText(dirtyCount == 0 ? "已保存" : QString("%1 未保存").arg(dirtyCount));
}

void EditorWindow::onProjectSettings() {
    auto* dlg = new ProjectSettingsDialog(m_project, this);
    if (dlg->exec() == QDialog::Accepted) {
        m_ppu = ProjectSettingsDialog::readPixelsPerUnit(m_project.path);
        m_viewport->setPixelsPerUnit(m_ppu);
        m_gameViewport->setPixelsPerUnit(m_ppu);
    }
    dlg->deleteLater();
}

void EditorWindow::reloadGlobalVarDefs() {
    m_globalVarDefs = GlobalVars::load(m_project.path);
    m_enumDefs      = Enums::loadAll(m_project.path);
    for (auto it = m_bpInstances.begin(); it != m_bpInstances.end(); ++it)
        if (it.value().editor) {
            it.value().editor->setGlobalVarDefs(m_globalVarDefs);
            it.value().editor->setEnumDefs(m_enumDefs);
        }
    if (m_globalVarPanel) m_globalVarPanel->refreshEnums();   // 变量类型下拉同步枚举
}

void EditorWindow::startRuntime() {
    if (m_runtime) return;
    const int index = m_docTabBar->currentIndex();
    if (index < 0) return;
    const QString tabPath = m_docTabBar->tabData(index).toString();
    // 关卡蓝图 tab / 游戏视图 tab 都用最近激活的关卡
    const bool useActive = (tabPath == DocTabBar::kGameViewTabData
                         || isLevelBlueprintTab(tabPath));
    const QString path = useActive ? m_activeLevelPath : tabPath;
    LevelDocument* doc = m_openLevels.value(path);
    if (!doc || !m_viewport) return;

    m_runtime = new BPRuntime(doc, this);
    m_runtime->setGlobalVars(&m_globalVars);
    m_uiRuntime = new UIRuntime(m_project.path, this);
    m_runtime->setUIRuntime(m_uiRuntime);
    connect(m_uiRuntime, &UIRuntime::uiStateChanged, this, [this]() {
        if (m_gameViewport) m_gameViewport->update();
    });
    connect(m_uiRuntime, &UIRuntime::buttonClicked,
            this, [this](const QString& instId, const QString& widgetName) {
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerButtonClick(instId, widgetName);
    });
    connect(m_uiRuntime, &UIRuntime::dropdownChanged,
            this, [this](const QString& instId, const QString& widgetName, int idx) {
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerDropdownChanged(instId, widgetName, idx);
    });
    connect(m_runtime, &BPRuntime::stateChanged, this, [this]() {
        if (!m_runtime || !m_viewport) return;
        // 触发本帧 Actor Tick
        const float dt = m_runtime->lastDt();
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerTick(dt);
        m_viewport->updateRuntimeActors(m_runtime->actors());
        m_viewport->syncPrintLog(m_runtime->printLog());
        if (m_gameViewport) {
            m_gameViewport->setRuntimeActors(m_runtime->actors());
            m_gameViewport->syncPrintLog(m_runtime->printLog());
        }
    });
    // 暂停时屏蔽按键事件
    connect(m_viewport, &Viewport2D::keyPressed, this, [this](const QString& key) {
        if (!m_runtime || (m_pauseBtn && m_pauseBtn->isChecked())) return;
        m_runtime->triggerKeyDown(key);
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerKeyDown(key);
    });
    connect(m_viewport, &Viewport2D::keyReleased, this, [this](const QString& key) {
        if (!m_runtime || (m_pauseBtn && m_pauseBtn->isChecked())) return;
        m_runtime->triggerKeyUp(key);
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerKeyUp(key);
    });

    connect(m_runtime, &BPRuntime::loadLevelRequested, this,
            [this](const QString& levelName) {
        const QString levelPath = m_project.path + "/Levels/" + levelName + ".level";
        if (!QFileInfo::exists(levelPath)) return;
        // 跳转前把当前关卡压入历史栈，供「返回上一关」精确回溯
        const QString cur = QFileInfo(m_activeLevelPath).baseName();
        if (!cur.isEmpty()) m_levelNavStack.append(cur);
        stopRuntime();
        openLevelTab(levelPath);
        startRuntime();
    }, Qt::QueuedConnection);

    connect(m_runtime, &BPRuntime::backLevelRequested, this, [this]() {
        if (m_levelNavStack.isEmpty()) return;   // 栈空：保持当前关卡，不动作
        const QString target = m_levelNavStack.takeLast();
        const QString levelPath = m_project.path + "/Levels/" + target + ".level";
        if (!QFileInfo::exists(levelPath)) return;
        stopRuntime();           // 返回是弹栈，不再压栈
        openLevelTab(levelPath);
        startRuntime();
    }, Qt::QueuedConnection);

    m_viewport->setRuntimeMode(true, m_runtime->actors());
    m_runtime->triggerBeginPlay();

    // 为每个有节点的 Actor 创建 ActorBPRuntime
    for (const ActorData& actor : doc->actors()) {
        const BPClass* bc = nullptr;
        if (actor.bpClass.startsWith("builtin/")) {
            bc = BPClass::findBuiltin(actor.bpClass);
        } else if (!actor.bpClass.isEmpty()) {
            bc = m_openBpClasses.value(actor.bpClass, nullptr);
            if (!bc) {
                auto* loaded = new BPClass(BPClass::load(
                    m_project.path + "/" + actor.bpClass));
                m_openBpClasses[actor.bpClass] = loaded;
                bc = loaded;
            }
        }
        if (bc && bc->hasNodes()) {
            auto* ar = new ActorBPRuntime(bc, actor.id,
                                          &m_runtime->mutableActors(), this);
            m_actorRuntimes.append(ar);
            ar->setUIRuntime(m_uiRuntime);
            connect(ar, &ActorBPRuntime::printOutput,
                    this, [this](const QString& text) {
                        m_runtime->appendPrintLog(text);
                    });
        }
    }
    // 触发各 Actor 蓝图 BeginPlay
    for (ActorBPRuntime* ar : m_actorRuntimes)
        ar->triggerBeginPlay();

    m_viewport->updateRuntimeActors(m_runtime->actors());
    m_viewport->syncPrintLog(m_runtime->printLog());

    // 游戏视图运行时模式
    if (m_gameViewport) {
        m_gameViewport->setRuntimeMode(true);
        m_gameViewport->setUIRuntime(m_uiRuntime);
        m_gameViewport->setRuntimeActors(m_runtime->actors());
        m_gameViewport->syncPrintLog(m_runtime->printLog());
    }
    // 自动跳转到游戏视图 Tab
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == DocTabBar::kGameViewTabData) {
            m_docTabBar->setCurrentIndex(i);
            break;
        }
    }

    if (m_runBtn)   m_runBtn->setEnabled(false);
    if (m_pauseBtn) m_pauseBtn->setEnabled(true);
    if (m_stopBtn)  m_stopBtn->setEnabled(true);

    if (m_gameViewport) {
        connect(m_gameViewport, &GameViewport::keyPressed, this, [this](const QString& key) {
            if (!m_runtime || (m_pauseBtn && m_pauseBtn->isChecked())) return;
            m_runtime->triggerKeyDown(key);
            for (ActorBPRuntime* ar : m_actorRuntimes)
                ar->triggerKeyDown(key);
        });
        connect(m_gameViewport, &GameViewport::keyReleased, this, [this](const QString& key) {
            if (!m_runtime || (m_pauseBtn && m_pauseBtn->isChecked())) return;
            m_runtime->triggerKeyUp(key);
            for (ActorBPRuntime* ar : m_actorRuntimes)
                ar->triggerKeyUp(key);
        });
        m_gameViewport->setFocus();
    }
}

void EditorWindow::togglePauseRuntime() {
    if (!m_pauseBtn) return;
    // m_pauseBtn->isChecked() 已由 Qt 自动切换；按键拦截在 keyPressed lambda 中处理
}

void EditorWindow::stopRuntime() {
    if (!m_runtime) return;
    disconnect(m_runtime, nullptr, this, nullptr);
    if (m_viewport) disconnect(m_viewport, &Viewport2D::keyPressed, this, nullptr);
    qDeleteAll(m_actorRuntimes);
    m_actorRuntimes.clear();
    delete m_runtime;
    m_runtime = nullptr;
    delete m_uiRuntime;
    m_uiRuntime = nullptr;

    if (m_viewport) {
        m_viewport->setRuntimeMode(false);
        m_viewport->clearPrintLog();
    }
    if (m_gameViewport) {
        m_gameViewport->setRuntimeMode(false);
        m_gameViewport->setUIRuntime(nullptr);
        m_gameViewport->clearPrintLog();
    }
    if (m_runBtn)   m_runBtn->setEnabled(true);
    if (m_pauseBtn) { m_pauseBtn->setChecked(false); m_pauseBtn->setEnabled(false); }
    if (m_stopBtn)  m_stopBtn->setEnabled(false);
}

void EditorWindow::openBlueprintTab() {
    if (m_activeLevelPath.isEmpty()) return;  // 无活动关卡，不开蓝图
    const QString tabId = DocTabBar::kBlueprintTabData + m_activeLevelPath;

    // 已浮动：置顶浮动窗口
    auto it = m_bpInstances.find(tabId);
    if (it != m_bpInstances.end() && it.value().dock) {
        it.value().dock->raise();
        return;
    }
    // 已嵌入：切换到该蓝图 Tab
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == tabId) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }
    // 首次打开：创建实例 + Tab
    ensureBpInstance(tabId);
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(m_activeLevelPath).baseName() + " 蓝图");
        m_docTabBar->setTabData(idx, tabId);
        updateTabTooltip(idx);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}

// 把某蓝图 tab 拖出为独立浮动窗口（可同时浮动多个）
void EditorWindow::floatBp(const QString& tabId) {
    auto it = m_bpInstances.find(tabId);
    if (it == m_bpInstances.end()) { ensureBpInstance(tabId); it = m_bpInstances.find(tabId); }
    BpInstance& inst = it.value();
    if (inst.dock) { inst.dock->raise(); return; }  // 已浮动

    // 移除对应 Tab
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == tabId) {
            QSignalBlocker b(m_docTabBar);
            m_docTabBar->removeTab(i);
            break;
        }
    }
    // editor 从中央 stack 移入 ADS 浮动 Dock
    m_centralStack->setCurrentWidget(m_viewportPage);
    m_centralStack->removeWidget(inst.editor);

    const QString title = inst.isLevelBp
        ? QFileInfo(inst.dataPath).baseName() + " 蓝图"
        : QFileInfo(inst.dataPath).baseName();
    auto* dock = new ads::CDockWidget(title);
    dock->setWidget(inst.editor);
    inst.editor->show();
    m_dockManager->addDockWidgetFloating(dock);
    inst.dock = dock;

    // 关闭浮动窗口 / 拖入 ADS 区域 → 嵌回
    connect(dock, &ads::CDockWidget::viewToggled, this, [this, tabId](bool open) {
        if (!open) QTimer::singleShot(0, this, [this, tabId]() { embedBp(tabId); });
    });
    connect(dock, &ads::CDockWidget::topLevelChanged, this, [this, tabId, dock](bool isTopLevel) {
        auto i2 = m_bpInstances.find(tabId);
        if (!isTopLevel && i2 != m_bpInstances.end() && dock->widget() == i2.value().editor)
            QTimer::singleShot(0, this, [this, tabId]() { embedBp(tabId); });
    });

    // 启动拖回 Tab 栏检测
    m_bpDropFirstDone = false;
    if (m_bpDropCheckTimer) m_bpDropCheckTimer->start();
}

void EditorWindow::openBpClassTab(const QString& bpFilePath) {
    // 已浮动：置顶
    auto it = m_bpInstances.find(bpFilePath);
    if (it != m_bpInstances.end() && it.value().dock) {
        it.value().dock->raise();
        return;
    }
    // 已嵌入 → 激活
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == bpFilePath) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }

    // 加载或复用 BPClass
    if (!m_openBpClasses.contains(bpFilePath)) {
        auto* bc = new BPClass(BPClass::load(bpFilePath));
        m_openBpClasses[bpFilePath] = bc;
    }
    ensureBpInstance(bpFilePath);

    // 添加 Tab（阻断信号，等 tabData 设好再触发 onTabChanged）
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(bpFilePath).baseName());
        m_docTabBar->setTabData(idx, bpFilePath);
        updateTabTooltip(idx);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}

void EditorWindow::openUIDocTab(const QString& uiFilePath) {
    // 已有 tab 则切换
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == uiFilePath) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }

    // 加载文档
    UIDocument* doc = m_openUIDocs.value(uiFilePath, nullptr);
    if (!doc) {
        doc = new UIDocument;
        if (!doc->load(uiFilePath)) { delete doc; return; }
        m_openUIDocs[uiFilePath] = doc;
    }

    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(uiFilePath).baseName());
        m_docTabBar->setTabData(idx, uiFilePath);
        updateTabTooltip(idx);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}

void EditorWindow::openEnumTab(const QString& enumPath) {
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == enumPath) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(enumPath).baseName());
        m_docTabBar->setTabData(idx, enumPath);
        updateTabTooltip(idx);
    }
    if (m_docTabBar->currentIndex() == idx) onTabChanged(idx);
    else                                    m_docTabBar->setCurrentIndex(idx);
}

// 把某浮动蓝图窗口嵌回 Tab 栏
void EditorWindow::embedBp(const QString& tabId) {
    auto it = m_bpInstances.find(tabId);
    if (it == m_bpInstances.end()) return;
    BpInstance& inst = it.value();
    if (!inst.dock) return;  // 已嵌入，防重入

    ads::CDockWidget* dock = inst.dock;
    BlueprintEditor*  ed   = inst.editor;
    inst.dock = nullptr;     // 先标记，避免 closeDockWidget 的 viewToggled 重入

    // 清拖回高亮
    if (auto* tb = findChild<QToolBar*>("docTabToolBar")) {
        if (tb->property("bpDropHighlight").toBool()) {
            tb->setProperty("bpDropHighlight", false);
            tb->style()->unpolish(tb);
            tb->style()->polish(tb);
            tb->update();
        }
    }

    // 占位避免 ADS 持有悬空指针，把 editor 接回 stack
    dock->setWidget(new QWidget());
    dock->closeDockWidget();
    dock->deleteLater();
    m_centralStack->addWidget(ed);

    // 重建 Tab
    const QString title = inst.isLevelBp
        ? QFileInfo(inst.dataPath).baseName() + " 蓝图"
        : QFileInfo(inst.dataPath).baseName();
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + title);
        m_docTabBar->setTabData(idx, tabId);
        updateTabTooltip(idx);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);
    else
        m_docTabBar->setCurrentIndex(idx);
}

bool EditorWindow::isTextInputFocused() const {
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) ||
           qobject_cast<QTextEdit*>(fw) ||
           qobject_cast<QAbstractSpinBox*>(fw);
}
