#include "DetailsPanel.h"
#include "models/ActorTypeUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QFrame>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QSignalBlocker>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QColorDialog>
#include <QFileInfo>
#include <QDialog>
#include <QDirIterator>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDataStream>
#include <QMimeData>

QColor DetailsPanel::typeColor(const QString& type) {
    return actorTypeColor(type);
}

// ── 构造 ──────────────────────────────────────────────────────────────

DetailsPanel::DetailsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("detailsPanel");

    auto* stack = new QStackedWidget(this);

    // 第 0 页：空状态
    auto* emptyPage = new QWidget(stack);
    auto* elay = new QVBoxLayout(emptyPage);
    auto* hint = new QLabel("请在大纲或视口中\n选中一个 Actor", emptyPage);
    hint->setObjectName("detailEmptyHint");
    hint->setAlignment(Qt::AlignCenter);
    elay->addWidget(hint);
    stack->addWidget(emptyPage);

    // 第 1 页：Inspector 内容
    auto* scroll = new QScrollArea(stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("detailScroll");

    auto* content = new QWidget(scroll);
    content->setObjectName("detailContent");
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildHeader(root);
    buildComponents(root);
    buildTransform(root);
    buildSpriteRenderer(root);
    buildCameraComponent(root);
    buildFollowControl(root);
    buildConfiner(root);
    root->addStretch();

    scroll->setWidget(content);
    stack->addWidget(scroll);

    m_stack = stack;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(stack);

    // 初始显示空状态
    stack->setCurrentIndex(0);
    setAcceptDrops(true);
}

// ── 顶部身份区 ────────────────────────────────────────────────────────

void DetailsPanel::buildHeader(QVBoxLayout* root) {
    auto* headerWrap = new QWidget;
    headerWrap->setObjectName("detailHeader");
    auto* vl = new QVBoxLayout(headerWrap);
    vl->setContentsMargins(8, 8, 8, 6);
    vl->setSpacing(4);

    // Row 1: [图标] [✓] [名称输入框] [□ 静态]
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(6);

    m_iconLabel = new QLabel(headerWrap);
    m_iconLabel->setFixedSize(18, 18);
    m_iconLabel->setObjectName("actorIconLabel");

    m_activeCheck = new QCheckBox(headerWrap);
    m_activeCheck->setChecked(true);
    m_activeCheck->setObjectName("actorActiveCheck");

    m_nameEdit = new QLineEdit(headerWrap);
    m_nameEdit->setObjectName("actorNameEdit");
    m_nameEdit->setPlaceholderText("Actor 名称");

    m_addComponentBtn = new QPushButton("＋ 添加", headerWrap);
    m_addComponentBtn->setObjectName("addComponentBtn");
    m_addComponentBtn->setFixedHeight(22);
    connect(m_addComponentBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentActor.id.isEmpty()) return;
        QMenu menu(this);
        const QStringList all = {"变换", "精灵渲染器", "摄像机组件", "跟随控制组件",
                                  "边界限制组件", "点光源", "碰撞盒", "刚体", "音频源", "动画控制器"};
        for (const QString& comp : all) {
            if (!m_currentActor.components.contains(comp))
                menu.addAction(comp, [this, comp]() { onAddComponent(comp); });
        }
        if (!menu.isEmpty())
            menu.exec(m_addComponentBtn->mapToGlobal(
                QPoint(0, m_addComponentBtn->height())));
    });

    m_staticCheck = new QCheckBox(headerWrap);
    m_staticCheck->setObjectName("actorStaticCheck");
    auto* staticLbl = new QLabel("静态", headerWrap);
    staticLbl->setObjectName("detailSmallLabel");

    row1->addWidget(m_iconLabel);
    row1->addWidget(m_activeCheck);
    row1->addWidget(m_nameEdit, 1);
    row1->addWidget(m_addComponentBtn);
    row1->addWidget(m_staticCheck);
    row1->addWidget(staticLbl);
    vl->addLayout(row1);

    // Row 2: 标签 [combo] | 图层 [combo]
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(6);

    auto* tagLbl = new QLabel("标签", headerWrap);
    tagLbl->setObjectName("detailSmallLabel");
    m_tagCombo = new QComboBox(headerWrap);
    m_tagCombo->setObjectName("detailCombo");
    m_tagCombo->addItems({"未标记", "玩家", "敌人", "地面", "障碍", "拾取物"});

    auto* layerLbl = new QLabel("图层", headerWrap);
    layerLbl->setObjectName("detailSmallLabel");
    m_layerCombo = new QComboBox(headerWrap);
    m_layerCombo->setObjectName("detailCombo");
    m_layerCombo->addItems({"默认", "背景", "前景", "界面", "物理"});

    row2->addWidget(tagLbl);
    row2->addWidget(m_tagCombo, 1);
    row2->addSpacing(8);
    row2->addWidget(layerLbl);
    row2->addWidget(m_layerCombo, 1);
    vl->addLayout(row2);

    // Row 3: 编辑蓝图按钮（仅在 bpClass 非 builtin 时显示）
    m_editBpBtn = new QPushButton("编辑蓝图", headerWrap);
    m_editBpBtn->setObjectName("editBpBtn");
    m_editBpBtn->setFixedHeight(22);
    m_editBpBtn->setVisible(false);
    connect(m_editBpBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentActor.bpClass.isEmpty() && !m_currentActor.bpClass.startsWith("builtin/"))
            emit editBpClassRequested(m_currentActor.bpClass);
    });
    vl->addWidget(m_editBpBtn);

    root->addWidget(headerWrap);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("detailSeparator");
    root->addWidget(sep);

    connect(m_activeCheck, &QCheckBox::toggled,            this, &DetailsPanel::onAnyFieldChanged);
    connect(m_staticCheck, &QCheckBox::toggled,            this, &DetailsPanel::onAnyFieldChanged);
    connect(m_nameEdit,    &QLineEdit::textEdited,          this, &DetailsPanel::onAnyFieldChanged);
    connect(m_tagCombo,    &QComboBox::currentTextChanged,  this, &DetailsPanel::onAnyFieldChanged);
    connect(m_layerCombo,  &QComboBox::currentTextChanged,  this, &DetailsPanel::onAnyFieldChanged);
}

// ── 组件列表区 ────────────────────────────────────────────────────────

void DetailsPanel::buildComponents(QVBoxLayout* root) {
    m_componentTree = new QTreeWidget;
    m_componentTree->setObjectName("componentTree");
    m_componentTree->setHeaderHidden(true);
    m_componentTree->setColumnCount(1);
    m_componentTree->setIndentation(16);
    m_componentTree->setRootIsDecorated(true);
    m_componentTree->setFrameShape(QFrame::NoFrame);
    m_componentTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_componentTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_componentTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_componentTree->setFocusPolicy(Qt::StrongFocus);

    // 右键删除组件
    connect(m_componentTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_componentTree->itemAt(pos);
        if (!item || !item->parent()) return;
        const QString comp = item->data(0, Qt::UserRole).toString();
        if (comp.isEmpty()) return;
        QMenu menu(this);
        menu.addAction("移除组件", [this, comp]() {
            m_currentActor.components.removeAll(comp);
            refreshComponentList();
            emit actorModified(m_currentActor);
        });
        menu.exec(m_componentTree->viewport()->mapToGlobal(pos));
    });

    root->addWidget(m_componentTree);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("detailSeparator");
    root->addWidget(sep);
}

void DetailsPanel::refreshComponentList() {
    m_componentTree->clear();

    // 根节点 = Actor 本身（不可选中）
    const QString rootName = m_currentActor.name.isEmpty()
                             ? "(实例)" : m_currentActor.name + "  (实例)";
    auto* root = new QTreeWidgetItem(QStringList{rootName});
    root->setFlags(root->flags() & ~Qt::ItemIsSelectable);
    QFont f = root->font(0); f.setBold(true); root->setFont(0, f);
    m_componentTree->addTopLevelItem(root);

    // 所有组件（含变换）统一渲染，均可右键删除
    for (const QString& comp : m_currentActor.components) {
        auto* child = new QTreeWidgetItem(QStringList{comp});
        child->setData(0, Qt::UserRole, comp);
        root->addChild(child);
    }
    root->setExpanded(true);

    // 每行 22px，最少显示 3 行（根节点 + 至少两行内容空间）
    const int rows = qMax(3, 1 + m_currentActor.components.size());
    m_componentTree->setFixedHeight(rows * 22 + 6);

    refreshSpriteSection();
    refreshCameraSections();
}

void DetailsPanel::onAddComponent(const QString& compName) {
    if (m_currentActor.id.isEmpty()) return;
    if (!m_currentActor.components.contains(compName)) {
        m_currentActor.components.append(compName);
        refreshComponentList();
        emit actorModified(m_currentActor);
    }
}

// ── 变换区 ────────────────────────────────────────────────────────────

void DetailsPanel::buildTransform(QVBoxLayout* root) {
    auto* box = new QGroupBox(this);
    box->setObjectName("detailGroup");
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(0);
    boxLayout->setContentsMargins(0, 0, 0, 0);

    // 标题行
    auto* titleBar = new QWidget(box);
    titleBar->setObjectName("detailTitleBar");
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(8, 6, 8, 6);
    auto* triBtn = new QToolButton(box);
    triBtn->setText("▼"); triBtn->setCheckable(true); triBtn->setChecked(true);
    triBtn->setObjectName("detailTriangle"); triBtn->setAutoRaise(true);
    auto* titleLbl = new QLabel("变换", box); titleLbl->setObjectName("detailGroupTitle");
    QFont f = titleLbl->font(); f.setBold(true); titleLbl->setFont(f);
    titleRow->addWidget(triBtn);
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    boxLayout->addWidget(titleBar);

    // 内容区域
    auto* content = new QWidget(box);
    auto* grid = new QGridLayout(content);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 4, 8, 10);
    grid->setColumnMinimumWidth(0, 50);
    boxLayout->addWidget(content);

    auto mkSpin = [&](double lo, double hi, double val) -> QDoubleSpinBox* {
        auto* s = new QDoubleSpinBox(content);
        s->setRange(lo, hi); s->setValue(val);
        s->setDecimals(2); s->setObjectName("detailSpin");
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &DetailsPanel::onAnyFieldChanged);
        return s;
    };

    // 位置
    grid->addWidget(new QLabel("位置", content), 0, 0);
    auto* posRow = new QHBoxLayout;
    auto* lx = new QLabel("X", content); lx->setObjectName("axisLabel");
    m_posX = mkSpin(-99999, 99999, 0);
    auto* ly = new QLabel("Y", content); ly->setObjectName("axisLabel");
    m_posY = mkSpin(-99999, 99999, 0);
    posRow->addWidget(lx); posRow->addWidget(m_posX, 1);
    posRow->addWidget(ly); posRow->addWidget(m_posY, 1);
    grid->addLayout(posRow, 0, 1, 1, 2);

    // 旋转
    grid->addWidget(new QLabel("旋转", content), 1, 0);
    m_rotation = mkSpin(-360, 360, 0);
    grid->addWidget(m_rotation, 1, 1, 1, 2);

    // 缩放
    grid->addWidget(new QLabel("缩放", content), 2, 0);
    auto* scaleRow = new QHBoxLayout;
    auto* sx = new QLabel("X", content); sx->setObjectName("axisLabel");
    m_scaleX = mkSpin(-99999, 99999, 1);
    auto* sy = new QLabel("Y", content); sy->setObjectName("axisLabel");
    m_scaleY = mkSpin(-99999, 99999, 1);
    scaleRow->addWidget(sx); scaleRow->addWidget(m_scaleX, 1);
    scaleRow->addWidget(sy); scaleRow->addWidget(m_scaleY, 1);
    grid->addLayout(scaleRow, 2, 1, 1, 2);

    connect(triBtn, &QToolButton::toggled, this, [content, triBtn](bool expanded) {
        content->setVisible(expanded);
        triBtn->setText(expanded ? "▼" : "▶");
    });

    root->addWidget(box);
}

// ── 外部接口 ──────────────────────────────────────────────────────────

void DetailsPanel::showActor(const ActorData& actor) {
    QSignalBlocker b1(m_activeCheck), b2(m_staticCheck), b3(m_nameEdit),
                   b4(m_tagCombo),    b5(m_layerCombo),
                   b6(m_posX),  b7(m_posY), b8(m_rotation),
                   b9(m_scaleX), b10(m_scaleY),
                   b11(*m_flipXCheck), b12(*m_flipYCheck),
                   b13(*m_sortLayerCombo), b14(*m_orderSpin), b15(*m_drawModeCombo),
                   b16(m_cameraOrthoCheck), b17(m_cameraSizeSpin),
                   b18(*m_cameraIsMainCheck), b18b(*m_cameraResWSpin), b18c(*m_cameraResHSpin),
                   b19(m_followTargetEdit), b20(m_followLerpSpin),
                   b21(m_followOffsetXSpin), b22(m_followOffsetYSpin),
                   b23(m_confinerEnabledChk), b23b(m_confinerActorEdit),
                   b24(m_confinerMinXSpin), b25(m_confinerMaxXSpin),
                   b26(m_confinerMinYSpin), b27(m_confinerMaxYSpin);

    m_currentActor = actor;
    // 迁移：旧 Actor JSON 中 components 为空时补入默认组件并立即保存
    const bool needsMigration = m_currentActor.components.isEmpty();
    if (needsMigration)
        m_currentActor.components = defaultComponentsForBpClass(m_currentActor.bpClass);

    QPixmap px(18, 18);
    px.fill(bpClassColor(actor.bpClass));
    m_iconLabel->setPixmap(px);

    m_activeCheck->setChecked(actor.active);
    m_nameEdit->setText(actor.name);
    m_staticCheck->setChecked(actor.isStatic);

    int tagIdx = m_tagCombo->findText(actor.tag);
    m_tagCombo->setCurrentIndex(tagIdx >= 0 ? tagIdx : 0);

    int layerIdx = m_layerCombo->findText(actor.layer);
    m_layerCombo->setCurrentIndex(layerIdx >= 0 ? layerIdx : 0);

    m_posX->setValue(actor.x);
    m_posY->setValue(actor.y);
    m_rotation->setValue(actor.rotation);
    m_scaleX->setValue(actor.scaleX);
    m_scaleY->setValue(actor.scaleY);

    // 精灵渲染器字段
    m_spritePathLabel->setText(actor.spritePath.isEmpty() ? "(无图片)"
                               : QFileInfo(actor.spritePath).fileName());
    m_spriteColorBtn->setStyleSheet(
        QString("background:%1; border:1px solid #555;")
        .arg(actor.spriteColor.name(QColor::HexArgb)));
    m_flipXCheck->setChecked(actor.flipX);
    m_flipYCheck->setChecked(actor.flipY);
    {
        int si = m_sortLayerCombo->findText(actor.sortingLayer);
        m_sortLayerCombo->setCurrentIndex(si >= 0 ? si : 1);
    }
    m_orderSpin->setValue(actor.orderInLayer);
    {
        int di = m_drawModeCombo->findText(actor.drawMode);
        m_drawModeCombo->setCurrentIndex(di >= 0 ? di : 0);
    }

    // 摄像机组件字段
    m_cameraIsMainCheck->setChecked(actor.cameraIsMain);
    m_cameraOrthoCheck->setChecked(actor.cameraOrthographic);
    m_cameraSizeSpin->setValue(actor.cameraSize);
    m_cameraResWSpin->setValue(actor.cameraResW);
    m_cameraResHSpin->setValue(actor.cameraResH);
    m_cameraBgBtn->setStyleSheet(
        QString("background:%1; border:1px solid #555;")
        .arg(actor.cameraBackground.name(QColor::HexArgb)));

    // 跟随控制组件字段
    m_followTargetEdit->setText(actor.followTarget);
    m_followLerpSpin->setValue(actor.followLerpSpeed);
    m_followOffsetXSpin->setValue(actor.followOffsetX);
    m_followOffsetYSpin->setValue(actor.followOffsetY);

    // 边界限制组件字段
    m_confinerEnabledChk->setChecked(actor.confinerEnabled);
    m_confinerActorEdit->setText(actor.confinerActor);
    m_confinerMinXSpin->setValue(actor.confinerMinX);
    m_confinerMaxXSpin->setValue(actor.confinerMaxX);
    m_confinerMinYSpin->setValue(actor.confinerMinY);
    m_confinerMaxYSpin->setValue(actor.confinerMaxY);

    m_editBpBtn->setVisible(!actor.bpClass.isEmpty() && !actor.bpClass.startsWith("builtin/"));

    refreshComponentList();
    m_stack->setCurrentIndex(1);  // 显示 Inspector 内容
    if (needsMigration)
        emit actorModified(m_currentActor);
}

void DetailsPanel::clearActor() {
    m_currentActor = ActorData{};
    m_stack->setCurrentIndex(0);  // 显示空状态
}

void DetailsPanel::setProjectRoot(const QString& root) {
    m_projectRoot = root;
}

void DetailsPanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist"))
        e->acceptProposedAction();
}

void DetailsPanel::dropEvent(QDropEvent* e) {
    const QByteArray encoded =
        e->mimeData()->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(encoded);
    while (!stream.atEnd()) {
        int row, col;
        QMap<int, QVariant> d;
        stream >> row >> col >> d;
        if (d.value(Qt::UserRole).toString() == "image") {
            assignSpritePath(d.value(Qt::UserRole + 1).toString());
            e->acceptProposedAction();
            return;
        }
    }
}

void DetailsPanel::assignSpritePath(const QString& path) {
    if (m_currentActor.id.isEmpty()) return;
    if (!m_currentActor.components.contains("精灵渲染器")) return;
    m_currentActor.spritePath = path;
    m_spritePathLabel->setText(QFileInfo(path).fileName());
    emit actorModified(m_currentActor);
}

void DetailsPanel::refreshSpriteSection() {
    if (m_spriteBox)
        m_spriteBox->setVisible(m_currentActor.components.contains("精灵渲染器"));
}

void DetailsPanel::refreshCameraSections() {
    if (m_cameraBox)
        m_cameraBox->setVisible(m_currentActor.components.contains("摄像机组件"));
    if (m_followBox)
        m_followBox->setVisible(m_currentActor.components.contains("跟随控制组件"));
    if (m_confinerBox)
        m_confinerBox->setVisible(m_currentActor.components.contains("边界限制组件"));
}

// ── 辅助宏：构建折叠区块标题行 ────────────────────────────────────────
static std::tuple<QWidget*, QWidget*> makeSectionBox(QWidget* parent,
                                                      QVBoxLayout** boxLayout,
                                                      const QString& title) {
    auto* box = new QGroupBox(parent);
    box->setObjectName("detailGroup");
    auto* bl = new QVBoxLayout(box);
    bl->setSpacing(0);
    bl->setContentsMargins(0, 0, 0, 0);
    *boxLayout = bl;

    auto* titleBar = new QWidget(box);
    titleBar->setObjectName("detailTitleBar");
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(8, 6, 8, 6);

    auto* triBtn = new QToolButton(box);
    triBtn->setText("▼"); triBtn->setCheckable(true); triBtn->setChecked(true);
    triBtn->setObjectName("detailTriangle"); triBtn->setAutoRaise(true);

    auto* titleLbl = new QLabel(title, box);
    titleLbl->setObjectName("detailGroupTitle");
    QFont f = titleLbl->font(); f.setBold(true); titleLbl->setFont(f);
    titleRow->addWidget(triBtn);
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    bl->addWidget(titleBar);

    auto* content = new QWidget(box);
    bl->addWidget(content);

    QObject::connect(triBtn, &QToolButton::toggled, content, [content, triBtn](bool expanded) {
        content->setVisible(expanded);
        triBtn->setText(expanded ? "▼" : "▶");
    });

    return {box, content};
}

// ── 摄像机组件 ─────────────────────────────────────────────────────────

void DetailsPanel::buildCameraComponent(QVBoxLayout* root) {
    QVBoxLayout* bl = nullptr;
    auto [box_sb, content_sb] = makeSectionBox(this, &bl, "摄像机组件");
    QWidget* box = box_sb; QWidget* content = content_sb;

    auto* grid = new QGridLayout(content);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 4, 8, 10);
    grid->setColumnMinimumWidth(0, 60);

    auto mkDSpin = [&](double lo, double hi, double val, double step = 0.1) -> QDoubleSpinBox* {
        auto* s = new QDoubleSpinBox(content);
        s->setRange(lo, hi); s->setValue(val); s->setSingleStep(step);
        s->setDecimals(3); s->setObjectName("detailSpin");
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &DetailsPanel::onAnyFieldChanged);
        return s;
    };

    // 行0：主摄像机
    grid->addWidget(new QLabel("主摄像机", content), 0, 0);
    m_cameraIsMainCheck = new QCheckBox(content);
    m_cameraIsMainCheck->setObjectName("detailCheck");
    connect(m_cameraIsMainCheck, &QCheckBox::toggled, this, &DetailsPanel::onAnyFieldChanged);
    grid->addWidget(m_cameraIsMainCheck, 0, 1, 1, 2);

    // 行1：投影模式
    grid->addWidget(new QLabel("投影模式", content), 1, 0);
    m_cameraOrthoCheck = new QCheckBox("正交", content);
    m_cameraOrthoCheck->setObjectName("detailCheck");
    m_cameraOrthoCheck->setChecked(true);
    connect(m_cameraOrthoCheck, &QCheckBox::toggled, this, &DetailsPanel::onAnyFieldChanged);
    grid->addWidget(m_cameraOrthoCheck, 1, 1, 1, 2);

    // 行2：大小
    grid->addWidget(new QLabel("大小", content), 2, 0);
    m_cameraSizeSpin = mkDSpin(0.1, 9999, 540.0, 5.0);
    grid->addWidget(m_cameraSizeSpin, 2, 1, 1, 2);

    // 行3：分辨率
    grid->addWidget(new QLabel("分辨率", content), 3, 0);
    auto* resRow = new QHBoxLayout;
    m_cameraResWSpin = new QSpinBox(content);
    m_cameraResWSpin->setObjectName("detailSpin");
    m_cameraResWSpin->setRange(1, 7680);
    m_cameraResWSpin->setValue(1920);
    connect(m_cameraResWSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DetailsPanel::onAnyFieldChanged);
    auto* resX = new QLabel("×", content);
    resX->setObjectName("detailSmallLabel");
    m_cameraResHSpin = new QSpinBox(content);
    m_cameraResHSpin->setObjectName("detailSpin");
    m_cameraResHSpin->setRange(1, 4320);
    m_cameraResHSpin->setValue(1080);
    connect(m_cameraResHSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DetailsPanel::onAnyFieldChanged);
    resRow->addWidget(m_cameraResWSpin, 1);
    resRow->addWidget(resX);
    resRow->addWidget(m_cameraResHSpin, 1);
    grid->addLayout(resRow, 3, 1, 1, 2);

    // 行4：背景色
    grid->addWidget(new QLabel("背景色", content), 4, 0);
    m_cameraBgBtn = new QPushButton(content);
    m_cameraBgBtn->setObjectName("spriteColorBtn");
    m_cameraBgBtn->setFixedWidth(70);
    m_cameraBgBtn->setFixedHeight(22);
    m_cameraBgBtn->setStyleSheet("background:#ff1e1e1e; border:1px solid #555;");
    grid->addWidget(m_cameraBgBtn, 4, 1, 1, 2);

    connect(m_cameraBgBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_currentActor.cameraBackground, this,
            "摄像机背景色", QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        m_currentActor.cameraBackground = c;
        m_cameraBgBtn->setStyleSheet(
            QString("background:%1; border:1px solid #555;").arg(c.name(QColor::HexArgb)));
        emit actorModified(m_currentActor);
    });

    m_cameraBox = box;
    root->addWidget(box);
    box->hide();
}

// ── 跟随控制组件 ───────────────────────────────────────────────────────

void DetailsPanel::buildFollowControl(QVBoxLayout* root) {
    QVBoxLayout* bl = nullptr;
    auto [box_sb, content_sb] = makeSectionBox(this, &bl, "跟随控制组件");
    QWidget* box = box_sb; QWidget* content = content_sb;

    auto* grid = new QGridLayout(content);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 4, 8, 10);
    grid->setColumnMinimumWidth(0, 60);

    auto mkDSpin = [&](double lo, double hi, double val, double step = 0.1) -> QDoubleSpinBox* {
        auto* s = new QDoubleSpinBox(content);
        s->setRange(lo, hi); s->setValue(val); s->setSingleStep(step);
        s->setDecimals(2); s->setObjectName("detailSpin");
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &DetailsPanel::onAnyFieldChanged);
        return s;
    };

    // 行0：目标 Actor
    grid->addWidget(new QLabel("目标", content), 0, 0);
    m_followTargetEdit = new QLineEdit(content);
    m_followTargetEdit->setObjectName("actorNameEdit");
    m_followTargetEdit->setPlaceholderText("Actor 名称");
    connect(m_followTargetEdit, &QLineEdit::textEdited, this, &DetailsPanel::onAnyFieldChanged);
    grid->addWidget(m_followTargetEdit, 0, 1, 1, 2);

    // 行1：平滑速度
    grid->addWidget(new QLabel("平滑速度", content), 1, 0);
    m_followLerpSpin = mkDSpin(0.0, 50.0, 5.0, 0.5);
    grid->addWidget(m_followLerpSpin, 1, 1, 1, 2);

    // 行2：偏移 X/Y
    grid->addWidget(new QLabel("偏移", content), 2, 0);
    auto* offsetRow = new QHBoxLayout;
    auto* lx = new QLabel("X", content); lx->setObjectName("axisLabel");
    m_followOffsetXSpin = mkDSpin(-9999, 9999, 0.0, 1.0);
    auto* ly = new QLabel("Y", content); ly->setObjectName("axisLabel");
    m_followOffsetYSpin = mkDSpin(-9999, 9999, 0.0, 1.0);
    offsetRow->addWidget(lx); offsetRow->addWidget(m_followOffsetXSpin, 1);
    offsetRow->addWidget(ly); offsetRow->addWidget(m_followOffsetYSpin, 1);
    grid->addLayout(offsetRow, 2, 1, 1, 2);

    m_followBox = box;
    root->addWidget(box);
    box->hide();
}

// ── 边界限制组件 ───────────────────────────────────────────────────────

void DetailsPanel::buildConfiner(QVBoxLayout* root) {
    QVBoxLayout* bl = nullptr;
    auto [box_sb, content_sb] = makeSectionBox(this, &bl, "边界限制组件");
    QWidget* box = box_sb; QWidget* content = content_sb;

    auto* grid = new QGridLayout(content);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 4, 8, 10);
    grid->setColumnMinimumWidth(0, 60);

    auto mkDSpin = [&](double lo, double hi, double val) -> QDoubleSpinBox* {
        auto* s = new QDoubleSpinBox(content);
        s->setRange(lo, hi); s->setValue(val); s->setSingleStep(10.0);
        s->setDecimals(1); s->setObjectName("detailSpin");
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &DetailsPanel::onAnyFieldChanged);
        return s;
    };

    // 行0：启用
    grid->addWidget(new QLabel("启用", content), 0, 0);
    m_confinerEnabledChk = new QCheckBox(content);
    m_confinerEnabledChk->setObjectName("detailCheck");
    connect(m_confinerEnabledChk, &QCheckBox::toggled, this, &DetailsPanel::onAnyFieldChanged);
    grid->addWidget(m_confinerEnabledChk, 0, 1, 1, 2);

    // 行1：绑定 Trigger Actor（优先于手填）
    grid->addWidget(new QLabel("边界Actor", content), 1, 0);
    m_confinerActorEdit = new QLineEdit(content);
    m_confinerActorEdit->setPlaceholderText("Trigger Actor 名称（可选）");
    m_confinerActorEdit->setObjectName("detailEdit");
    connect(m_confinerActorEdit, &QLineEdit::textChanged, this, &DetailsPanel::onAnyFieldChanged);
    grid->addWidget(m_confinerActorEdit, 1, 1, 1, 2);

    // 行2：X 范围
    grid->addWidget(new QLabel("X 范围", content), 2, 0);
    auto* xRow = new QHBoxLayout;
    auto* lmin = new QLabel("最小", content); lmin->setObjectName("detailSmallLabel");
    m_confinerMinXSpin = mkDSpin(-99999, 99999, -500.0);
    auto* lmax = new QLabel("最大", content); lmax->setObjectName("detailSmallLabel");
    m_confinerMaxXSpin = mkDSpin(-99999, 99999, 500.0);
    xRow->addWidget(lmin); xRow->addWidget(m_confinerMinXSpin, 1);
    xRow->addWidget(lmax); xRow->addWidget(m_confinerMaxXSpin, 1);
    grid->addLayout(xRow, 2, 1, 1, 2);

    // 行3：Y 范围
    grid->addWidget(new QLabel("Y 范围", content), 3, 0);
    auto* yRow = new QHBoxLayout;
    auto* lymin = new QLabel("最小", content); lymin->setObjectName("detailSmallLabel");
    m_confinerMinYSpin = mkDSpin(-99999, 99999, -500.0);
    auto* lymax = new QLabel("最大", content); lymax->setObjectName("detailSmallLabel");
    m_confinerMaxYSpin = mkDSpin(-99999, 99999, 500.0);
    yRow->addWidget(lymin); yRow->addWidget(m_confinerMinYSpin, 1);
    yRow->addWidget(lymax); yRow->addWidget(m_confinerMaxYSpin, 1);
    grid->addLayout(yRow, 3, 1, 1, 2);

    m_confinerBox = box;
    root->addWidget(box);
    box->hide();
}

void DetailsPanel::buildSpriteRenderer(QVBoxLayout* root) {
    auto* box = new QGroupBox(this);
    box->setObjectName("detailGroup");
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(0);
    boxLayout->setContentsMargins(0, 0, 0, 0);

    // 标题行
    auto* titleBar = new QWidget(box);
    titleBar->setObjectName("detailTitleBar");
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(8, 6, 8, 6);
    auto* triBtn = new QToolButton(box);
    triBtn->setText("▼"); triBtn->setCheckable(true); triBtn->setChecked(true);
    triBtn->setObjectName("detailTriangle"); triBtn->setAutoRaise(true);
    auto* titleLbl = new QLabel("精灵渲染器", box); titleLbl->setObjectName("detailGroupTitle");
    QFont f = titleLbl->font(); f.setBold(true); titleLbl->setFont(f);
    titleRow->addWidget(triBtn);
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    boxLayout->addWidget(titleBar);

    // 内容区域
    auto* content = new QWidget(box);
    auto* grid = new QGridLayout(content);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 4, 8, 10);
    grid->setColumnMinimumWidth(0, 50);
    boxLayout->addWidget(content);

    // 行1：精灵图片
    grid->addWidget(new QLabel("精灵", content), 0, 0);
    auto* spriteRow = new QHBoxLayout;
    m_spritePathLabel = new QLabel("(无图片)", content);
    m_spritePathLabel->setObjectName("detailSmallLabel");
    auto* browseBtn = new QPushButton("选择...", content);
    browseBtn->setObjectName("addComponentBtn");
    browseBtn->setFixedHeight(22);
    spriteRow->addWidget(m_spritePathLabel, 1);
    spriteRow->addWidget(browseBtn);
    grid->addLayout(spriteRow, 0, 1, 1, 2);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("选择精灵图片");
        dlg.resize(500, 420);

        auto* vl   = new QVBoxLayout(&dlg);
        auto* grid = new QListWidget(&dlg);
        grid->setViewMode(QListWidget::IconMode);
        grid->setIconSize({64, 56});
        grid->setGridSize({88, 88});
        grid->setResizeMode(QListWidget::Adjust);
        grid->setMovement(QListWidget::Static);
        grid->setWrapping(true);
        grid->setSpacing(4);

        if (!m_projectRoot.isEmpty()) {
            QDirIterator it(m_projectRoot,
                {"*.png","*.jpg","*.jpeg","*.bmp","*.svg","*.webp"},
                QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                const QString name = QFileInfo(path).fileName();
                QPixmap bg(64, 56); bg.fill(QColor(35, 35, 35));
                QPixmap src(path);
                if (!src.isNull()) {
                    QPixmap scaled = src.scaled(60, 52,
                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    QPainter p(&bg);
                    p.drawPixmap((64 - scaled.width()) / 2,
                                 (56 - scaled.height()) / 2, scaled);
                }
                auto* item = new QListWidgetItem(QIcon(bg), name, grid);
                item->setData(Qt::UserRole, path);
                item->setSizeHint({88, 88});
                item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            }
        }

        auto* hl        = new QHBoxLayout;
        auto* okBtn     = new QPushButton("确定", &dlg);
        auto* cancelBtn = new QPushButton("取消", &dlg);
        hl->addStretch();
        hl->addWidget(okBtn);
        hl->addWidget(cancelBtn);
        vl->addWidget(grid);
        vl->addLayout(hl);

        connect(grid, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
        connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() == QDialog::Accepted) {
            auto* sel = grid->currentItem();
            if (sel) assignSpritePath(sel->data(Qt::UserRole).toString());
        }
    });

    // 行2：颜色
    grid->addWidget(new QLabel("颜色", content), 1, 0);
    m_spriteColorBtn = new QPushButton(content);
    m_spriteColorBtn->setObjectName("spriteColorBtn");
    m_spriteColorBtn->setFixedWidth(70);
    m_spriteColorBtn->setFixedHeight(22);
    m_spriteColorBtn->setStyleSheet("background:#ffffffff; border:1px solid #555;");
    grid->addWidget(m_spriteColorBtn, 1, 1, 1, 2);

    connect(m_spriteColorBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_currentActor.spriteColor, this,
            "精灵颜色", QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        m_currentActor.spriteColor = c;
        m_spriteColorBtn->setStyleSheet(
            QString("background:%1; border:1px solid #555;").arg(c.name(QColor::HexArgb)));
        emit actorModified(m_currentActor);
    });

    // 行3：翻转
    grid->addWidget(new QLabel("翻转", content), 2, 0);
    auto* flipRow = new QHBoxLayout;
    m_flipXCheck = new QCheckBox("X", content);
    m_flipXCheck->setObjectName("detailCheck");
    m_flipYCheck = new QCheckBox("Y", content);
    m_flipYCheck->setObjectName("detailCheck");
    flipRow->addWidget(m_flipXCheck);
    flipRow->addWidget(m_flipYCheck);
    flipRow->addStretch();
    grid->addLayout(flipRow, 2, 1, 1, 2);

    connect(m_flipXCheck, &QCheckBox::toggled, this, &DetailsPanel::onAnyFieldChanged);
    connect(m_flipYCheck, &QCheckBox::toggled, this, &DetailsPanel::onAnyFieldChanged);

    // 行4：排序层
    grid->addWidget(new QLabel("排序层", content), 3, 0);
    m_sortLayerCombo = new QComboBox(content);
    m_sortLayerCombo->setObjectName("detailCombo");
    m_sortLayerCombo->addItems({"背景", "默认", "前景", "界面", "物理"});
    m_sortLayerCombo->setCurrentIndex(1);
    grid->addWidget(m_sortLayerCombo, 3, 1, 1, 2);
    connect(m_sortLayerCombo, &QComboBox::currentTextChanged,
            this, &DetailsPanel::onAnyFieldChanged);

    // 行5：层序
    grid->addWidget(new QLabel("层序", content), 4, 0);
    m_orderSpin = new QSpinBox(content);
    m_orderSpin->setObjectName("detailSpin");
    m_orderSpin->setRange(-100, 100);
    m_orderSpin->setValue(0);
    grid->addWidget(m_orderSpin, 4, 1, 1, 2);
    connect(m_orderSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DetailsPanel::onAnyFieldChanged);

    // 行6：绘制模式
    grid->addWidget(new QLabel("绘制模式", content), 5, 0);
    m_drawModeCombo = new QComboBox(content);
    m_drawModeCombo->setObjectName("detailCombo");
    m_drawModeCombo->addItems({"简单", "平铺", "切片"});
    grid->addWidget(m_drawModeCombo, 5, 1, 1, 2);
    connect(m_drawModeCombo, &QComboBox::currentTextChanged,
            this, &DetailsPanel::onAnyFieldChanged);

    connect(triBtn, &QToolButton::toggled, this, [content, triBtn](bool expanded) {
        content->setVisible(expanded);
        triBtn->setText(expanded ? "▼" : "▶");
    });

    m_spriteBox = box;
    root->addWidget(box);
    box->hide();
}

void DetailsPanel::onAnyFieldChanged() {
    if (m_currentActor.id.isEmpty()) return;
    ActorData a  = m_currentActor;
    a.active     = m_activeCheck->isChecked();
    a.name       = m_nameEdit->text().trimmed();

    // 实时更新组件树根节点名称
    if (m_componentTree->topLevelItemCount() > 0) {
        const QString rootText = a.name.isEmpty() ? "(实例)" : a.name + "  (实例)";
        m_componentTree->topLevelItem(0)->setText(0, rootText);
    }
    a.isStatic   = m_staticCheck->isChecked();
    a.tag        = m_tagCombo->currentText();
    a.layer      = m_layerCombo->currentText();
    a.x          = (float)m_posX->value();
    a.y          = (float)m_posY->value();
    a.rotation   = (float)m_rotation->value();
    a.scaleX     = (float)m_scaleX->value();
    a.scaleY     = (float)m_scaleY->value();
    if (m_spriteBox && m_spriteBox->isVisible()) {
        a.spritePath   = m_currentActor.spritePath;
        a.spriteColor  = m_currentActor.spriteColor;
        a.flipX        = m_flipXCheck->isChecked();
        a.flipY        = m_flipYCheck->isChecked();
        a.sortingLayer = m_sortLayerCombo->currentText();
        a.orderInLayer = m_orderSpin->value();
        a.drawMode     = m_drawModeCombo->currentText();
    }
    if (m_cameraBox && m_cameraBox->isVisible()) {
        a.cameraIsMain       = m_cameraIsMainCheck->isChecked();
        a.cameraOrthographic = m_cameraOrthoCheck->isChecked();
        a.cameraSize         = (float)m_cameraSizeSpin->value();
        a.cameraResW         = m_cameraResWSpin->value();
        a.cameraResH         = m_cameraResHSpin->value();
        a.cameraBackground   = m_currentActor.cameraBackground;
    }
    if (m_followBox && m_followBox->isVisible()) {
        a.followTarget    = m_followTargetEdit->text().trimmed();
        a.followLerpSpeed = (float)m_followLerpSpin->value();
        a.followOffsetX   = (float)m_followOffsetXSpin->value();
        a.followOffsetY   = (float)m_followOffsetYSpin->value();
    }
    if (m_confinerBox && m_confinerBox->isVisible()) {
        a.confinerEnabled = m_confinerEnabledChk->isChecked();
        a.confinerActor   = m_confinerActorEdit->text().trimmed();
        a.confinerMinX    = (float)m_confinerMinXSpin->value();
        a.confinerMaxX    = (float)m_confinerMaxXSpin->value();
        a.confinerMinY    = (float)m_confinerMinYSpin->value();
        a.confinerMaxY    = (float)m_confinerMaxYSpin->value();
    }
    m_currentActor = a;
    emit actorModified(a);
}
