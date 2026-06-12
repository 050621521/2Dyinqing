#include "SceneOutliner.h"
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
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_tree, 1);

    connect(m_search, &QLineEdit::textChanged, this, &SceneOutliner::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &SceneOutliner::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SceneOutliner::showContextMenu);
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

// ── 重建树 ────────────────────────────────────────────────────────────

void SceneOutliner::rebuild() {
    m_tree->clear();
    if (!m_doc) return;

    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();

    for (const ActorData& a : m_doc->actors()) {
        if (!filter.isEmpty() && !a.name.toLower().contains(filter)) continue;

        auto* item = new QTreeWidgetItem(m_tree, QStringList{a.name, bpClassLabel(a.bpClass)});
        item->setData(0, Qt::UserRole, a.id);
        m_tree->addTopLevelItem(item);
    }
}

// ── 点击 Actor ────────────────────────────────────────────────────────

void SceneOutliner::onItemClicked(QTreeWidgetItem* item, int) {
    if (!m_doc || !item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return; // 分组节点

    for (const ActorData& a : m_doc->actors()) {
        if (a.id == id) { emit actorSelected(a); return; }
    }
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
        menu.addAction("重命名", [this, id, item]() {
            if (!m_doc) return;
            bool ok = false;
            QString newName = QInputDialog::getText(this, "重命名", "新名称：",
                              QLineEdit::Normal, item->text(0), &ok);
            if (!ok || newName.trimmed().isEmpty()) return;
            for (ActorData a : m_doc->actors()) {
                if (a.id == id) { a.name = newName.trimmed(); m_doc->updateActor(a); break; }
            }
            rebuild();
            emit levelChanged();
        });
        menu.addAction("删除", [this, id]() {
            if (!m_doc) return;
            m_doc->removeActor(id);
            rebuild();
            emit actorRemoved(id);
            emit levelChanged();
        });
    } else {
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
