#include "GlobalVarPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QIcon>

static QColor colorForType(const QString& type) {
    if (type == "number") return QColor(0x3a, 0x7a, 0xd4);   // 蓝
    if (type == "bool")   return QColor(0xc0, 0x50, 0x50);   // 红
    if (type == "string") return QColor(0xd0, 0x6a, 0xb0);   // 粉
    if (type.startsWith("enum:")) return QColor(0x2a, 0x9a, 0x8a); // 青
    return QColor(0x88, 0x88, 0x88);
}

static QIcon typeIcon(const QString& type) {
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(colorForType(type));
    p.drawRoundedRect(1, 3, 10, 6, 2, 2);
    return QIcon(pm);
}

GlobalVarPanel::GlobalVarPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("globalVarPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    // 顶部：标题 + 添加/删除
    auto* head = new QHBoxLayout();
    head->addWidget(new QLabel("变量", this));
    head->addStretch(1);
    auto* addBtn = new QPushButton("＋", this);
    auto* delBtn = new QPushButton("－", this);
    addBtn->setFixedWidth(28); delBtn->setFixedWidth(28);
    head->addWidget(addBtn); head->addWidget(delBtn);
    lay->addLayout(head);

    // 列表
    m_list = new QListWidget(this);
    m_list->setObjectName("globalVarList");
    lay->addWidget(m_list, 1);

    // 细节区
    lay->addWidget(new QLabel("细节", this));
    auto* form = new QFormLayout();
    m_nameEdit  = new QLineEdit(this);
    m_typeCombo = new QComboBox(this);
    form->addRow("名字", m_nameEdit);
    form->addRow("类型", m_typeCombo);
    lay->addLayout(form);

    connect(addBtn, &QPushButton::clicked, this, &GlobalVarPanel::addVar);
    connect(delBtn, &QPushButton::clicked, this, &GlobalVarPanel::removeSelected);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &GlobalVarPanel::onNameEdited);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loading) onTypeChanged();
    });
    onSelectionChanged();   // 初始禁用细节
}

void GlobalVarPanel::setProjectRoot(const QString& root) {
    m_projectRoot = root;
    reload();
}

void GlobalVarPanel::refreshEnums() {
    // 类型下拉里枚举项可能变了，按当前选中变量重填
    const int row = m_list->currentRow();
    if (row >= 0 && row < m_vars.size())
        fillTypeCombo(m_vars[row].type);
}

void GlobalVarPanel::reload() {
    m_loading = true;
    m_vars = GlobalVars::load(m_projectRoot);
    rebuildList();
    m_loading = false;
    onSelectionChanged();
}

void GlobalVarPanel::rebuildList() {
    const int keep = m_list->currentRow();
    m_list->clear();
    for (int i = 0; i < m_vars.size(); ++i) {
        const GlobalVarDef& d = m_vars[i];
        auto* item = new QListWidgetItem(m_list);
        auto* w = new QWidget();
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(6, 2, 6, 2);
        auto* nameLbl = new QLabel(d.name, w);
        // 可点的类型色块：点开弹所有类型
        auto* pill = new QToolButton(w);
        pill->setText(GlobalVars::typeLabel(d.type));
        pill->setPopupMode(QToolButton::InstantPopup);
        pill->setCursor(Qt::PointingHandCursor);
        pill->setStyleSheet(QString(
            "QToolButton{background:%1;color:white;border:none;border-radius:8px;padding:1px 10px;}"
            "QToolButton::menu-indicator{image:none;}").arg(colorForType(d.type).name()));
        auto* menu = new QMenu(pill);
        auto addType = [this, menu, i](const QString& label, const QString& type) {
            menu->addAction(label, this, [this, i, type]() {
                if (i < 0 || i >= m_vars.size()) return;
                m_vars[i].type = type;
                commit();
                rebuildList();
            });
        };
        addType("数值", "number");
        addType("布尔", "bool");
        addType("字符串", "string");
        for (const EnumDef& e : Enums::loadAll(m_projectRoot))
            addType("枚举(" + e.name + ")", "enum:" + e.name);
        pill->setMenu(menu);
        h->addWidget(nameLbl, 1);
        h->addWidget(pill, 0);
        item->setSizeHint(w->sizeHint());
        m_list->setItemWidget(item, w);
    }
    if (keep >= 0 && keep < m_vars.size()) m_list->setCurrentRow(keep);
}

void GlobalVarPanel::fillTypeCombo(const QString& current) {
    m_loading = true;
    m_typeCombo->clear();
    m_typeCombo->addItem("数值",   "number");
    m_typeCombo->addItem("布尔",   "bool");
    m_typeCombo->addItem("字符串", "string");
    for (const EnumDef& e : Enums::loadAll(m_projectRoot))
        m_typeCombo->addItem("枚举(" + e.name + ")", "enum:" + e.name);
    for (int i = 0; i < m_typeCombo->count(); ++i)
        if (m_typeCombo->itemData(i).toString() == current) { m_typeCombo->setCurrentIndex(i); break; }
    m_loading = false;
}

void GlobalVarPanel::onSelectionChanged() {
    const int row = m_list->currentRow();
    const bool has = (row >= 0 && row < m_vars.size());
    m_nameEdit->setEnabled(has);
    m_typeCombo->setEnabled(has);
    if (!has) { m_loading = true; m_nameEdit->clear(); m_typeCombo->clear(); m_loading = false; return; }
    m_loading = true;
    m_nameEdit->setText(m_vars[row].name);
    m_loading = false;
    fillTypeCombo(m_vars[row].type);
}

void GlobalVarPanel::onNameEdited() {
    const int row = m_list->currentRow();
    if (m_loading || row < 0 || row >= m_vars.size()) return;
    const QString newName = m_nameEdit->text().trimmed();
    const QString oldName = m_vars[row].name;
    if (newName.isEmpty() || newName == oldName) return;
    // 唯一性
    for (int i = 0; i < m_vars.size(); ++i)
        if (i != row && m_vars[i].name == newName) { m_nameEdit->setText(oldName); return; }
    m_vars[row].name = newName;
    emit varRenamed(oldName, newName);
    rebuildList();
    commit();
}

void GlobalVarPanel::onTypeChanged() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_vars.size()) return;
    m_vars[row].type = m_typeCombo->currentData().toString();
    rebuildList();
    commit();
}

void GlobalVarPanel::addVar() {
    GlobalVarDef d;
    d.name = QString("变量%1").arg(m_vars.size() + 1);
    d.type = "string";
    m_vars.append(d);
    commit();
    rebuildList();
    m_list->setCurrentRow(m_vars.size() - 1);
}

void GlobalVarPanel::removeSelected() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_vars.size()) return;
    m_vars.removeAt(row);
    commit();
    rebuildList();
}

void GlobalVarPanel::commit() {
    GlobalVars::save(m_projectRoot, m_vars);
    emit changed();
}
