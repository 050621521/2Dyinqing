#include "ContentBrowser.h"
#include "models/BPClass.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QSizePolicy>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QFileDialog>
#include <QUuid>
#include <QKeyEvent>

ContentBrowser::ContentBrowser(const QString& projectRoot, QWidget* parent)
    : QWidget(parent), m_projectRoot(projectRoot), m_currentPath(projectRoot)
{
    setObjectName("contentBrowser");

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 左侧：文件夹树 ────────────────────────────────────────────────
    auto* leftWrap = new QWidget(this);
    leftWrap->setObjectName("cbFolderWrap");
    leftWrap->setFixedWidth(200);
    auto* leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    m_folderTree = new QTreeWidget(leftWrap);
    m_folderTree->setObjectName("cbFolderTree");
    m_folderTree->setHeaderHidden(true);
    m_folderTree->setIndentation(16);
    m_folderTree->setRootIsDecorated(true);
    leftLay->addWidget(m_folderTree);

    auto* vLine = new QFrame(this);
    vLine->setFrameShape(QFrame::VLine);
    vLine->setObjectName("cbDivider");
    vLine->setFixedWidth(1);

    // ── 右侧：工具栏 + 资源网格 ───────────────────────────────────────
    auto* rightWrap = new QWidget(this);
    auto* rightLay = new QVBoxLayout(rightWrap);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);

    // 工具栏
    auto* toolbar = new QWidget(rightWrap);
    toolbar->setObjectName("cbToolbar");
    toolbar->setFixedHeight(30);
    auto* tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(6, 2, 6, 2);
    tl->setSpacing(4);

    auto cbBtn = [&](const QString& t) -> QPushButton* {
        auto* b = new QPushButton(t, toolbar);
        b->setObjectName("cbToolBtn");
        tl->addWidget(b);
        return b;
    };
    auto cbSep = [&]() {
        auto* f = new QFrame(toolbar);
        f->setFrameShape(QFrame::VLine);
        f->setObjectName("cbSep");
        tl->addWidget(f);
    };

    cbBtn("过滤  ▾");
    cbSep();
    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setObjectName("cbSearchEdit");
    m_searchEdit->setPlaceholderText("搜索资源…");
    m_searchEdit->setFixedWidth(160);
    tl->addWidget(m_searchEdit);
    cbSep();
    auto* saveAllBtn = cbBtn("全部保存");
    auto* importBtn = cbBtn("导入图片");
    auto* newLevelBtn = cbBtn("+ 新建关卡");
    newLevelBtn->setObjectName("cbNewLevelBtn");
    tl->addStretch();
    cbBtn("方向  ▾");

    rightLay->addWidget(toolbar);

    auto* hLine = new QFrame(rightWrap);
    hLine->setFrameShape(QFrame::HLine);
    hLine->setObjectName("cbHDivider");
    hLine->setFixedHeight(1);
    rightLay->addWidget(hLine);

    // 资源网格
    m_assetGrid = new QListWidget(rightWrap);
    m_assetGrid->setObjectName("cbAssetGrid");
    m_assetGrid->setViewMode(QListWidget::IconMode);
    m_assetGrid->setIconSize({64, 56});
    m_assetGrid->setGridSize({88, 88});
    m_assetGrid->setResizeMode(QListWidget::Adjust);
    m_assetGrid->setMovement(QListWidget::Static);
    m_assetGrid->setWrapping(true);
    m_assetGrid->setSpacing(4);
    m_assetGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_assetGrid->setDragEnabled(true);
    m_assetGrid->setDragDropMode(QAbstractItemView::DragOnly);
    m_assetGrid->installEventFilter(this);
    rightLay->addWidget(m_assetGrid, 1);

    // 面包屑
    auto* breadcrumb = new QWidget(rightWrap);
    breadcrumb->setObjectName("cbBreadcrumb");
    breadcrumb->setFixedHeight(22);
    auto* bl = new QHBoxLayout(breadcrumb);
    bl->setContentsMargins(8, 0, 8, 0);
    m_bcLabel = new QLabel("内容", breadcrumb);
    m_bcLabel->setObjectName("cbBcLabel");
    m_countLabel = new QLabel("", breadcrumb);
    m_countLabel->setObjectName("cbCountLabel");
    bl->addWidget(m_bcLabel);
    bl->addStretch();
    bl->addWidget(m_countLabel);
    rightLay->addWidget(breadcrumb);

    root->addWidget(leftWrap);
    root->addWidget(vLine);
    root->addWidget(rightWrap, 1);

    // 左侧树右键菜单
    m_folderTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folderTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_folderTree->itemAt(pos);
        QMenu menu(this);
        if (item) {
            const QString path = item->data(0, Qt::UserRole).toString();
            const bool isRoot = (path == m_projectRoot);
            if (!isRoot) {
                menu.addAction("重命名", [this, item]() {
                    m_folderTree->editItem(item, 0);
                });
                menu.addAction("删除", [this, path]() {
                    const QString name = QDir(path).dirName();
                    if (QMessageBox::question(this, "删除文件夹",
                            QString("确定删除文件夹「%1」及其所有内容？").arg(name),
                            QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
                    if (QDir(path).removeRecursively()) {
                        buildFolderTree();
                        populateFolder(m_currentPath.startsWith(path)
                            ? QFileInfo(path).dir().absolutePath()
                            : m_currentPath);
                    } else {
                        QMessageBox::warning(this, "删除文件夹", "删除失败，请确认文件夹未被占用。");
                    }
                });
            }
        } else {
            menu.addAction("新建文件夹", this, &ContentBrowser::onNewFolder);
        }
        if (!menu.isEmpty())
            menu.exec(m_folderTree->viewport()->mapToGlobal(pos));
    });

    // 连接信号
    connect(m_folderTree, &QTreeWidget::itemClicked, this, &ContentBrowser::onFolderSelected);
    connect(m_assetGrid, &QListWidget::itemDoubleClicked, this, &ContentBrowser::onGridItemDoubleClicked);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ContentBrowser::onSearchChanged);
    connect(newLevelBtn, &QPushButton::clicked, this, &ContentBrowser::onNewLevel);
    connect(m_assetGrid, &QListWidget::customContextMenuRequested, this, &ContentBrowser::showGridContextMenu);
    connect(saveAllBtn, &QPushButton::clicked, this, &ContentBrowser::saveAllRequested);
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, "导入图片", QString(),
            "图片 (*.png *.jpg *.jpeg *.bmp *.svg *.webp)");
        if (files.isEmpty()) return;
        for (const QString& src : files) {
            const QString dst = m_currentPath + "/" + QFileInfo(src).fileName();
            if (!QFile::exists(dst)) QFile::copy(src, dst);
        }
        populateFolder(m_currentPath);
    });

    // F2 内联重命名
    m_assetGrid->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_folderTree->setEditTriggers(QAbstractItemView::EditKeyPressed);

    connect(m_assetGrid, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const QString type    = item->data(Qt::UserRole).toString();
        const QString oldPath = item->data(Qt::UserRole + 1).toString();
        const QString newBase = item->text().trimmed();

        auto restoreName = [&]() {
            QSignalBlocker b(m_assetGrid);
            item->setText(type == "dir" ? QDir(oldPath).dirName() : QFileInfo(oldPath).baseName());
        };
        if (newBase.isEmpty()) { restoreName(); return; }

        QString newFileName = newBase;
        if      (type == "level") newFileName += ".level";
        else if (type == "bp")    newFileName += ".bp";
        else if (type == "ui")    newFileName += ".ui";
        else if (type == "image") newFileName += "." + QFileInfo(oldPath).suffix();

        const QString newPath = QFileInfo(oldPath).dir().filePath(newFileName);
        if (newPath == oldPath) return;
        if (!QFile::rename(oldPath, newPath)) { restoreName(); return; }

        QSignalBlocker b(m_assetGrid);
        item->setData(Qt::UserRole + 1, newPath);
        if (type == "dir") {
            buildFolderTree();
            if (m_currentPath.startsWith(oldPath))
                m_currentPath = newPath + m_currentPath.mid(oldPath.length());
        }
    });

    connect(m_folderTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        const QString oldPath = item->data(0, Qt::UserRole).toString();
        if (oldPath.isEmpty() || oldPath == m_projectRoot) {
            QSignalBlocker b(m_folderTree); item->setText(0, "内容"); return;
        }
        const QString newName = item->text(0).trimmed();
        const QString oldName = QDir(oldPath).dirName();
        if (newName == oldName) return;
        if (newName.isEmpty()) {
            QSignalBlocker b(m_folderTree); item->setText(0, oldName); return;
        }
        const QString newPath = QFileInfo(oldPath).dir().filePath(newName);
        if (!QFile::rename(oldPath, newPath)) {
            QSignalBlocker b(m_folderTree); item->setText(0, oldName); return;
        }
        QSignalBlocker b(m_folderTree);
        item->setData(0, Qt::UserRole, newPath);
        populateFolder(m_currentPath.startsWith(oldPath)
            ? newPath + m_currentPath.mid(oldPath.length())
            : m_currentPath);
    });

    buildFolderTree();
    populateFolder(m_projectRoot);
}

// ── 图标 ─────────────────────────────────────────────────────────────

QIcon ContentBrowser::makeFolderIcon() {
    static QIcon icon;
    if (!icon.isNull()) return icon;

    QPixmap px(64, 56);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    // 主体
    p.setBrush(QColor("#c8941a"));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(2, 14, 60, 38, 5, 5);

    // 标签突出部分
    p.setBrush(QColor("#d4a820"));
    p.drawRoundedRect(2, 10, 24, 10, 3, 3);

    icon = QIcon(px);
    return icon;
}

QIcon ContentBrowser::makeLevelIcon() {
    static QIcon icon;
    if (!icon.isNull()) return icon;

    QPixmap px(64, 56);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    // 主体
    p.setBrush(QColor("#1a5fa0"));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(4, 6, 56, 44, 5, 5);

    // 网格线
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (int x = 16; x < 60; x += 12)
        p.drawLine(x, 6, x, 50);
    for (int y = 18; y < 50; y += 12)
        p.drawLine(4, y, 60, y);

    // 地图针
    p.setBrush(QColor("#66aaff"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(28, 20, 8, 8);

    icon = QIcon(px);
    return icon;
}

QIcon ContentBrowser::makeBpClassIcon() {
    static QIcon icon;
    if (!icon.isNull()) return icon;

    QPixmap px(64, 64);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(100, 60, 160));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(8, 8, 48, 48, 8, 8);
    p.setPen(Qt::white);
    QFont f; f.setPixelSize(18); f.setBold(true);
    p.setFont(f);
    p.drawText(px.rect(), Qt::AlignCenter, "BP");
    icon = QIcon(px);
    return icon;
}

QIcon ContentBrowser::makeUIDocIcon() {
    static QIcon icon;
    if (!icon.isNull()) return icon;
    QPixmap px(64, 64);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 80, 140));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(8, 8, 48, 48, 8, 8);
    p.setBrush(QColor(60, 120, 200));
    p.drawRoundedRect(14, 14, 36, 10, 3, 3);
    p.setBrush(QColor(50, 100, 160));
    p.drawRect(14, 28, 36, 20);
    icon = QIcon(px);
    return icon;
}

// ── 目录树 ────────────────────────────────────────────────────────────

void ContentBrowser::buildFolderTree() {
    QSignalBlocker blocker(m_folderTree);
    m_folderTree->clear();

    auto* rootItem = new QTreeWidgetItem(m_folderTree, QStringList{"内容"});
    rootItem->setData(0, Qt::UserRole, m_projectRoot);
    rootItem->setIcon(0, makeFolderIcon());
    m_folderTree->addTopLevelItem(rootItem);

    addDirToTree(rootItem, m_projectRoot, 0);
    rootItem->setExpanded(true);
}

void ContentBrowser::addDirToTree(QTreeWidgetItem* parent, const QString& absPath, int depth) {
    if (depth >= 3) return;

    QDir dir(absPath);
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : subs) {
        const QString childPath = absPath + "/" + name;
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setData(0, Qt::UserRole, childPath);
        child->setIcon(0, makeFolderIcon());
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        addDirToTree(child, childPath, depth + 1);
    }
}

// ── 网格填充 ──────────────────────────────────────────────────────────

void ContentBrowser::populateFolder(const QString& absPath) {
    m_currentPath = absPath;
    m_currentEntries.clear();
    m_currentTypes.clear();

    QDir dir(absPath);

    // 子目录
    const QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& d : dirs) {
        m_currentEntries << d;
        m_currentTypes   << "dir";
    }

    // .level 文件
    const QStringList levels = dir.entryList({"*.level"}, QDir::Files, QDir::Name);
    for (const QString& lv : levels) {
        m_currentEntries << lv;
        m_currentTypes   << "level";
    }

    // .bp 蓝图类文件
    const QStringList bpFiles = dir.entryList({"*.bp"}, QDir::Files, QDir::Name);
    for (const QString& bp : bpFiles) {
        m_currentEntries << bp;
        m_currentTypes   << "bp";
    }

    // .ui UI 文档文件
    const QStringList uiFiles = dir.entryList({"*.ui"}, QDir::Files, QDir::Name);
    for (const QString& ui : uiFiles) {
        m_currentEntries << ui;
        m_currentTypes   << "ui";
    }

    // 图片文件
    const QStringList imgExts = {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.svg", "*.webp"};
    const QStringList images = dir.entryList(imgExts, QDir::Files, QDir::Name);
    for (const QString& img : images) {
        m_currentEntries << img;
        m_currentTypes   << "image";
    }

    onSearchChanged(m_searchEdit ? m_searchEdit->text() : QString());
    updateBreadcrumb();
}

void ContentBrowser::onSearchChanged(const QString& text) {
    m_assetGrid->clear();
    const QString filter = text.trimmed().toLower();

    for (int i = 0; i < m_currentEntries.size(); ++i) {
        const QString& name = m_currentEntries[i];
        const QString& type = m_currentTypes[i];

        if (!filter.isEmpty() && !name.toLower().contains(filter))
            continue;

        const QString absPath = m_currentPath + "/" + name;
        const QString label   = (type == "level" || type == "bp" || type == "ui")
            ? QFileInfo(name).baseName()
            : name;

        QSignalBlocker gridBlocker(m_assetGrid);
        auto* item = new QListWidgetItem(label, m_assetGrid);
        if      (type == "dir")   item->setIcon(makeFolderIcon());
        else if (type == "level") item->setIcon(makeLevelIcon());
        else if (type == "bp")    item->setIcon(makeBpClassIcon());
        else if (type == "ui")    item->setIcon(makeUIDocIcon());
        else if (type == "image") item->setIcon(makeImageIcon(absPath));
        item->setData(Qt::UserRole,     type);
        item->setData(Qt::UserRole + 1, absPath);
        item->setSizeHint({88, 88});
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
}

// ── 交互 ──────────────────────────────────────────────────────────────

void ContentBrowser::onFolderSelected(QTreeWidgetItem* item, int) {
    const QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty())
        populateFolder(path);
}

void ContentBrowser::onGridItemDoubleClicked(QListWidgetItem* item) {
    const QString type = item->data(Qt::UserRole).toString();
    const QString path = item->data(Qt::UserRole + 1).toString();

    if (type == "dir") {
        // 进入子目录，同步树选中
        populateFolder(path);
        // 展开并选中对应树节点
        QList<QTreeWidgetItem*> found = m_folderTree->findItems(
            QDir(path).dirName(), Qt::MatchRecursive | Qt::MatchExactly);
        for (auto* ti : found) {
            if (ti->data(0, Qt::UserRole).toString() == path) {
                m_folderTree->setCurrentItem(ti);
                break;
            }
        }
    } else if (type == "level") {
        emit levelOpenRequested(path);
    } else if (type == "bp") {
        emit bpClassOpenRequested(path);
    } else if (type == "ui") {
        emit uiDocOpenRequested(path);
    } else if (type == "image") {
        emit imageAssignRequested(path);
    }
}

void ContentBrowser::onNewLevel() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "新建关卡", "关卡名称：",
                                         QLineEdit::Normal, "新关卡", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    name = name.trimmed();
    if (!name.endsWith(".level"))
        name += ".level";

    const QString filePath = m_currentPath + "/" + name;
    if (QFile::exists(filePath)) {
        QMessageBox::warning(this, "新建关卡", QString("文件已存在：%1").arg(name));
        return;
    }
    if (createLevelFile(filePath))
        populateFolder(m_currentPath);
}

void ContentBrowser::onNewFolder() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "新建文件夹", "文件夹名称：",
                                         QLineEdit::Normal, "新文件夹", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    name = name.trimmed();
    if (!QDir(m_currentPath).mkdir(name)) {
        QMessageBox::warning(this, "新建文件夹", "创建失败，请检查名称是否合法。");
        return;
    }
    buildFolderTree();
    populateFolder(m_currentPath);
}

bool ContentBrowser::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_assetGrid && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            QListWidgetItem* item = m_assetGrid->currentItem();
            if (!item) return true;
            const QString type = item->data(Qt::UserRole).toString();
            const QString path = item->data(Qt::UserRole + 1).toString();
            const QString name = QFileInfo(path).fileName();
            if (type == "level") {
                if (QMessageBox::question(this, "删除", QString("确定删除「%1」？").arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return true;
                QFile::remove(path);
                populateFolder(m_currentPath);
                emit levelFileDeleted(path);
            } else if (type == "bp") {
                if (QMessageBox::question(this, "删除蓝图", QString("确定删除「%1」？").arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return true;
                QFile::remove(path);
                populateFolder(m_currentPath);
            } else if (type == "ui") {
                if (QMessageBox::question(this, "删除UI", QString("确定删除「%1」？").arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return true;
                QFile::remove(path);
                populateFolder(m_currentPath);
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void ContentBrowser::showGridContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_assetGrid->itemAt(pos);
    QMenu menu(this);

    if (item) {
        const QString type = item->data(Qt::UserRole).toString();
        const QString path = item->data(Qt::UserRole + 1).toString();

        if (type == "level") {
            menu.addAction("打开", [this, path]() {
                emit levelOpenRequested(path);
            });
            menu.addSeparator();
            menu.addAction("重命名", [this, item]() {
                m_assetGrid->editItem(item);
            });
            menu.addAction("删除", [this, path]() {
                const QString name = QFileInfo(path).fileName();
                if (QMessageBox::question(this, "删除", QString("确定删除「%1」？").arg(name),
                    QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
                QFile::remove(path);
                populateFolder(m_currentPath);
                emit levelFileDeleted(path);
            });
        } else if (type == "dir") {
            menu.addAction("进入文件夹", [this, path]() {
                populateFolder(path);
                const QList<QTreeWidgetItem*> found = m_folderTree->findItems(
                    QDir(path).dirName(), Qt::MatchRecursive | Qt::MatchExactly);
                for (auto* ti : found) {
                    if (ti->data(0, Qt::UserRole).toString() == path) {
                        m_folderTree->setCurrentItem(ti);
                        break;
                    }
                }
            });
            menu.addSeparator();
            menu.addAction("重命名", [this, item]() {
                m_assetGrid->editItem(item);
            });
            menu.addAction("删除", [this, path]() {
                const QString name = QDir(path).dirName();
                if (QMessageBox::question(this, "删除文件夹",
                        QString("确定删除文件夹「%1」及其所有内容？").arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
                if (QDir(path).removeRecursively()) {
                    buildFolderTree();
                    populateFolder(m_currentPath.startsWith(path)
                        ? QFileInfo(path).dir().absolutePath()
                        : m_currentPath);
                } else {
                    QMessageBox::warning(this, "删除文件夹", "删除失败，请确认文件夹未被占用。");
                }
            });
        } else if (type == "bp") {
            menu.addAction("打开", [this, path]() {
                emit bpClassOpenRequested(path);
            });
            menu.addSeparator();
            menu.addAction("重命名", [this, item]() {
                m_assetGrid->editItem(item);
            });
            menu.addAction("删除", [this, path]() {
                const QString name = QFileInfo(path).fileName();
                if (QMessageBox::question(this, "删除蓝图",
                        QString("确定删除「%1」？").arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
                QFile::remove(path);
                populateFolder(m_currentPath);
            });
        } else if (type == "ui") {
            menu.addAction("打开", [this, path]() {
                emit uiDocOpenRequested(path);
            });
            menu.addSeparator();
            menu.addAction("重命名", [this, item]() {
                m_assetGrid->editItem(item);
            });
            menu.addAction("删除", [this, path]() {
                const QString name = QFileInfo(path).fileName();
                if (QMessageBox::question(this, "删除UI",
                        QString("确定删除「%1」？").arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
                QFile::remove(path);
                populateFolder(m_currentPath);
            });
        } else if (type == "image") {
            menu.addAction("指定给当前精灵", [this, path]() {
                emit imageAssignRequested(path);
            });
            menu.addSeparator();
            menu.addAction("重命名", [this, item]() {
                m_assetGrid->editItem(item);
            });
            menu.addAction("删除", [this, path]() {
                if (QMessageBox::question(this, "删除图片",
                        QString("确定删除「%1」？").arg(QFileInfo(path).fileName()),
                        QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
                QFile::remove(path);
                m_imageIconCache.remove(path);
                populateFolder(m_currentPath);
            });
        }
    } else {
        menu.addAction("导入图片", [this]() {
            const QStringList files = QFileDialog::getOpenFileNames(
                this, "导入图片", QString(),
                "图片 (*.png *.jpg *.jpeg *.bmp *.svg *.webp)");
            if (files.isEmpty()) return;
            for (const QString& src : files) {
                const QString dst = m_currentPath + "/" + QFileInfo(src).fileName();
                if (!QFile::exists(dst)) QFile::copy(src, dst);
            }
            populateFolder(m_currentPath);
        });
        menu.addSeparator();
        menu.addAction("新建关卡", this, &ContentBrowser::onNewLevel);
        menu.addAction("新建蓝图类", this, [this]() {
            bool ok;
            const QString name = QInputDialog::getText(this, "新建蓝图类", "蓝图类名称：",
                                                        QLineEdit::Normal, "新蓝图", &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            const QString dir = m_projectRoot + "/Blueprints";
            QDir().mkpath(dir);
            const QString path = dir + "/" + name.trimmed() + ".bp";
            BPClass bc;
            bc.name     = name.trimmed();
            bc.filePath = path;
            bc.save();
            populateFolder(m_currentPath);
        });
        menu.addAction("新建UI", this, [this]() {
            bool ok;
            const QString name = QInputDialog::getText(this, "新建UI", "UI名称：",
                                                        QLineEdit::Normal, "新UI", &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            const QString dir = m_projectRoot + "/UI";
            QDir().mkpath(dir);
            const QString path = dir + "/" + name.trimmed() + ".ui";
            QJsonObject root;
            root["name"]    = name.trimmed();
            root["version"] = "0.1";
            root["widgets"] = QJsonArray();
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            populateFolder(m_currentPath);
        });
        menu.addAction("新建文件夹", this, &ContentBrowser::onNewFolder);
    }

    if (!menu.isEmpty())
        menu.exec(m_assetGrid->viewport()->mapToGlobal(pos));
}

// ── 辅助 ─────────────────────────────────────────────────────────────

bool ContentBrowser::createLevelFile(const QString& filePath) {
    QJsonObject obj;
    obj["name"]    = QFileInfo(filePath).baseName();
    obj["version"] = "0.1";

    // 创建默认主摄像机 Actor
    QJsonObject cameraActor;
    cameraActor["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cameraActor["name"] = "主摄像机";
    cameraActor["bpClass"] = "builtin/Camera";

    // 位置和旋转
    QJsonObject pos;
    pos["x"] = 0;
    pos["y"] = 0;
    cameraActor["position"] = pos;
    cameraActor["rotation"] = 0;

    // 缩放
    QJsonObject scale;
    scale["x"] = 1;
    scale["y"] = 1;
    cameraActor["scale"] = scale;

    // 基本属性
    cameraActor["active"] = true;
    cameraActor["static"] = false;
    cameraActor["tag"] = "未标记";
    cameraActor["layer"] = "默认";

    // 组件
    QJsonArray components;
    components.append("摄像机组件");
    cameraActor["components"] = components;

    // 精灵渲染相关
    cameraActor["spritePath"] = "";
    cameraActor["spriteColor"] = "#ffffffff";
    cameraActor["sortingLayer"] = "默认";
    cameraActor["orderInLayer"] = 0;
    cameraActor["flipX"] = false;
    cameraActor["flipY"] = false;
    cameraActor["drawMode"] = "简单";

    // 摄像机特定属性
    cameraActor["cameraSize"] = 5.0;
    cameraActor["cameraIsMain"] = true;
    cameraActor["cameraResW"] = 1920;
    cameraActor["cameraResH"] = 1080;
    cameraActor["cameraOrthographic"] = true;
    cameraActor["cameraBackground"] = "#ff1e1e1e";

    // 跟随属性
    cameraActor["followTarget"] = "";
    cameraActor["followLerpSpeed"] = 5.0;
    cameraActor["followOffsetX"] = 0;
    cameraActor["followOffsetY"] = 0;

    // 约束属性
    cameraActor["confinerEnabled"] = false;
    cameraActor["confinerMinX"] = -500;
    cameraActor["confinerMaxX"] = 500;
    cameraActor["confinerMinY"] = -500;
    cameraActor["confinerMaxY"] = 500;

    // 将摄像机 Actor 加入 objects 数组
    QJsonArray objects;
    objects.append(cameraActor);
    obj["objects"] = objects;

    QJsonObject blueprint;
    blueprint["nodes"] = QJsonArray();
    blueprint["connections"] = QJsonArray();
    obj["blueprint"] = blueprint;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QIcon ContentBrowser::makeImageIcon(const QString& path) {
    auto it = m_imageIconCache.find(path);
    if (it != m_imageIconCache.end()) return it.value();

    QPixmap bg(64, 56);
    bg.fill(QColor(35, 35, 35));

    QPixmap src(path);
    if (!src.isNull()) {
        QPixmap scaled = src.scaled(60, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter p(&bg);
        p.drawPixmap((64 - scaled.width()) / 2, (56 - scaled.height()) / 2, scaled);
    }

    QIcon icon(bg);
    m_imageIconCache[path] = icon;
    return icon;
}

void ContentBrowser::updateBreadcrumb() {
    if (!m_bcLabel || !m_countLabel) return;

    const QString rel = QDir(m_projectRoot).relativeFilePath(m_currentPath);
    const QString display = rel.isEmpty() || rel == "." ? "内容" : ("内容 / " + rel);
    m_bcLabel->setText(display);

    const int count = m_currentEntries.size();
    m_countLabel->setText(QString("%1 项").arg(count));
}
