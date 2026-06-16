#include "EnumEditor.h"
#include "GlobalVars.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>

EnumEditor::EnumEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("enumEditor");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 16);

    m_title = new QLabel(this);
    m_title->setObjectName("enumEditorTitle");
    lay->addWidget(m_title);

    lay->addWidget(new QLabel("选项：", this));
    m_list = new QListWidget(this);
    m_list->setMaximumWidth(420);
    lay->addWidget(m_list, 1);

    auto* row = new QHBoxLayout();
    auto* addBtn = new QPushButton("＋ 添加", this);
    auto* delBtn = new QPushButton("－ 删除", this);
    auto* upBtn  = new QPushButton("↑", this);
    auto* dnBtn  = new QPushButton("↓", this);
    upBtn->setFixedWidth(36); dnBtn->setFixedWidth(36);
    row->addWidget(addBtn); row->addWidget(delBtn);
    row->addStretch(1);
    row->addWidget(upBtn); row->addWidget(dnBtn);
    row->setStretch(2, 0);
    auto* rowWrap = new QHBoxLayout();
    rowWrap->addLayout(row);
    rowWrap->addStretch(1);
    lay->addLayout(rowWrap);
    lay->addStretch(1);

    connect(addBtn, &QPushButton::clicked, this, &EnumEditor::addValue);
    connect(delBtn, &QPushButton::clicked, this, &EnumEditor::removeSelected);
    connect(upBtn,  &QPushButton::clicked, this, [this]() { move(-1); });
    connect(dnBtn,  &QPushButton::clicked, this, [this]() { move(1); });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        if (!m_loading) save();
    });
}

void EnumEditor::load(const QString& enumPath) {
    m_loading = true;
    m_path = enumPath;
    const EnumDef e = EnumDef::load(enumPath);
    const QString nm = e.name.isEmpty() ? QFileInfo(enumPath).baseName() : e.name;
    m_title->setText("枚举：" + nm);
    m_list->clear();
    for (const QString& v : e.values) {
        auto* it = new QListWidgetItem(v, m_list);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
    }
    m_loading = false;
}

void EnumEditor::addValue() {
    if (m_path.isEmpty()) return;
    auto* it = new QListWidgetItem(QString("选项%1").arg(m_list->count() + 1), m_list);
    it->setFlags(it->flags() | Qt::ItemIsEditable);
    m_list->setCurrentItem(it);
    save();
    m_list->editItem(it);
}

void EnumEditor::removeSelected() {
    const int r = m_list->currentRow();
    if (r < 0) return;
    delete m_list->takeItem(r);
    save();
}

void EnumEditor::move(int delta) {
    const int r = m_list->currentRow();
    const int nr = r + delta;
    if (r < 0 || nr < 0 || nr >= m_list->count()) return;
    auto* it = m_list->takeItem(r);
    m_list->insertItem(nr, it);
    m_list->setCurrentRow(nr);
    save();
}

void EnumEditor::save() {
    if (m_path.isEmpty()) return;
    EnumDef e;
    e.name = QFileInfo(m_path).baseName();
    for (int i = 0; i < m_list->count(); ++i) {
        const QString v = m_list->item(i)->text().trimmed();
        if (!v.isEmpty()) e.values.append(v);
    }
    e.save(m_path);
    emit changed();
}
