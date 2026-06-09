#include "DetailsPanel.h"
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

// ── 默认组件（与 Viewport2D / SceneOutliner 保持同步） ───────────────

static QStringList defaultComponents(const QString& type) {
    if (type == "Camera")  return {"变换", "摄像机组件"};
    if (type == "Sprite")  return {"变换", "精灵渲染器"};
    if (type == "Light")   return {"变换", "点光源"};
    if (type == "Trigger") return {"变换", "碰撞盒"};
    return {"变换"};
}

// ── 类型颜色（与 Viewport2D 一致） ────────────────────────────────────

QColor DetailsPanel::typeColor(const QString& type) {
    if (type == "Camera")  return QColor(80,  160, 240);
    if (type == "Sprite")  return QColor(80,  200, 100);
    if (type == "Light")   return QColor(255, 220,  50);
    if (type == "Trigger") return QColor(220, 130,  50);
    return                        QColor(160, 160, 160);
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
        const QStringList all = {"变换", "精灵渲染器", "摄像机组件", "点光源",
                                  "碰撞盒", "刚体", "音频源", "动画控制器"};
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
                   b13(*m_sortLayerCombo), b14(*m_orderSpin), b15(*m_drawModeCombo);

    m_currentActor = actor;
    // 迁移：旧 Actor JSON 中 components 为空时补入默认组件并立即保存
    const bool needsMigration = m_currentActor.components.isEmpty();
    if (needsMigration)
        m_currentActor.components = defaultComponents(m_currentActor.type);

    QPixmap px(18, 18);
    px.fill(typeColor(actor.type));
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
    m_currentActor = a;
    emit actorModified(a);
}
