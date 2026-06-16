#include "GlobalVarPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>

GlobalVarPanel::GlobalVarPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("globalVarPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    // ── 变量 ──
    lay->addWidget(new QLabel("全局变量", this));
    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName("globalVarTable");
    m_table->setHorizontalHeaderLabels({"变量名", "类型"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 110);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_table, 1);

    auto* vbtns = new QHBoxLayout();
    auto* addVar = new QPushButton("＋ 变量", this);
    auto* delVar = new QPushButton("－ 变量", this);
    vbtns->addWidget(addVar); vbtns->addWidget(delVar); vbtns->addStretch(1);
    lay->addLayout(vbtns);

    // ── 枚举 ──
    lay->addWidget(new QLabel("枚举", this));
    m_enumTable = new QTableWidget(0, 2, this);
    m_enumTable->setObjectName("enumTable");
    m_enumTable->setHorizontalHeaderLabels({"枚举名", "选项（逗号分隔）"});
    m_enumTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_enumTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_enumTable->setColumnWidth(0, 90);
    m_enumTable->verticalHeader()->setVisible(false);
    m_enumTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_enumTable, 1);

    auto* ebtns = new QHBoxLayout();
    auto* addEnum = new QPushButton("＋ 枚举", this);
    auto* delEnum = new QPushButton("－ 枚举", this);
    ebtns->addWidget(addEnum); ebtns->addWidget(delEnum); ebtns->addStretch(1);
    lay->addLayout(ebtns);

    connect(addVar, &QPushButton::clicked, this, &GlobalVarPanel::addVarRow);
    connect(delVar, &QPushButton::clicked, this, &GlobalVarPanel::removeSelectedVar);
    connect(addEnum, &QPushButton::clicked, this, &GlobalVarPanel::addEnumRow);
    connect(delEnum, &QPushButton::clicked, this, &GlobalVarPanel::removeSelectedEnum);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (m_loading) return;
        if (it->column() == 0) {
            const QString oldName = it->data(Qt::UserRole).toString();
            const QString newName = it->text().trimmed();
            if (!oldName.isEmpty() && oldName != newName) emit varRenamed(oldName, newName);
            m_table->blockSignals(true);
            it->setData(Qt::UserRole, newName);
            m_table->blockSignals(false);
        }
        commitVars();
    });
    connect(m_enumTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        if (!m_loading) commitEnums();
    });
}

void GlobalVarPanel::setProjectRoot(const QString& root) {
    m_projectRoot = root;
    reload();
}

void GlobalVarPanel::fillTypeCombo(QComboBox* combo, const QString& currentType) {
    combo->clear();
    combo->addItem("数值",   "number");
    combo->addItem("布尔",   "bool");
    combo->addItem("字符串", "string");
    for (const EnumDef& e : Enums::load(m_projectRoot))
        combo->addItem("枚举(" + e.name + ")", "enum:" + e.name);
    for (int i = 0; i < combo->count(); ++i)
        if (combo->itemData(i).toString() == currentType) { combo->setCurrentIndex(i); break; }
}

void GlobalVarPanel::reload() {
    m_loading = true;
    // 变量
    m_table->setRowCount(0);
    for (const GlobalVarDef& d : GlobalVars::load(m_projectRoot)) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        auto* nameItem = new QTableWidgetItem(d.name);
        nameItem->setData(Qt::UserRole, d.name);
        m_table->setItem(r, 0, nameItem);
        auto* combo = new QComboBox(m_table);
        fillTypeCombo(combo, d.type);
        connect(combo, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!m_loading) commitVars();
        });
        m_table->setCellWidget(r, 1, combo);
    }
    // 枚举
    m_enumTable->setRowCount(0);
    for (const EnumDef& e : Enums::load(m_projectRoot)) {
        const int r = m_enumTable->rowCount();
        m_enumTable->insertRow(r);
        m_enumTable->setItem(r, 0, new QTableWidgetItem(e.name));
        m_enumTable->setItem(r, 1, new QTableWidgetItem(e.values.join(",")));
    }
    m_loading = false;
}

void GlobalVarPanel::commitVars() {
    QList<GlobalVarDef> defs;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* it = m_table->item(r, 0);
        const QString name = it ? it->text().trimmed() : QString();
        if (name.isEmpty()) continue;
        QString type = "string";
        if (auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(r, 1)))
            type = combo->currentData().toString();
        bool dup = false;
        for (const GlobalVarDef& d : defs) if (d.name == name) { dup = true; break; }
        if (dup) continue;
        defs.append({name, type});
    }
    GlobalVars::save(m_projectRoot, defs);
    emit changed();
}

void GlobalVarPanel::commitEnums() {
    QList<EnumDef> defs;
    for (int r = 0; r < m_enumTable->rowCount(); ++r) {
        QTableWidgetItem* nameIt = m_enumTable->item(r, 0);
        const QString name = nameIt ? nameIt->text().trimmed() : QString();
        if (name.isEmpty()) continue;
        EnumDef e; e.name = name;
        QTableWidgetItem* valIt = m_enumTable->item(r, 1);
        if (valIt)
            for (const QString& v : valIt->text().split(",", Qt::SkipEmptyParts))
                e.values.append(v.trimmed());
        bool dup = false;
        for (const EnumDef& d : defs) if (d.name == name) { dup = true; break; }
        if (dup) continue;
        defs.append(e);
    }
    Enums::save(m_projectRoot, defs);
    reload();         // 枚举变了 → 重建变量类型下拉
    emit changed();
}

void GlobalVarPanel::addVarRow() {
    m_loading = true;
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    auto* nameItem = new QTableWidgetItem(QString("变量%1").arg(r + 1));
    nameItem->setData(Qt::UserRole, nameItem->text());
    m_table->setItem(r, 0, nameItem);
    auto* combo = new QComboBox(m_table);
    fillTypeCombo(combo, "string");
    connect(combo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loading) commitVars();
    });
    m_table->setCellWidget(r, 1, combo);
    m_loading = false;
    commitVars();
}

void GlobalVarPanel::removeSelectedVar() {
    const int r = m_table->currentRow();
    if (r < 0) return;
    m_table->removeRow(r);
    commitVars();
}

void GlobalVarPanel::addEnumRow() {
    m_loading = true;
    const int r = m_enumTable->rowCount();
    m_enumTable->insertRow(r);
    m_enumTable->setItem(r, 0, new QTableWidgetItem(QString("枚举%1").arg(r + 1)));
    m_enumTable->setItem(r, 1, new QTableWidgetItem("选项1,选项2"));
    m_loading = false;
    commitEnums();
}

void GlobalVarPanel::removeSelectedEnum() {
    const int r = m_enumTable->currentRow();
    if (r < 0) return;
    m_enumTable->removeRow(r);
    commitEnums();
}
