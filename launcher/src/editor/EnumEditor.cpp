#include "EnumEditor.h"
#include "GlobalVars.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>

static const char* kStyle =
    "QWidget#enumEditor { background:#1b1b1b; }"
    "QLabel#secHdr { background:#2c2c2c; color:#cfcfcf; font-weight:bold; padding:5px 8px; }"
    "QLabel#rowLbl { color:#9a9a9a; }"
    "QLineEdit { background:#101010; border:1px solid #383838; border-radius:3px; padding:4px 6px; color:#dddddd; }"
    "QLineEdit:focus { border:1px solid #5a7fb0; }"
    "QPushButton#addEnum { background:#2a3a2a; border:1px solid #3a5a3a; border-radius:3px; padding:5px 12px; color:#9bd39b; }"
    "QPushButton#delRow { border:none; color:#c06060; font-size:14px; }";

EnumEditor::EnumEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("enumEditor");
    setStyleSheet(kStyle);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 工具栏
    auto* tb = new QHBoxLayout();
    tb->setContentsMargins(10, 8, 10, 8);
    m_title = new QLabel(this);
    auto* addBtn = new QPushButton("＋ 添加枚举器", this);
    addBtn->setObjectName("addEnum");
    tb->addWidget(addBtn);
    tb->addSpacing(12);
    tb->addWidget(m_title);
    tb->addStretch(1);
    lay->addLayout(tb);

    // 描述区
    auto* descHdr = new QLabel("描述", this);
    descHdr->setObjectName("secHdr");
    lay->addWidget(descHdr);
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(16, 6, 16, 6);
        auto* l = new QLabel("列举描述", this); l->setObjectName("rowLbl"); l->setFixedWidth(80);
        m_enumDesc = new QLineEdit(this);
        row->addWidget(l);
        row->addWidget(m_enumDesc, 1);
        lay->addLayout(row);
        connect(m_enumDesc, &QLineEdit::editingFinished, this, [this]() { if (!m_loading) save(); });
    }

    // 枚举值区
    auto* valHdr = new QLabel("枚举值", this);
    valHdr->setObjectName("secHdr");
    lay->addWidget(valHdr);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* rowsHost = new QWidget();
    m_rowsLay = new QVBoxLayout(rowsHost);
    m_rowsLay->setContentsMargins(16, 6, 16, 6);
    m_rowsLay->setSpacing(4);
    m_rowsLay->addStretch(1);
    scroll->setWidget(rowsHost);
    lay->addWidget(scroll, 1);

    connect(addBtn, &QPushButton::clicked, this, &EnumEditor::addValue);
}

void EnumEditor::appendRow(const QString& key, const QString& disp, const QString& desc) {
    auto* w = new QWidget();
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* kl = new QLabel("键值", w); kl->setObjectName("rowLbl"); kl->setFixedWidth(50);
    auto* keyEdit = new QLineEdit(key, w);
    auto* nl = new QLabel("显示命名", w); nl->setObjectName("rowLbl"); nl->setFixedWidth(70);
    auto* dispEdit = new QLineEdit(disp, w);
    auto* dl = new QLabel("描述", w); dl->setObjectName("rowLbl"); dl->setFixedWidth(40);
    auto* descEdit = new QLineEdit(desc, w);
    auto* del = new QPushButton("🗑", w); del->setObjectName("delRow"); del->setFixedWidth(28);
    h->addWidget(kl);  h->addWidget(keyEdit, 2);
    h->addWidget(nl);  h->addWidget(dispEdit, 2);
    h->addWidget(dl);  h->addWidget(descEdit, 2);
    h->addWidget(del);

    m_rowsLay->insertWidget(m_rowsLay->count() - 1, w);
    m_rows.append({w, keyEdit, dispEdit, descEdit});

    auto saveOnEdit = [this]() { if (!m_loading) save(); };
    connect(keyEdit,  &QLineEdit::editingFinished, this, saveOnEdit);
    connect(dispEdit, &QLineEdit::editingFinished, this, saveOnEdit);
    connect(descEdit, &QLineEdit::editingFinished, this, saveOnEdit);
    connect(del, &QPushButton::clicked, this, [this, w]() { removeRow(w); });
}

void EnumEditor::removeRow(QWidget* rowW) {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].w == rowW) { m_rows.removeAt(i); break; }
    rowW->deleteLater();
    save();
}

void EnumEditor::load(const QString& enumPath) {
    m_loading = true;
    m_path = enumPath;
    const EnumDef e = EnumDef::load(enumPath);
    const QString nm = e.name.isEmpty() ? QFileInfo(enumPath).baseName() : e.name;
    m_title->setText("枚举：" + nm);
    m_enumDesc->setText(QString());   // 列举描述：当前 EnumDef 暂不存，留作 UI（可后续接）
    // 清空旧行
    for (const Row& r : m_rows) r.w->deleteLater();
    m_rows.clear();
    for (int i = 0; i < e.values.size(); ++i)
        appendRow(e.values[i],
                  i < e.displays.size()     ? e.displays[i]     : QString(),
                  i < e.descriptions.size() ? e.descriptions[i] : QString());
    m_loading = false;
}

void EnumEditor::addValue() {
    if (m_path.isEmpty()) return;
    appendRow(QString("值%1").arg(m_rows.size() + 1), QString(), QString());
    save();
}

void EnumEditor::save() {
    if (m_path.isEmpty()) return;
    EnumDef e;
    e.name = QFileInfo(m_path).baseName();
    for (const Row& r : m_rows) {
        const QString v = r.key->text().trimmed();
        if (v.isEmpty()) continue;
        e.values.append(v);
        e.displays.append(r.disp->text());
        e.descriptions.append(r.desc->text());
    }
    e.save(m_path);
    emit changed();
}
