#include "SceneOutliner.h"
#include "UndoCommands.h"
#include "models/ActorTypeUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QMenu>
#include <QInputDialog>
#include <QHeaderView>
#include <QUuid>
#include <QFont>
#include <QFrame>
#include <QMap>
#include <QSet>
#include <QAbstractItemView>
#include <QItemSelectionModel>

SceneOutliner::SceneOutliner(QWidget* parent) : QWidget(parent) {
    setObjectName("sceneOutliner");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 搜索栏
    auto* searchRow = new QWidget(this);
    searchRow->setObjectName("outlineSearchRow");
    searchRow->setFixedHeight(28);
    auto* hl = new QHBoxLayout(searchRow);
    hl->setContentsMargins(6, 3, 6, 3);
    hl->setSpacing(4);
    m_search = new QLineEdit(searchRow);
    m_search->setObjectName("outlineSearch");
    m_search->setPlaceholderText("搜索 Actor…");
    hl->addWidget(m_search);
    root->addWidget(searchRow);

    // 树
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName("sceneTree");
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setHeaderHidden(false);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"名称", "类型"});
    m_tree->header()->setObjectName("sceneTreeHeader");
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 70);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection); // 支持 Ctrl/Shift 多选
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_tree, 1);

    connect(m_search, &QLineEdit::textChanged, this, &SceneOutliner::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SceneOutliner::onSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SceneOutliner::showContextMenu);

    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item && !item->data(0, Qt::UserRole).toString().isEmpty())
            m_tree->editItem(item, 0);
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
        if (col != 0 || !m_doc) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty()) return;
        const QString newName = item->text(0).trimmed();
        if (newName.isEmpty()) {
            for (const ActorData& a : m_doc->actors()) {
                if (a.id == id) { QSignalBlocker b(m_tree); item->setText(0, a.name); break; }
            }
            return;
        }
        for (ActorData a : m_doc->actors()) {
            if (a.id == id) {
                if (a.name == newName) break;
                a.name = newName;
                m_doc->updateActor(a);
                emit levelChanged();
                break;
            }
        }
    });
}

// ── 加载 / 清空 ───────────────────────────────────────────────────────

void SceneOutliner::loadLevel(LevelDocument* doc) {
    m_doc = doc;
    rebuild();
}

void SceneOutliner::clear() {
    m_doc = nullptr;
    m_tree->clear();
}

void SceneOutliner::setUndoStack(QUndoStack* stack, std::function<void()> refresh) {
    m_undoStack = stack;
    m_onRefresh = std::move(refresh);
}

// ── 重建树 ────────────────────────────────────────────────────────────

void SceneOutliner::rebuild() {
    QSignalBlocker blocker(m_tree);
    m_tree->clear();
    if (!m_doc) return;

    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();

    for (const ActorData& a : m_doc->actors()) {
        if (!filter.isEmpty() && !a.name.toLower().contains(filter)) continue;

        auto* item = new QTreeWidgetItem(m_tree, QStringList{a.name, bpClassLabel(a.bpClass)});
        item->setData(0, Qt::UserRole, a.id);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        m_tree->addTopLevelItem(item);
    }
}

// ── 选择 Actor（支持多选）────────────────────────────────────────────

void SceneOutliner::onSelectionChanged() {
    if (!m_doc) return;
    QStringList ids;
    for (QTreeWidgetItem* item : m_tree->selectedItems()) {
        const QString id = item->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) ids << id;
    }
    emit selectionChanged(ids);
}

// 视口选区回灌到大纲高亮：阻塞信号避免回环
void SceneOutliner::setSelectedIds(const QStringList& ids) {
    if (!m_tree) return;
    QSignalBlocker blocker(m_tree);
    const QSet<QString> set(ids.begin(), ids.end());
    QTreeWidgetItem* firstSel = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        const bool sel = set.contains(item->data(0, Qt::UserRole).toString());
        item->setSelected(sel);
        if (sel && !firstSel) firstSel = item;
    }
    // 只移动当前项、不改动选区（NoUpdate），便于键盘导航与重命名
    m_tree->setCurrentItem(firstSel, 0, QItemSelectionModel::NoUpdate);
}

// ── 搜索 ──────────────────────────────────────────────────────────────

void SceneOutliner::onSearchChanged(const QString&) {
    rebuild();
}

// ── 右键菜单 ──────────────────────────────────────────────────────────

void SceneOutliner::showContextMenu(const QPoint& pos) {
    if (!m_doc) return;

    QTreeWidgetItem* item = m_tree->itemAt(pos);
    const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
    const bool isActor = !id.isEmpty();

    QMenu menu(this);

    if (isActor) {
        // 收集要操作的 id：右键项在多选内 → 整批；否则仅该项
        QStringList targetIds;
        for (QTreeWidgetItem* it : m_tree->selectedItems()) {
            const QString sid = it->data(0, Qt::UserRole).toString();
            if (!sid.isEmpty()) targetIds << sid;
        }
        if (!targetIds.contains(id)) targetIds = {id};

        const QString copyLabel = targetIds.size() > 1
            ? QString("复制 %1 个").arg(targetIds.size()) : QString("复制");
        menu.addAction(copyLabel, [this]() { emit copyRequested(); });
        menu.addAction("粘贴", [this]() { emit pasteRequested(); });
        menu.addSeparator();
        menu.addAction("重命名", [this, item]() {
            m_tree->editItem(item, 0);
        });
        const QString delLabel = targetIds.size() > 1
            ? QString("删除 %1 个").arg(targetIds.size()) : QString("删除");
        menu.addAction(delLabel, [this, targetIds]() {
            if (!m_doc) return;
            if (m_undoStack && m_onRefresh) m_undoStack->beginMacro("删除 Actor");
            for (const QString& tid : targetIds) {
                ActorData data;
                for (const ActorData& a : m_doc->actors())
                    if (a.id == tid) { data = a; break; }
                if (m_undoStack && m_onRefresh) {
                    m_undoStack->push(new ActorRemoveCmd(m_doc, data, m_onRefresh));
                } else {
                    m_doc->removeActor(tid);
                }
                emit actorRemoved(tid);
            }
            if (m_undoStack && m_onRefresh) m_undoStack->endMacro();
            else { rebuild(); emit levelChanged(); }
        });
    } else {
        menu.addAction("粘贴", [this]() { emit pasteRequested(); });
        menu.addSeparator();
        auto* addMenu = menu.addMenu("添加 Actor");
        for (const QString& type : kActorTypes) {
            addMenu->addAction(typeLabel(type), [this, type]() {
                if (!m_doc) return;
                ActorData a;
                a.id         = QUuid::createUuid().toString(QUuid::WithoutBraces);
                a.name       = typeLabel(type);
                a.bpClass    = "builtin/" + type;
                a.components = defaultComponents(type);
                m_doc->addActor(a);
                rebuild();
                emit levelChanged();
            });
        }
    }

    if (!menu.isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
