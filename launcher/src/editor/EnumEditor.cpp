#include "EnumEditor.h"
#include "GlobalVars.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>

EnumEditor::EnumEditor(const QString& enumPath, QWidget* parent)
    : QDialog(parent), m_path(enumPath)
{
    const EnumDef e = EnumDef::load(enumPath);
    setWindowTitle("枚举编辑器 — " + (e.name.isEmpty() ? QFileInfo(enumPath).baseName() : e.name));
    setModal(true);
    resize(320, 380);

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel("选项：", this));

    m_list = new QListWidget(this);
    for (const QString& v : e.values) {
        auto* it = new QListWidgetItem(v, m_list);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
    }
    lay->addWidget(m_list, 1);

    auto* row = new QHBoxLayout();
    auto* addBtn = new QPushButton("＋ 添加", this);
    auto* delBtn = new QPushButton("－ 删除", this);
    auto* upBtn  = new QPushButton("↑", this);
    auto* dnBtn  = new QPushButton("↓", this);
    upBtn->setFixedWidth(32); dnBtn->setFixedWidth(32);
    row->addWidget(addBtn); row->addWidget(delBtn);
    row->addStretch(1);
    row->addWidget(upBtn); row->addWidget(dnBtn);
    lay->addLayout(row);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    auto* okBtn = new QPushButton("确定", this);
    auto* cancelBtn = new QPushButton("取消", this);
    bottom->addWidget(okBtn); bottom->addWidget(cancelBtn);
    lay->addLayout(bottom);

    connect(addBtn, &QPushButton::clicked, this, &EnumEditor::addValue);
    connect(delBtn, &QPushButton::clicked, this, &EnumEditor::removeSelected);
    connect(upBtn,  &QPushButton::clicked, this, [this]() { move(-1); });
    connect(dnBtn,  &QPushButton::clicked, this, [this]() { move(1); });
    connect(okBtn,  &QPushButton::clicked, this, &EnumEditor::saveAndClose);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void EnumEditor::addValue() {
    auto* it = new QListWidgetItem(QString("选项%1").arg(m_list->count() + 1), m_list);
    it->setFlags(it->flags() | Qt::ItemIsEditable);
    m_list->setCurrentItem(it);
    m_list->editItem(it);
}

void EnumEditor::removeSelected() {
    const int r = m_list->currentRow();
    if (r >= 0) delete m_list->takeItem(r);
}

void EnumEditor::move(int delta) {
    const int r = m_list->currentRow();
    const int nr = r + delta;
    if (r < 0 || nr < 0 || nr >= m_list->count()) return;
    auto* it = m_list->takeItem(r);
    m_list->insertItem(nr, it);
    m_list->setCurrentRow(nr);
}

void EnumEditor::saveAndClose() {
    EnumDef e;
    e.name = QFileInfo(m_path).baseName();
    for (int i = 0; i < m_list->count(); ++i) {
        const QString v = m_list->item(i)->text().trimmed();
        if (!v.isEmpty()) e.values.append(v);
    }
    e.save(m_path);
    accept();
}
