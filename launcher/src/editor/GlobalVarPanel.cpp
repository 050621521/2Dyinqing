#include "GlobalVarPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>

static const char* kTypes[] = { "number", "bool", "string" };

GlobalVarPanel::GlobalVarPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("globalVarPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName("globalVarTable");
    m_table->setHorizontalHeaderLabels({"变量名", "类型"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 90);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_table, 1);

    auto* btns = new QHBoxLayout();
    auto* addBtn = new QPushButton("＋ 添加", this);
    auto* delBtn = new QPushButton("－ 删除", this);
    btns->addWidget(addBtn);
    btns->addWidget(delBtn);
    btns->addStretch(1);
    lay->addLayout(btns);

    connect(addBtn, &QPushButton::clicked, this, &GlobalVarPanel::addRow);
    connect(delBtn, &QPushButton::clicked, this, &GlobalVarPanel::removeSelected);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (m_loading) return;
        if (it->column() == 0) {
            const QString oldName = it->data(Qt::UserRole).toString();
            const QString newName = it->text().trimmed();
            if (!oldName.isEmpty() && oldName != newName)
                emit varRenamed(oldName, newName);
            m_table->blockSignals(true);
            it->setData(Qt::UserRole, newName);   // 记住新名作下次比较
            m_table->blockSignals(false);
        }
        commit();
    });
}

void GlobalVarPanel::setProjectRoot(const QString& root) {
    m_projectRoot = root;
    reload();
}

void GlobalVarPanel::reload() {
    m_loading = true;
    m_table->setRowCount(0);
    for (const GlobalVarDef& d : GlobalVars::load(m_projectRoot)) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        auto* nameItem = new QTableWidgetItem(d.name);
        nameItem->setData(Qt::UserRole, d.name);
        m_table->setItem(r, 0, nameItem);
        auto* combo = new QComboBox(m_table);
        combo->addItem("数值",   "number");
        combo->addItem("布尔",   "bool");
        combo->addItem("字符串", "string");
        for (int i = 0; i < 3; ++i)
            if (d.type == kTypes[i]) { combo->setCurrentIndex(i); break; }
        connect(combo, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!m_loading) commit();
        });
        m_table->setCellWidget(r, 1, combo);
    }
    m_loading = false;
}

void GlobalVarPanel::commit() {
    QList<GlobalVarDef> defs;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* it = m_table->item(r, 0);
        const QString name = it ? it->text().trimmed() : QString();
        if (name.isEmpty()) continue;
        QString type = "string";
        if (auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(r, 1)))
            type = combo->currentData().toString();
        // 名字唯一：重复则跳过后者
        bool dup = false;
        for (const GlobalVarDef& d : defs) if (d.name == name) { dup = true; break; }
        if (dup) continue;
        defs.append({name, type});
    }
    GlobalVars::save(m_projectRoot, defs);
    emit changed();
}

void GlobalVarPanel::addRow() {
    m_loading = true;
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    auto* nameItem = new QTableWidgetItem(QString("变量%1").arg(r + 1));
    nameItem->setData(Qt::UserRole, nameItem->text());
    m_table->setItem(r, 0, nameItem);
    auto* combo = new QComboBox(m_table);
    combo->addItem("数值",   "number");
    combo->addItem("布尔",   "bool");
    combo->addItem("字符串", "string");
    combo->setCurrentIndex(2);   // 默认字符串
    connect(combo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loading) commit();
    });
    m_table->setCellWidget(r, 1, combo);
    m_loading = false;
    commit();
}

void GlobalVarPanel::removeSelected() {
    const int r = m_table->currentRow();
    if (r < 0) return;
    m_table->removeRow(r);
    commit();
}
