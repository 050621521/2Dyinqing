#include "EnumEditor.h"
#include "GlobalVars.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>

EnumEditor::EnumEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("enumEditor");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(8);

    m_title = new QLabel(this);
    m_title->setObjectName("enumEditorTitle");
    lay->addWidget(m_title);

    // 顶部：添加枚举器
    auto* top = new QHBoxLayout();
    auto* addBtn = new QPushButton("＋ 添加枚举器", this);
    top->addWidget(addBtn);
    top->addStretch(1);
    lay->addLayout(top);

    // 枚举值表：显示命名 | 描述 | 删除
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("enumValueTable");
    m_table->setHorizontalHeaderLabels({"显示命名", "描述", ""});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->setColumnWidth(2, 40);
    m_table->verticalHeader()->setVisible(false);
    m_table->setMaximumWidth(900);
    lay->addWidget(m_table, 1);

    connect(addBtn, &QPushButton::clicked, this, &EnumEditor::addValue);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        if (!m_loading) save();
    });
}

void EnumEditor::appendRow(const QString& name, const QString& desc) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(name));
    m_table->setItem(r, 1, new QTableWidgetItem(desc));
    auto* del = new QPushButton("🗑", m_table);
    del->setFlat(true);
    connect(del, &QPushButton::clicked, this, [this, del]() {
        for (int i = 0; i < m_table->rowCount(); ++i)
            if (m_table->cellWidget(i, 2) == del) { m_table->removeRow(i); break; }
        save();
    });
    m_table->setCellWidget(r, 2, del);
}

void EnumEditor::load(const QString& enumPath) {
    m_loading = true;
    m_path = enumPath;
    const EnumDef e = EnumDef::load(enumPath);
    const QString nm = e.name.isEmpty() ? QFileInfo(enumPath).baseName() : e.name;
    m_title->setText("枚举：" + nm);
    m_table->setRowCount(0);
    for (int i = 0; i < e.values.size(); ++i)
        appendRow(e.values[i], i < e.descriptions.size() ? e.descriptions[i] : QString());
    m_loading = false;
}

void EnumEditor::addValue() {
    if (m_path.isEmpty()) return;
    m_loading = true;
    appendRow(QString("枚举值%1").arg(m_table->rowCount() + 1), QString());
    m_loading = false;
    save();
}

void EnumEditor::save() {
    if (m_path.isEmpty()) return;
    EnumDef e;
    e.name = QFileInfo(m_path).baseName();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem* nameIt = m_table->item(i, 0);
        const QString v = nameIt ? nameIt->text().trimmed() : QString();
        if (v.isEmpty()) continue;
        QTableWidgetItem* descIt = m_table->item(i, 1);
        e.values.append(v);
        e.descriptions.append(descIt ? descIt->text() : QString());
    }
    e.save(m_path);
    emit changed();
}
