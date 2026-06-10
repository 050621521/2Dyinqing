#include "EditorWindow.h"
#include "Viewport2D.h"
#include "BlueprintEditor.h"
#include "BPRuntime.h"
#include "SceneOutliner.h"
#include "DetailsPanel.h"
#include "ContentBrowser.h"
#include "ProjectSettingsDialog.h"
#include "models/LevelDocument.h"
#include <QMetaObject>
#include <QMenuBar>
#include <QToolBar>
#include <QTabBar>
#include <QSplitter>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QFrame>
#include <QSizePolicy>
#include <QDockWidget>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QEvent>
#include <QTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>

EditorWindow::EditorWindow(const ProjectInfo& project, QWidget* parent)
    : QMainWindow(parent), m_project(project)
{
    setWindowTitle(project.name + " — 2D引擎编辑器");
    setMinimumSize(1100, 700);
    resize(1440, 900);

    setupMenuBar();
    setupDocTabBar();           // 第一行：文档 Tab
    addToolBarBreak();          // ← 换行，保证主工具栏独占第二行
    setupMainToolBar();         // 第二行：模式 + 播放 + 平台
    setDockOptions(QMainWindow::AnimatedDocks);
    setupCentralArea();         // 中央：视口（含次级工具栏）+ 右侧面板
    setupBottomBar();           // 底部：极细状态栏

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &EditorWindow::saveCurrentLevel);

    // 自动打开默认关卡（优先读 project.json，回退到 Default.level）
    QString defaultLevel = ProjectSettingsDialog::readDefaultLevel(m_project.path);
    if (defaultLevel.isEmpty())
        defaultLevel = m_project.path + "/Levels/Default.level";
    if (QFileInfo::exists(defaultLevel))
        openLevelTab(defaultLevel);
}

// ── 1. 菜单栏 ────────────────────────────────────────────────────────
void EditorWindow::setupMenuBar() {
    auto* mb = menuBar();
    mb->setNativeMenuBar(false);   // 强制显示在窗口内（macOS 默认放屏幕顶部）
    mb->setObjectName("editorMenuBar");
    auto* fileMenu = mb->addMenu("文件");
    auto* projSettingsAct = fileMenu->addAction("项目设置…");
    connect(projSettingsAct, &QAction::triggered, this, &EditorWindow::onProjectSettings);

    for (const QString& n : {"编辑","窗口","工具","构建","选择","Actor","帮助"})
        mb->addMenu(n);

    // 右侧显示项目名
    auto* spacer = new QWidget(mb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mb->setCornerWidget(spacer, Qt::TopRightCorner);
}

// ── 2. 文档 Tab 栏（独立第一行） ─────────────────────────────────────
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

// ── 3. 主工具栏（独立第二行） ─────────────────────────────────────────
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

// ── 4. 中央区域 ───────────────────────────────────────────────────────
void EditorWindow::setupCentralArea() {
    // 左：视口次级工具栏 + Viewport2D（直接作为 central widget，右侧用 QDockWidget）
    auto* leftWrap = new QWidget(this);
    leftWrap->setObjectName("viewportWrap");
    auto* leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);
    leftLay->addWidget(buildViewportToolBar(leftWrap));

    m_viewport  = new Viewport2D(leftWrap);
    m_viewStack = new QStackedWidget(leftWrap);
    m_viewStack->addWidget(m_viewport);
    leftLay->addWidget(m_viewStack, 1);

    // 记录 leftWrap，用于覆盖层定位
    m_viewportWrap = leftWrap;
    leftWrap->installEventFilter(this);

    // ── 浮动蓝图面板（父为 EditorWindow，覆盖整个中央区域）────────────
    m_bpPanel = new QWidget(this);
    m_bpPanel->setObjectName("bpFloatPanel");
    m_bpPanel->resize(820, 560);
    m_bpPanel->hide();

    auto* bpLay = new QVBoxLayout(m_bpPanel);
    bpLay->setContentsMargins(0, 0, 0, 0);
    bpLay->setSpacing(0);

    // 标题栏
    m_bpTitleBar = new QWidget(m_bpPanel);
    m_bpTitleBar->setObjectName("bpPanelTitleBar");
    m_bpTitleBar->setFixedHeight(30);
    m_bpTitleBar->setCursor(Qt::SizeAllCursor);
    m_bpTitleBar->installEventFilter(this);
    auto* titleLay = new QHBoxLayout(m_bpTitleBar);
    titleLay->setContentsMargins(10, 0, 6, 0);
    auto* titleLbl = new QLabel("关卡蓝图", m_bpTitleBar);
    titleLbl->setObjectName("bpPanelTitle");
    auto* closeBtn = new QToolButton(m_bpTitleBar);
    closeBtn->setText("✕");
    closeBtn->setObjectName("bpPanelClose");
    closeBtn->setFixedSize(22, 22);
    connect(closeBtn, &QToolButton::clicked, m_bpPanel, &QWidget::hide);
    titleLay->addWidget(titleLbl);
    titleLay->addStretch();
    titleLay->addWidget(closeBtn);
    bpLay->addWidget(m_bpTitleBar);

    m_blueprintEditor = new BlueprintEditor(m_bpPanel);
    bpLay->addWidget(m_blueprintEditor, 1);

    connect(m_blueprintEditor, &BlueprintEditor::documentModified, this, [this]() {
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });

    // 嵌入状态下的拖拽句柄（覆盖在 viewStack 顶部，parent = viewportWrap）
    m_bpDockedBar = new QWidget(leftWrap);
    m_bpDockedBar->setObjectName("bpDockedBar");
    m_bpDockedBar->setFixedHeight(30);
    m_bpDockedBar->setCursor(Qt::SizeAllCursor);
    m_bpDockedBar->hide();
    m_bpDockedBar->installEventFilter(this);
    auto* dbLay = new QHBoxLayout(m_bpDockedBar);
    dbLay->setContentsMargins(10, 0, 6, 0);
    auto* dbLbl = new QLabel("关卡蓝图", m_bpDockedBar);
    dbLbl->setObjectName("bpDockedBarTitle");
    dbLay->addWidget(dbLbl);
    dbLay->addStretch();
    auto* dbFloatBtn = new QToolButton(m_bpDockedBar);
    dbFloatBtn->setText("↗");
    dbFloatBtn->setObjectName("bpDockedBarFloat");
    dbFloatBtn->setFixedSize(22, 22);
    dbFloatBtn->setToolTip("浮出为独立窗口");
    dbLay->addWidget(dbFloatBtn);
    // 点击"↗"直接浮出，无需拖拽
    connect(dbFloatBtn, &QToolButton::clicked, this, [this]() {
        if (!m_bpDocked) return;
        // 删除蓝图 tab
        if (m_bpTabIndex >= 0) {
            QSignalBlocker b(m_docTabBar);
            m_docTabBar->removeTab(m_bpTabIndex);
            m_bpTabIndex = -1;
        }
        m_bpDocked = false;
        auto* bpLay = qobject_cast<QVBoxLayout*>(m_bpPanel->layout());
        if (bpLay) bpLay->addWidget(m_blueprintEditor, 1);
        m_blueprintEditor->show();
        if (m_viewStack && m_viewport) m_viewStack->setCurrentWidget(m_viewport);
        m_bpDockedBar->hide();
        // 居中显示浮动面板
        QRect allowed = m_viewportWrap->geometry();
        int x = allowed.left() + qMax(0, (allowed.width()  - m_bpPanel->width())  / 2);
        int y = allowed.top()  + qMax(0, (allowed.height() - m_bpPanel->height()) / 2);
        m_bpPanel->move(mapFromParent(m_viewportWrap->mapToParent(QPoint(x, y))));
        m_bpPanel->show();
        m_bpPanel->raise();
    });

    // 细节面板提前创建，供内容浏览器信号连接
    m_detailsPanel = new DetailsPanel(this);
    m_detailsPanel->setProjectRoot(m_project.path);

    // 内容浏览器：作为 leftWrap 的悬浮子 widget，不加入布局，仅覆盖视口区域
    m_contentBrowserPanel = new QWidget(leftWrap);
    m_contentBrowserPanel->setObjectName("cbPanel");
    auto* cbLay = new QVBoxLayout(m_contentBrowserPanel);
    cbLay->setContentsMargins(0, 0, 0, 0);
    cbLay->setSpacing(0);
    cbLay->addWidget(buildPanelHeader("内容浏览器", m_contentBrowserPanel));
    auto* cb = new ContentBrowser(m_project.path, m_contentBrowserPanel);
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
    m_contentBrowserPanel->hide();

    setCentralWidget(leftWrap);

    // 右侧：大纲 + 细节，使用 QDockWidget 支持浮动、停靠和布局还原
    m_sceneOutliner = new SceneOutliner(this);
    m_outlineDock = new QDockWidget("大纲", this);
    m_outlineDock->setObjectName("outlineDock");
    m_outlineDock->setWidget(m_sceneOutliner);
    addDockWidget(Qt::RightDockWidgetArea, m_outlineDock);
    m_outlineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_outlineDock->setTitleBarWidget(buildPanelHeader("大纲", m_outlineDock));

    m_detailsDock = new QDockWidget("细节", this);
    m_detailsDock->setObjectName("detailsDock");
    m_detailsDock->setWidget(m_detailsPanel);
    splitDockWidget(m_outlineDock, m_detailsDock, Qt::Vertical);
    m_detailsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_detailsDock->setTitleBarWidget(buildPanelHeader("细节", m_detailsDock));
    resizeDocks({m_outlineDock, m_detailsDock}, {300, 300}, Qt::Vertical);
    resizeDocks({m_outlineDock}, {340}, Qt::Horizontal);
}

// 定位嵌入句柄到 viewStack 顶部
void EditorWindow::positionBPDockedBar() {
    if (!m_bpDockedBar || !m_viewStack || !m_viewportWrap) return;
    const QPoint p = m_viewStack->mapTo(m_viewportWrap, QPoint(0, 0));
    m_bpDockedBar->setGeometry(p.x(), p.y(), m_viewportWrap->width(), 30);
    m_bpDockedBar->raise();
}

// 定位覆盖层到 leftWrap 底部
void EditorWindow::positionCBPanel() {
    if (!m_contentBrowserPanel || !m_viewportWrap) return;
    const int h = 220;
    m_contentBrowserPanel->setGeometry(
        0, m_viewportWrap->height() - h,
        m_viewportWrap->width(), h);
}

bool EditorWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_viewportWrap && e->type() == QEvent::Resize) {
        positionCBPanel();
        positionBPDockedBar();
    }

    // 嵌入状态拖拽句柄：拖拽时立即浮出并接管后续拖拽
    if (obj == m_bpDockedBar && m_bpPanel && m_viewportWrap) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton && m_bpDocked) {
                // 删除蓝图 tab
                if (m_bpTabIndex >= 0) {
                    QSignalBlocker b(m_docTabBar);
                    m_docTabBar->removeTab(m_bpTabIndex);
                    m_bpTabIndex = -1;
                }
                m_bpDocked = false;
                // 蓝图编辑器移回 bpPanel
                auto* bpLay = qobject_cast<QVBoxLayout*>(m_bpPanel->layout());
                if (bpLay) bpLay->addWidget(m_blueprintEditor, 1);
                m_blueprintEditor->show();
                if (m_viewStack && m_viewport) m_viewStack->setCurrentWidget(m_viewport);
                m_bpDockedBar->hide();
                // 将 bpPanel 定位到光标处（标题栏随鼠标）
                const QPoint globalCursor = me->globalPosition().toPoint();
                m_bpPanel->move(mapFromGlobal(globalCursor - QPoint(m_bpPanel->width() / 2, 15)));
                m_bpPanel->show();
                m_bpPanel->raise();
                // 启动拖拽
                m_bpDragging   = true;
                m_bpDragOffset = globalCursor - m_bpPanel->mapToGlobal(QPoint(0, 0));
            }
            return true;
        } else if (e->type() == QEvent::MouseMove && m_bpDragging) {
            auto* me = static_cast<QMouseEvent*>(e);
            QPoint newPos = mapFromGlobal(me->globalPosition().toPoint() - m_bpDragOffset);
            QRect allowed = rect();
            int minX = allowed.left(), minY = allowed.top();
            int maxX = allowed.right()  - m_bpPanel->width()  + 1;
            int maxY = allowed.bottom() - m_bpPanel->height() + 1;
            newPos.setX(qBound(minX, newPos.x(), qMax(minX, maxX)));
            newPos.setY(qBound(minY, newPos.y(), qMax(minY, maxY)));
            m_bpPanel->move(newPos);
            // ghost tab 逻辑（与 m_bpTitleBar 相同）
            if (!m_bpDocked && m_docTabBar && centralWidget()) {
                bool inZone = newPos.y() < centralWidget()->geometry().top();
                if (inZone && m_bpGhostTabIndex < 0) {
                    QSignalBlocker b(m_docTabBar);
                    m_bpGhostTabIndex = m_docTabBar->addTab("  关卡蓝图");
                    m_docTabBar->setTabData(m_bpGhostTabIndex, QStringLiteral("blueprint_ghost"));
                } else if (!inZone && m_bpGhostTabIndex >= 0) {
                    QSignalBlocker b(m_docTabBar);
                    m_docTabBar->removeTab(m_bpGhostTabIndex);
                    m_bpGhostTabIndex = -1;
                }
            }
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            if (m_bpDragging) {
                m_bpDragging = false;
                if (!m_bpDocked && m_bpGhostTabIndex >= 0)
                    dockBlueprintAsTab();
            }
            return true;
        }
    }

    // 蓝图面板标题栏拖拽
    if (obj == m_bpTitleBar && m_bpPanel && m_viewportWrap) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                m_bpDragging   = true;
                m_bpDragOffset = me->globalPosition().toPoint()
                                 - m_bpPanel->mapToGlobal(QPoint(0, 0));
            }
        } else if (e->type() == QEvent::MouseMove && m_bpDragging) {
            auto* me = static_cast<QMouseEvent*>(e);
            QPoint newPos = mapFromGlobal(
                me->globalPosition().toPoint() - m_bpDragOffset);
            QRect allowed = rect();
            int minX = allowed.left(), minY = allowed.top();
            int maxX = allowed.right()  - m_bpPanel->width()  + 1;
            int maxY = allowed.bottom() - m_bpPanel->height() + 1;
            newPos.setX(qBound(minX, newPos.x(), qMax(minX, maxX)));
            newPos.setY(qBound(minY, newPos.y(), qMax(minY, maxY)));
            m_bpPanel->move(newPos);

            // 进入/离开展示栏区域时显示/移除幽灵标签页
            if (!m_bpDocked && m_docTabBar && centralWidget()) {
                bool inZone = newPos.y() < centralWidget()->geometry().top();
                if (inZone && m_bpGhostTabIndex < 0) {
                    QSignalBlocker b(m_docTabBar);
                    m_bpGhostTabIndex = m_docTabBar->addTab("  关卡蓝图");
                    m_docTabBar->setTabData(m_bpGhostTabIndex,
                                            QStringLiteral("blueprint_ghost"));
                } else if (!inZone && m_bpGhostTabIndex >= 0) {
                    QSignalBlocker b(m_docTabBar);
                    m_docTabBar->removeTab(m_bpGhostTabIndex);
                    m_bpGhostTabIndex = -1;
                }
            }
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            if (m_bpDragging) {
                m_bpDragging = false;
                if (!m_bpDocked && m_bpGhostTabIndex >= 0)
                    dockBlueprintAsTab();  // 幽灵标签 → 真实标签
            }
        }
    }

    return QMainWindow::eventFilter(obj, e);
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
        hl->addSpacing(4);
        hl->addWidget(f);
        hl->addSpacing(4);
    };
    auto drop = [&](const QString& t) {
        auto* b = new QPushButton(t, bar);
        b->setObjectName("vpDropBtn");
        hl->addWidget(b);
    };

    // 4 个互斥工具按钮
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
    vBtn("⊠","显示选项"); vBtn("⚙","视口设置");
    sep();
    auto* bpBtn = vBtn("关卡蓝图", "打开关卡蓝图（可视化脚本）");
    connect(bpBtn, &QToolButton::clicked, this, [this]() {
        // 已停靠为标签页：切换到蓝图标签
        if (m_bpDocked) {
            if (m_bpTabIndex >= 0) m_docTabBar->setCurrentIndex(m_bpTabIndex);
            return;
        }
        if (!m_bpPanel) return;
        if (m_bpPanel->isVisible()) {
            m_bpPanel->hide();
        } else {
            int idx = m_docTabBar->currentIndex();
            QString path = idx >= 0 ? m_docTabBar->tabData(idx).toString() : QString{};
            if (m_blueprintEditor)
                m_blueprintEditor->loadLevel(m_openLevels.value(path, nullptr));
            // 居中显示（在整个窗口范围）
            QRect allowed = rect();
            int x = allowed.left() + qMax(0, (allowed.width()  - m_bpPanel->width())  / 2);
            int y = allowed.top()  + qMax(0, (allowed.height() - m_bpPanel->height()) / 2);
            m_bpPanel->move(x, y);
            m_bpPanel->show();
            m_bpPanel->raise();
        }
    });
    hl->addStretch();
    auto* zl = new QLabel("1×", bar); zl->setObjectName("vpZoomLabel");
    hl->addWidget(zl);
    sep();
    vBtn("☀","光照");

    return bar;
}

// ── 辅助：面板标题头 ─────────────────────────────────────────────────
QWidget* EditorWindow::buildPanelHeader(const QString& title, QWidget* parent) {
    auto* hdr = new QWidget(parent);
    hdr->setObjectName("panelHeader");
    hdr->setFixedHeight(26);
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(8, 0, 4, 0);
    auto* lbl = new QLabel(title, hdr);
    lbl->setObjectName("panelHeaderTitle");
    auto* closeBtn = new QToolButton(hdr);
    closeBtn->setText("×");
    closeBtn->setObjectName("panelCloseBtn");
    connect(closeBtn, &QToolButton::clicked, parent, &QWidget::hide);
    hl->addWidget(lbl);
    hl->addStretch();
    hl->addWidget(closeBtn);
    return hdr;
}

// ── 5. 底部极细状态栏 ─────────────────────────────────────────────────
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

    // 内容浏览器 toggle 按钮
    auto* cbToggle = new QPushButton("内容浏览器", bar);
    cbToggle->setObjectName("statusBtn");
    cbToggle->setCheckable(true);
    hl->addWidget(cbToggle);
    connect(cbToggle, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            positionCBPanel();
            m_contentBrowserPanel->show();
            m_contentBrowserPanel->raise();
        } else {
            m_contentBrowserPanel->hide();
        }
    });

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

    // 断开上一个 Tab 的所有信号连接
    for (auto& conn : m_tabConnections) disconnect(conn);
    m_tabConnections.clear();

    const QString path = m_docTabBar->tabData(index).toString();

    // 蓝图标签页（包含拖拽中的幽灵标签）
    if (path == QLatin1String("blueprint") || path == QLatin1String("blueprint_ghost")) {
        if (path == QLatin1String("blueprint") && m_viewStack && m_blueprintEditor)
            m_viewStack->setCurrentWidget(m_blueprintEditor);
        return;
    }

    // 切换到关卡标签时，确保 viewStack 显示视口
    if (m_viewStack && m_viewport)
        m_viewStack->setCurrentWidget(m_viewport);

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
    if (m_bpPanel && m_bpPanel->isVisible() && m_blueprintEditor)
        m_blueprintEditor->loadLevel(doc);
    m_detailsPanel->clearActor();

    // ① 大纲选中 → 细节面板 + 视口高亮
    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::actorSelected,
                                this, [this](const ActorData& a) {
        m_detailsPanel->showActor(a);
        if (m_viewport) m_viewport->setSelectedId(a.id);
    });

    if (m_viewport) {
        // ② 视口点击选中 → 细节面板
        m_tabConnections << connect(m_viewport, &Viewport2D::actorSelected,
                                    this, [this](const ActorData& a) {
            m_detailsPanel->showActor(a);
        });

        // ② 视口拖动中 → 细节面板实时更新
        m_tabConnections << connect(m_viewport, &Viewport2D::actorDragging,
                                    m_detailsPanel, &DetailsPanel::showActor);

        // ③ 视口拖拽结束 → 细节面板同步坐标 + 标记脏
        m_tabConnections << connect(m_viewport, &Viewport2D::actorTransformed,
                                    this, [this](const ActorData& a) {
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
        });

        // ④ 视口右键创建 → 大纲刷新 + 细节面板显示新 Actor + 标记脏
        m_tabConnections << connect(m_viewport, &Viewport2D::actorCreated,
                                    this, [this, doc](const ActorData& a) {
            m_sceneOutliner->loadLevel(doc);
            m_detailsPanel->showActor(a);
            updateTabTitle(m_docTabBar->currentIndex());
            updateSaveLabel();
        });
    }

    // ⑤ 细节面板改动 → 写入 doc + 刷新视口 + 大纲 + 标记脏
    m_tabConnections << connect(m_detailsPanel, &DetailsPanel::actorModified,
                                this, [this, doc](const ActorData& a) {
        doc->updateActor(a);
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
        if (m_viewport) m_viewport->update();
        m_sceneOutliner->loadLevel(doc);  // 名称可能变了，重建树
    });

    // ⑥ 大纲删除 → 清空细节面板 + 视口取消选中
    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::actorRemoved,
                                this, [this](const QString&) {
        m_detailsPanel->clearActor();
        if (m_viewport) m_viewport->setSelectedId({});
    });

    // ⑦ 大纲增删改 → 视口刷新 + 标记脏
    m_tabConnections << connect(m_sceneOutliner, &SceneOutliner::levelChanged,
                                this, [this]() {
        if (m_viewport) m_viewport->update();
        updateTabTitle(m_docTabBar->currentIndex());
        updateSaveLabel();
    });
}

void EditorWindow::onTabClosed(int index) {
    const QString path = m_docTabBar->tabData(index).toString();

    // 幽灵标签关闭（拖拽中意外触发）
    if (path == QLatin1String("blueprint_ghost")) {
        { QSignalBlocker b(m_docTabBar); m_docTabBar->removeTab(index); }
        m_bpGhostTabIndex = -1;
        return;
    }

    // 蓝图标签关闭 → undock 回浮动面板
    if (path == QLatin1String("blueprint")) {
        undockBlueprintFromTab();
        {
            QSignalBlocker b(m_docTabBar);
            m_docTabBar->removeTab(index);
        }
        // 切回当前关卡 tab（若有）
        if (m_docTabBar->count() > 0)
            onTabChanged(m_docTabBar->currentIndex());
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

    if (!path.isEmpty())
        delete m_openLevels.take(path);

    for (auto& conn : m_tabConnections) disconnect(conn);
    m_tabConnections.clear();

    // removeTab 会触发 currentChanged → onTabChanged 加载剩余 tab
    m_docTabBar->removeTab(index);

    // 只有关闭最后一个 tab 时才清空视口
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
    // 先阻断信号，等 tabData 设好后再触发 onTabChanged，
    // 否则 addTab 会立即 emit currentChanged 但此时 tabData 还是空的
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(path).baseName());
        m_docTabBar->setTabData(idx, path);
    }
    if (m_docTabBar->currentIndex() == idx)
        onTabChanged(idx);          // 已是当前 tab，手动触发
    else
        m_docTabBar->setCurrentIndex(idx);  // 切换会自动 emit currentChanged
}

void EditorWindow::closeEvent(QCloseEvent* e) {
    // 检查所有未保存的关卡
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

    // 蓝图停靠 Tab：从当前加载的关卡 doc 取脏状态
    if (path == QLatin1String("blueprint")) {
        // 找到蓝图编辑器对应的关卡 doc
        bool dirty = false;
        for (LevelDocument* doc : m_openLevels) {
            if (doc && doc->isDirty()) { dirty = true; break; }
        }
        m_docTabBar->setTabText(index, dirty ? "● 关卡蓝图" : "  关卡蓝图");
        return;
    }

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

    // 按键事件：每次 stateChanged 同步 actors + 新增打印条目
    connect(m_runtime, &BPRuntime::stateChanged, this, [this]() {
        if (!m_runtime || !m_viewport) return;
        m_viewport->updateRuntimeActors(m_runtime->actors());
        m_viewport->syncPrintLog(m_runtime->printLog());
    });

    connect(m_viewport, &Viewport2D::keyPressed, m_runtime, &BPRuntime::triggerKeyDown);

    m_viewport->setRuntimeMode(true, m_runtime->actors());
    m_runtime->triggerBeginPlay();

    // triggerBeginPlay 后直接强制同步一次（防止 stateChanged 时序问题）
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


void EditorWindow::dockBlueprintAsTab() {
    if (m_bpDocked || !m_blueprintEditor || !m_viewStack) return;
    m_bpDocked = true;

    // 加载当前关卡蓝图（跳过 ghost 占位数据）
    const int idx = m_docTabBar->currentIndex();
    const QString path = idx >= 0 ? m_docTabBar->tabData(idx).toString() : QString{};
    if (path != QLatin1String("blueprint") && path != QLatin1String("blueprint_ghost"))
        m_blueprintEditor->loadLevel(m_openLevels.value(path, nullptr));

    // 将蓝图编辑器移入 viewStack（自动 re-parent）
    m_viewStack->addWidget(m_blueprintEditor);

    // 幽灵标签已存在 → 直接转为真实标签；否则新建
    if (m_bpGhostTabIndex >= 0) {
        QSignalBlocker b(m_docTabBar);
        m_docTabBar->setTabData(m_bpGhostTabIndex, QStringLiteral("blueprint"));
        m_bpTabIndex      = m_bpGhostTabIndex;
        m_bpGhostTabIndex = -1;
    } else {
        QSignalBlocker b(m_docTabBar);
        m_bpTabIndex = m_docTabBar->addTab("  关卡蓝图");
        m_docTabBar->setTabData(m_bpTabIndex, QStringLiteral("blueprint"));
    }

    m_bpPanel->hide();
    m_docTabBar->setCurrentIndex(m_bpTabIndex);
    positionBPDockedBar();
    if (m_bpDockedBar) { m_bpDockedBar->show(); m_bpDockedBar->raise(); }
}


void EditorWindow::undockBlueprintFromTab() {
    if (!m_bpDocked || !m_blueprintEditor) return;
    m_bpDocked = false;

    // 将蓝图编辑器移回 bpPanel 布局
    auto* bpLay = qobject_cast<QVBoxLayout*>(m_bpPanel->layout());
    if (bpLay) bpLay->addWidget(m_blueprintEditor, 1);
    m_blueprintEditor->show(); // 抵消 QStackedLayout::takeAt() 的隐式 hide()

    // viewStack 切回视口
    if (m_viewStack && m_viewport)
        m_viewStack->setCurrentWidget(m_viewport);

    m_bpPanel->hide(); // 关闭 tab = 关闭蓝图，不重新浮出
    if (m_bpDockedBar) m_bpDockedBar->hide();
    m_bpTabIndex = -1;
}

