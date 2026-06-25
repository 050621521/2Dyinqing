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
#include "Recorder.h"
#include "models/ActorTypeUtils.h"
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
#include <QWidgetAction>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QAbstractButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QFrame>
#include <QSizePolicy>
#include <QCloseEvent>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include "UndoCommands.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QApplication>
#include <QClipboard>
#include <QStyle>
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

    // Q/W/E/R 工具模式切换（仅场景视口）
    // 作用域绑定到 m_viewport：仅场景视口（或其子控件）聚焦时才生效，
    // 运行时游戏视图/蓝图视图聚焦时这些键不会被消费，直达游戏（参照虚幻关卡视口命令）。
    auto* qSc = new QShortcut(QKeySequence("Q"), m_viewport);
    qSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(qSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Select);
    });
    auto* wSc = new QShortcut(QKeySequence("W"), m_viewport);
    wSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(wSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Move);
    });
    auto* eSc = new QShortcut(QKeySequence("E"), m_viewport);
    eSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(eSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Rotate);
    });
    auto* rSc = new QShortcut(QKeySequence("R"), m_viewport);
    rSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(rSc, &QShortcut::activated, this, [this]() {
        if (m_centralStack->currentIndex() == 0 && m_viewport)
            m_viewport->setToolMode(Viewport2D::ToolMode::Scale);
    });

    // 以下为跨上下文单键（F/Esc/Delete/退格）：蓝图/UI 编辑器也要用，无法死绑到场景视口。
    // 改为运行时挂起：startRuntime() 禁用、stopRuntime() 恢复，运行时全部交给游戏。

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

    // 运行时需挂起的跨上下文单键快捷键（避免抢占游戏按键）
    m_editorSingleKeyShortcuts << fSc << escSc << delSc << bsSc;

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
    copySc->setContext(Qt::ApplicationShortcut);
    connect(copySc, &QShortcut::activated, this, [this]() {
        if (isTextInputFocused()) return;
        const int idx = m_centralStack->currentIndex();
        if (idx == 0 && m_viewport) m_viewport->copySelected();
        else if (idx == 3 && m_uiEditor) m_uiEditor->copySelected();
    });

    // Ctrl+V 粘贴
    auto* pasteSc = new QShortcut(QKeySequence::Paste, this);
    pasteSc->setContext(Qt::ApplicationShortcut);
    connect(pasteSc, &QShortcut::activated, this, [this]() {
        if (isTextInputFocused()) return;
        const int idx = m_centralStack->currentIndex();
        if (idx == 0 && m_viewport) m_viewport->pasteFromClipboard();
        else if (idx == 3 && m_uiEditor) m_uiEditor->paste();
    });

    QString defaultLevel = ProjectSettingsDialog::readDefaultLevel(m_project.path);
    if (defaultLevel.isEmpty())
        defaultLevel = m_project.path + "/Levels/Default.level";
    if (QFileInfo::exists(defaultLevel))
        openLevelTab(defaultLevel);
}

// 复现录制：把 QJsonValue 转成可读字符串（嵌套对象/数组用紧凑 JSON）
static QString jvalStr(const QJsonValue& v) {
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return v.toVariant().toString();
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

    // 复现录制按钮：QMenuBar 不渲染内嵌 QWidgetAction，故做成菜单栏子控件，
    // 手动定位到“窗口”菜单右侧，并随菜单栏尺寸变化重定位（见 eventFilter）。
    m_recordBtn = new QToolButton(mb);
    m_recordBtn->setObjectName("recordBtn");
    m_recordBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_recordBtn->setText("● 复现录制");
    m_recordBtn->setToolTip("开始复现录制：记录操作时间线、状态快照与控制台日志，停止后生成文件供 AI 排查");
    m_recordBtn->setCursor(Qt::PointingHandCursor);
    connect(m_recordBtn, &QToolButton::clicked, this, &EditorWindow::toggleRecording);
    mb->installEventFilter(this);
    QTimer::singleShot(0, this, &EditorWindow::positionRecordButton);

    // 顶部菜单项触发 → 复现录制（菜单动作 event filter 抓不到，用 triggered 信号）
    connect(mb, &QMenuBar::triggered, this, [](QAction* a) {
        if (a && !a->text().isEmpty())
            Recorder::instance().log("菜单", QString("[%1]").arg(a->text()));
    });
    // 全局兜底网：录制时记录所有按钮点击、快捷键、画布缩放/平移（见 eventFilter）
    qApp->installEventFilter(this);

    auto* spacer = new QWidget(mb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mb->setCornerWidget(spacer, Qt::TopRightCorner);

    m_recordTimer = new QTimer(this);
    m_recordTimer->setInterval(500);
    connect(m_recordTimer, &QTimer::timeout, this, [this]() {
        if (!m_recordBtn) return;
        const qint64 s = m_recordClock.elapsed() / 1000;
        m_recordBtn->setText(QString("● 录制中 %1:%2  ⏹")
            .arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')));
        positionRecordButton();   // 文本变宽后重新对齐
    });
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
    auto* detailsArea = m_dockManager->addDockWidget(
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
            const bool isGlobalArrayOp = n.type.startsWith("Array.")
                                      && n.params.value("scope") != "local";
            return (n.type == "Global.Get" || n.type == "Global.Set" || isGlobalArrayOp)
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
    m_gvDock = new ads::CDockWidget("我的蓝图");
    m_gvDock->setWidget(m_globalVarPanel);
    // 停靠在画布左侧，像虚幻"我的蓝图"常驻可见
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_gvDock);
    if (m_windowMenu) m_windowMenu->addAction(m_gvDock->toggleViewAction());
    // 蓝图上下文的"细节"面板：变量属性（名字/类型），独立于视口的大纲/细节。
    // 注意：objectName 必须唯一（ADS 用它做 map 键），否则与视口"细节"键冲突
    // 会在 restoreState 时把其中一个 dock 搞悬空 → toggleView 段错误。显示标题仍设为"细节"。
    m_varDetailsDock = new ads::CDockWidget("变量细节");
    m_varDetailsDock->setWindowTitle("细节");
    m_varDetailsDock->setWidget(m_globalVarPanel->detailsWidget());
    // 像虚幻蓝图编辑器：我的蓝图在左、细节在右——与视口"细节"共用右下区域
    // （同一时刻只显示一个，由 onTabChanged 切换）。
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_varDetailsDock, detailsArea);

    // ── 局部变量面板（绑定当前关卡蓝图文档；列表+细节同停左侧，不动右侧细节区）──
    m_localVarPanel = new GlobalVarPanel();
    m_localVarPanel->setProjectRoot(m_project.path);
    m_localVarPanel->bindSource(
        [this]() -> QList<GlobalVarDef> {
            LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
            return doc ? doc->localVars() : QList<GlobalVarDef>{};
        },
        [this](const QList<GlobalVarDef>& vars) {
            if (LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr))
                doc->setLocalVars(vars);
        });
    connect(m_localVarPanel, &GlobalVarPanel::changed, this, [this]() {
        for (auto it = m_bpInstances.begin(); it != m_bpInstances.end(); ++it)
            if (it.value().editor) it.value().editor->update();
    });
    // 局部变量改名：同步当前关卡蓝图里引用它的 Local.*/数组操作 节点
    connect(m_localVarPanel, &GlobalVarPanel::varRenamed, this,
            [this](const QString& oldN, const QString& newN) {
        LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
        if (!doc) return;
        for (const BPNode& n : doc->bpNodes()) {
            const bool isLocalArrayOp = n.type.startsWith("Array.")
                                     && n.params.value("scope") == "local";
            const bool ref = (n.type == "Local.Get" || n.type == "Local.Set" || isLocalArrayOp)
                          && n.params.value("varName") == oldN;
            if (ref) { BPNode nn = n; nn.params["varName"] = newN; doc->updateBPNode(nn); }
        }
        for (auto it = m_bpInstances.begin(); it != m_bpInstances.end(); ++it)
            if (it.value().editor) it.value().editor->update();
    });
    m_localVarDock = new ads::CDockWidget("局部变量");
    m_localVarDock->setWidget(m_localVarPanel);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_localVarDock,
                                 m_gvDock->dockAreaWidget());
    if (m_windowMenu) m_windowMenu->addAction(m_localVarDock->toggleViewAction());
    m_localVarDetailsDock = new ads::CDockWidget("局部变量细节");
    m_localVarDetailsDock->setWindowTitle("细节");
    m_localVarDetailsDock->setWidget(m_localVarPanel->detailsWidget());
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_localVarDetailsDock,
                                 m_varDetailsDock->dockAreaWidget());

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
        // 旧布局不含这两个面板 → 恢复后会浮窗；强制摆回左侧并重存默认布局
        bool healed = false;
        if (m_gvDock && (m_gvDock->isFloating() || m_gvDock->isClosed())) {
            m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_gvDock);
            healed = true;
        }
        if (m_varDetailsDock && (m_varDetailsDock->isFloating() || m_varDetailsDock->isClosed())) {
            // 与视口"细节"共用右下区域（虚幻蓝图编辑器：细节在右）
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_varDetailsDock,
                                         m_detailsDockW ? m_detailsDockW->dockAreaWidget() : nullptr);
            healed = true;
        }
        // 局部变量 / 局部变量细节是更晚加入的面板，旧布局同样不含 → 摆回对应区域
        if (m_localVarDock && (m_localVarDock->isFloating() || m_localVarDock->isClosed())) {
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_localVarDock,
                                         m_gvDock ? m_gvDock->dockAreaWidget() : nullptr);
            healed = true;
        }
        if (m_localVarDetailsDock && (m_localVarDetailsDock->isFloating() || m_localVarDetailsDock->isClosed())) {
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_localVarDetailsDock,
                                         m_varDetailsDock ? m_varDetailsDock->dockAreaWidget() : nullptr);
            healed = true;
        }
        if (healed) m_layoutManager->saveLayout("默认布局");
        // 初始按当前标签决定显隐
        if (m_docTabBar) {
            const QString tabData =
                m_docTabBar->tabData(m_docTabBar->currentIndex()).toString();
            const bool bp = isAnyBlueprintTab(tabData);
            const bool levelBp = isLevelBlueprintTab(tabData);
            if (m_gvDock)         m_gvDock->toggleView(bp);
            if (m_varDetailsDock) m_varDetailsDock->toggleView(bp);
            if (m_localVarDock)        m_localVarDock->toggleView(levelBp);
            if (m_localVarDetailsDock) m_localVarDetailsDock->toggleView(levelBp);
            if (m_outlineDockW)   m_outlineDockW->toggleView(!bp);
            if (m_detailsDockW)   m_detailsDockW->toggleView(!bp);
        }
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
        static const char* names[] = {"选择", "移动", "旋转", "缩放"};
        Recorder::instance().log("工具 →", names[static_cast<int>(mode)]);
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
// ── 方案 A：把蓝图类当作「类默认对象」喂给细节面板编辑 ────────────────
// 类的 components + defaults(字段名→值) ↔ 一个合成的 ActorData
static ActorData actorFromBpClass(const BPClass& bc, const QString& classPath) {
    QJsonObject obj = QJsonObject::fromVariantMap(bc.defaults);
    QStringList comps = bc.components.isEmpty() ? QStringList{"变换"} : bc.components;
    QJsonArray compArr;
    for (const QString& c : comps) compArr.append(c);
    obj["components"] = compArr;
    ActorData a = ActorData::fromJson(obj);
    a.id      = classPath;   // 非空，避免 DetailsPanel 早退
    a.name    = bc.name;
    a.bpClass = QString();   // 隐藏"编辑蓝图"按钮（这是类本身）
    a.components = comps;
    return a;
}

static void applyActorToBpClass(const ActorData& a, BPClass& bc) {
    bc.components = a.components;
    QJsonObject j = a.toJson();
    // 仅剔除「实例身份 + 位置」；缩放/旋转/组件字段保留为可继承的类默认值
    for (const char* k : {"id","name","bpClass","position","overriddenFields"})
        j.remove(QString::fromUtf8(k));
    bc.defaults = j.toVariantMap();
}

// 活继承：把类默认值刷进实例所有「未覆盖」的字段（组件跟随类）
static bool resolveInstanceFromClass(ActorData& inst, const BPClass& cls) {
    QJsonObject j = inst.toJson();
    for (auto it = cls.defaults.constBegin(); it != cls.defaults.constEnd(); ++it)
        if (!inst.overriddenFields.contains(it.key()))
            j[it.key()] = QJsonValue::fromVariant(it.value());
    QJsonArray comps;
    for (const QString& c : cls.components) comps.append(c);
    j["components"] = comps;
    ActorData resolved = ActorData::fromJson(j);
    resolved.overriddenFields = inst.overriddenFields;  // fromJson 会按"无键=旧"误填，强制保留
    if (resolved.toJson() == inst.toJson()) return false;  // 无变化不标脏
    inst = resolved;
    return true;
}

// 关卡加载后：把每个类实例对其当前类默认值重新解析（实现活继承）
static void resolveLevelInstances(LevelDocument* doc, const QString& projectRoot) {
    if (!doc) return;
    QHash<QString, BPClass> cache;
    const QList<ActorData> snap = doc->actors();
    for (const ActorData& a : snap) {
        if (a.bpClass.isEmpty() || a.bpClass.startsWith("builtin/")) continue;
        if (!cache.contains(a.bpClass))
            cache.insert(a.bpClass, BPClass::load(projectRoot + "/" + a.bpClass));
        ActorData copy = a;
        if (resolveInstanceFromClass(copy, cache[a.bpClass]))
            doc->updateActor(copy);
    }
}

void EditorWindow::onTabChanged(int index) {
    const QString path = m_docTabBar->tabData(index).toString();
    if (m_runtime && path != DocTabBar::kGameViewTabData)
        stopRuntime();
    // 上下文切换：蓝图页显示"我的蓝图"+蓝图"细节"，隐藏视口的大纲/细节；视口页反之
    const bool bpCtx = isAnyBlueprintTab(path);
    const bool levelBpCtx = isLevelBlueprintTab(path);   // 局部变量仅关卡蓝图
    const bool classBpCtx = path.endsWith(".bp");        // Actor 类：显示组件细节面板
    if (m_gvDock)         m_gvDock->toggleView(bpCtx);
    if (m_varDetailsDock) m_varDetailsDock->toggleView(bpCtx);
    if (m_localVarDock)        m_localVarDock->toggleView(levelBpCtx);
    if (m_localVarDetailsDock) m_localVarDetailsDock->toggleView(levelBpCtx);
    if (m_outlineDockW)   m_outlineDockW->toggleView(!bpCtx);
    if (m_detailsDockW)   m_detailsDockW->toggleView(!bpCtx || classBpCtx);
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
                if (m_localVarPanel) m_localVarPanel->reloadFromSource();  // 局部变量随关卡切换
            }
        }
        // Actor 类：细节面板编辑类的组件 + 默认值（方案 A：复用实例那套 UI）
        if (classBpCtx) {
            BPClass* bc = m_openBpClasses.value(path, nullptr);
            if (bc) {
                m_detailsPanel->showActor(actorFromBpClass(*bc, path));
                m_tabConnections << connect(m_detailsPanel, &DetailsPanel::actorModified,
                    this, [this, path](const ActorData& mod) {
                        BPClass* c = m_openBpClasses.value(path, nullptr);
                        if (!c) return;
                        applyActorToBpClass(mod, *c);
                        c->save();
                        // 活继承：改类默认值 → 重刷所有未覆盖该字段的实例
                        const QString rel = QDir(m_project.path).relativeFilePath(path);
                        for (LevelDocument* d : m_openLevels.values()) {
                            if (!d) continue;
                            const QList<ActorData> snap = d->actors();
                            for (const ActorData& a : snap) {
                                if (a.bpClass != rel) continue;
                                ActorData copy = a;
                                if (resolveInstanceFromClass(copy, *c))
                                    d->updateActor(copy);
                            }
                        }
                        updateSaveLabel();
                    });
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
        resolveLevelInstances(doc, m_project.path);   // 活继承：实例跟随类默认值
        m_openLevels[path] = doc;
    }
    LevelDocument* doc = m_openLevels[path];
    m_activeLevelPath = path;
    m_activeUndoStack = doc->undoStack();

    // 复现录制：切关卡时记录并拍快照
    if (Recorder::instance().isRecording()) {
        Recorder::instance().log("切关卡 →", QFileInfo(path).fileName());
        Recorder::instance().snapshot(doc, nullptr, "切关卡后");
    }

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

    // 大纲多选 → 同步到视口选区（视口再统一广播 selectionChanged 驱动细节面板/对齐/回灌高亮）
    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::selectionChanged,
                                this, [this](QStringList ids) {
        if (m_viewport) m_viewport->setSelectedIds(ids);
        Recorder::instance().log("选中(大纲)", QString("%1 个").arg(ids.size()));
    });
    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::copyRequested,
                                this, [this]() { if (m_viewport) m_viewport->copySelected(); });
    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::pasteRequested,
                                this, [this]() { if (m_viewport) m_viewport->pasteFromClipboard(); });

    if (m_viewport) {
        m_tabConnections << connect(m_viewport, &Viewport2D::actorSelected,
                                    this, [this](const ActorData& a) {
            Recorder::instance().log("选中(视口)", QString("\"%1\"").arg(a.name));
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::selectionChanged,
                                    this, [this](QStringList ids) {
            const int n = ids.size();
            for (auto* btn : m_viewportAlignBtns)
                btn->setEnabled(n >= 2);
            if (n == 0)
                m_detailsPanel->clearActor();
            else if (n == 1) {
                LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
                if (doc) for (const ActorData& a : doc->actors())
                    if (a.id == ids.first()) { m_detailsPanel->showActor(a); break; }
            }
            else {
                LevelDocument* doc = m_openLevels.value(m_activeLevelPath, nullptr);
                QList<ActorData> sel;
                if (doc) for (const QString& id : ids)
                    for (const ActorData& a : doc->actors())
                        if (a.id == id) { sel << a; break; }
                m_detailsPanel->showMultiSelection(sel);
            }
            // 回灌大纲高亮（大纲内部阻塞信号，不会回环）
            if (m_sceneOutliner) m_sceneOutliner->setSelectedIds(ids);
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
                                    this, [this](const ActorData& a) {
            m_detailsPanel->showActor(a);
            // 帧级中间值：捕捉拖动/缩放过程的轨迹（含边缘吸附震荡）；
            // 连续相同值由 Recorder 自动合并，避免刷屏。
            Recorder::instance().log("  ↳帧",
                QString("\"%1\" pos(%2,%3) scale(%4,%5)")
                    .arg(a.name).arg(a.x).arg(a.y).arg(a.scaleX).arg(a.scaleY));
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::actorTransformed,
                                    this, [this](const ActorData& a) {
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
            Recorder::instance().log("变换完成",
                QString("\"%1\" pos(%2,%3) rot%4 scale(%5,%6)")
                    .arg(a.name).arg(a.x).arg(a.y).arg(a.rotation)
                    .arg(a.scaleX).arg(a.scaleY));
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::actorCreated,
                                    this, [this, doc](const ActorData& a) {
            m_sceneOutliner->loadLevel(doc);
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
            Recorder::instance().log("创建",
                QString("\"%1\" (%2)").arg(a.name, bpClassLabel(a.bpClass)));
        });
        m_tabConnections << connect(m_viewport, &Viewport2D::actorRemoved,
                                    this, [this, doc](const QString& id) {
            m_sceneOutliner->loadLevel(doc);
            m_detailsPanel->clearActor();
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
            Recorder::instance().log("删除", QString("id=%1").arg(id));
        });
        // 从内容浏览器拖 .bp 类进视口 → 落点生成实例（继承类的组件+动画器配置）
        m_tabConnections << connect(m_viewport, &Viewport2D::bpClassDropped, this,
                                    [this, doc](const QString& bpPath, const QPointF& worldPos) {
            const QString rel = QDir(m_project.path).relativeFilePath(bpPath);
            BPClass bc = BPClass::load(bpPath);
            ActorData a = actorFromBpClass(bc, rel);   // 取类的组件 + 默认字段
            a.id      = QUuid::createUuid().toString(QUuid::WithoutBraces);
            a.name    = bc.name.isEmpty() ? QFileInfo(bpPath).baseName() : bc.name;
            a.bpClass = rel;                            // 实例引用类（相对项目根）
            a.x = (float)worldPos.x();
            a.y = (float)worldPos.y();
            a.overriddenFields = {"position"};          // 仅位置算覆盖，其余跟随类
            doc->addActor(a);
            m_sceneOutliner->loadLevel(doc);
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
            Recorder::instance().log("创建",
                QString("\"%1\" (%2)").arg(a.name, bpClassLabel(a.bpClass)));
        });
    }

    m_tabConnections << connect(m_detailsPanel, &DetailsPanel::actorModified,
                                this, [this, doc](const ActorData& afterIn) {
        ActorData after = afterIn;
        // 找出修改前的状态
        ActorData before;
        for (const ActorData& a : doc->actors())
            if (a.id == after.id) { before = a; break; }
        // 活继承：实例改了哪些字段就标记为「已覆盖」（仅类实例，builtin 无类可继承）
        if (!after.bpClass.isEmpty() && !after.bpClass.startsWith("builtin/")) {
            after.overriddenFields = before.overriddenFields;
            const QJsonObject bj = before.toJson(), aj = after.toJson();
            for (const QString& k : aj.keys()) {
                if (k == "id" || k == "name" || k == "components" || k == "overriddenFields")
                    continue;
                if (aj[k] != bj[k]) after.overriddenFields.insert(k);
            }
        }
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
        // diff before/after，记下具体改了哪个字段、改成什么值
        const QJsonObject jb = before.toJson(), ja = after.toJson();
        QStringList changes;
        for (auto it = ja.begin(); it != ja.end(); ++it)
            if (jb.value(it.key()) != it.value())
                changes << QString("%1: %2→%3").arg(
                    it.key(), jvalStr(jb.value(it.key())), jvalStr(it.value()));
        Recorder::instance().log("修改属性", changes.isEmpty()
            ? QString("\"%1\"").arg(after.name)
            : QString("\"%1\" %2").arg(after.name, changes.join(", ")));
    });

    // 多选批量编辑：一次 macro 包裹所有改动 → 单步撤销，刷新一次
    m_tabConnections << connect(m_detailsPanel, &DetailsPanel::actorsModified,
                                this, [this, doc](const QList<ActorData>& afterList) {
        if (afterList.isEmpty()) return;
        auto doRefresh = [this, doc]() {
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
            if (m_viewport) m_viewport->update();
            if (m_gameViewport) { m_gameViewport->update(); updateGameViewToolbar(doc); }
            m_sceneOutliner->loadLevel(doc);
        };
        doc->undoStack()->beginMacro("批量修改属性");
        for (const ActorData& afterIn : afterList) {
            ActorData after = afterIn, before;
            for (const ActorData& a : doc->actors())
                if (a.id == after.id) { before = a; break; }
            // 活继承：类实例改了哪些字段标记为「已覆盖」
            if (!after.bpClass.isEmpty() && !after.bpClass.startsWith("builtin/")) {
                after.overriddenFields = before.overriddenFields;
                const QJsonObject bj = before.toJson(), aj = after.toJson();
                for (const QString& k : aj.keys()) {
                    if (k == "id" || k == "name" || k == "components" || k == "overriddenFields")
                        continue;
                    if (aj[k] != bj[k]) after.overriddenFields.insert(k);
                }
            }
            doc->undoStack()->push(new ActorModifyCmd(doc, before, after, doRefresh));
        }
        doc->undoStack()->endMacro();
        // 大纲被 loadLevel 重建后，恢复多选高亮
        QStringList ids;
        for (const ActorData& a : afterList) ids << a.id;
        if (m_sceneOutliner) m_sceneOutliner->setSelectedIds(ids);
        Recorder::instance().log("批量修改属性", QString("%1 个对象").arg(afterList.size()));
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
    // 录制中关窗：先落盘，避免丢失录制
    if (Recorder::instance().isRecording()) Recorder::instance().stopRecording();
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
        // 所有移动完成后跑碰撞 pass（分轴阻挡 + 重叠事件）
        m_runtime->runCollisionPass();
        m_viewport->updateRuntimeActors(m_runtime->actors());
        m_viewport->syncPrintLog(m_runtime->printLog());
        if (m_gameViewport) {
            m_gameViewport->setRuntimeActors(m_runtime->actors());
            m_gameViewport->syncPrintLog(m_runtime->printLog());
        }
    });
    // 重叠事件 → 分发给「本方」Actor 蓝图的「碰撞时」
    connect(m_runtime, &BPRuntime::overlapDetected, this,
            [this](const QString& selfId, const QString& otherId, const QString& otherTag) {
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerCollision(selfId, otherId, otherTag);
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

    // 复现录制：运行时打印按时序进时间线 + 运行前快照
    connect(m_runtime, &BPRuntime::printAppended, this, [](const QString& text) {
        Recorder::instance().log("打印", QString("\"%1\"").arg(text));
    });
    Recorder::instance().log("▶运行", "");
    Recorder::instance().snapshot(doc, m_runtime, "运行前");

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
                                          &m_runtime->mutableActors(), m_runtime, this);
            m_actorRuntimes.append(ar);
            ar->setUIRuntime(m_uiRuntime);
            connect(ar, &ActorBPRuntime::printOutput,
                    this, [this](const QString& text) {
                        m_runtime->appendPrintLog(text);
                    });
            // Actor 蓝图的换关请求转交关卡运行时（复用同一套换关处理）
            connect(ar, &ActorBPRuntime::loadLevelRequested,
                    m_runtime, &BPRuntime::loadLevelRequested);
            connect(ar, &ActorBPRuntime::backLevelRequested,
                    m_runtime, &BPRuntime::backLevelRequested);
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

    // 运行时挂起跨上下文单键编辑快捷键（F/Esc/Delete/退格），把按键全交给游戏
    for (QShortcut* sc : m_editorSingleKeyShortcuts)
        if (sc) sc->setEnabled(false);
}

void EditorWindow::togglePauseRuntime() {
    if (!m_pauseBtn) return;
    // m_pauseBtn->isChecked() 已由 Qt 自动切换；按键拦截在 keyPressed lambda 中处理
}

void EditorWindow::stopRuntime() {
    if (!m_runtime) return;
    // 复现录制：运行结束前抓一份运行时快照（含打印累计）
    Recorder::instance().snapshot(nullptr, m_runtime, "运行后");
    Recorder::instance().log("■停止运行", "");
    disconnect(m_runtime, nullptr, this, nullptr);
    if (m_viewport) disconnect(m_viewport, &Viewport2D::keyPressed, this, nullptr);
    qDeleteAll(m_actorRuntimes);
    m_actorRuntimes.clear();
    delete m_runtime;
    m_runtime = nullptr;
    delete m_uiRuntime;
    m_uiRuntime = nullptr;

    // 停止运行：恢复运行时被挂起的编辑器快捷键
    for (QShortcut* sc : m_editorSingleKeyShortcuts)
        if (sc) sc->setEnabled(true);

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

void EditorWindow::toggleRecording() {
    Recorder& rec = Recorder::instance();
    auto restyle = [this]() {
        m_recordBtn->style()->unpolish(m_recordBtn);
        m_recordBtn->style()->polish(m_recordBtn);
    };
    if (!rec.isRecording()) {
        const QString levelName = m_activeLevelPath.isEmpty()
            ? QString() : QFileInfo(m_activeLevelPath).fileName();
        static const char* toolNames[] = {"选择", "移动", "旋转", "缩放"};
        int tid = m_toolBtnGroup ? m_toolBtnGroup->checkedId() : 0;
        if (tid < 0 || tid > 3) tid = 0;
        rec.startRecording(m_project, levelName, int(m_ppu), toolNames[tid]);
        m_recordClock.start();
        m_recordTimer->start();
        m_recordBtn->setText("● 录制中 00:00  ⏹");
        m_recordBtn->setProperty("recording", true);
        restyle();
        positionRecordButton();
    } else {
        const QString path = rec.stopRecording();
        m_recordTimer->stop();
        m_recordBtn->setText("● 复现录制");
        m_recordBtn->setProperty("recording", false);
        restyle();
        positionRecordButton();
        if (!path.isEmpty()) {
            QApplication::clipboard()->setText(path);
            QMessageBox::information(this, "复现录制已保存",
                QString("已保存到：\n%1\n\n路径已复制到剪贴板。发给 AI，或直接说“录好了”即可。")
                    .arg(path));
        } else {
            QMessageBox::warning(this, "复现录制", "保存失败：未找到项目目录。");
        }
    }
}

void EditorWindow::positionRecordButton() {
    if (!m_recordBtn || !m_windowMenu) return;
    auto* mb = menuBar();
    const QRect r = mb->actionGeometry(m_windowMenu->menuAction());
    if (r.isNull()) return;
    m_recordBtn->adjustSize();
    const int h = qMax(m_recordBtn->sizeHint().height(), 22);
    const int y = (mb->height() - h) / 2;
    m_recordBtn->setGeometry(r.right() + 10, y, m_recordBtn->sizeHint().width(), h);
    m_recordBtn->raise();
    m_recordBtn->show();
}

bool EditorWindow::eventFilter(QObject* o, QEvent* e) {
    const QEvent::Type t = e->type();
    if (o == menuBar() &&
        (t == QEvent::Resize || t == QEvent::Show || t == QEvent::LayoutRequest)) {
        positionRecordButton();
    }

    // 全局兜底网：录制时记录 UI 层操作，覆盖所有面板/对话框
    if (Recorder::instance().isRecording()) {
        switch (t) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                // 按钮点击 → 用按钮文字/提示/objectName 作语义标签（跳过录制按钮自身）
                if (auto* b = qobject_cast<QAbstractButton*>(o); b && b != m_recordBtn) {
                    QString lbl = b->text();
                    if (lbl.isEmpty()) lbl = b->toolTip();
                    if (lbl.isEmpty()) lbl = b->objectName();
                    if (!lbl.isEmpty())
                        Recorder::instance().log("点击", QString("[%1]").arg(lbl.simplified()));
                }
            } else if (me->button() == Qt::RightButton &&
                       (o == m_viewport || o == m_gameViewport)) {
                Recorder::instance().log("平移画布", "");
            }
            break;
        }
        case QEvent::Wheel:
            if (o == m_viewport || o == m_gameViewport)
                Recorder::instance().log("缩放画布", "");
            break;
        case QEvent::KeyPress: {
            auto* ke = static_cast<QKeyEvent*>(e);
            const bool hasMod = ke->modifiers() &
                (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier);
            const int k = ke->key();
            const bool isModKey = (k == Qt::Key_Control || k == Qt::Key_Shift ||
                                   k == Qt::Key_Alt || k == Qt::Key_Meta);
            // 仅记带修饰键的快捷键（撤销/重做/保存/复制粘贴等），跳过文本输入与纯修饰键
            if (hasMod && !isModKey && !ke->isAutoRepeat() && !isTextInputFocused()) {
                const QString s = QKeySequence(k | int(ke->modifiers()))
                                      .toString(QKeySequence::NativeText);
                if (!s.isEmpty()) Recorder::instance().log("快捷键", s);
            }
            break;
        }
        default: break;
        }
    }
    return QMainWindow::eventFilter(o, e);
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
