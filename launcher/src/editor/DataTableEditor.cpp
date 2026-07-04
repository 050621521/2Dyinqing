#include "DataTableEditor.h"

#include "models/AssetRegistry.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>

namespace {
class DataTableCellDelegate : public QStyledItemDelegate {
public:
    using TypeResolver = std::function<QString(int)>;
    using OptionsResolver = std::function<QStringList(const QString&)>;

    DataTableCellDelegate(TypeResolver typeResolver, OptionsResolver optionsResolver, QObject* parent = nullptr)
        : QStyledItemDelegate(parent),
          m_typeResolver(std::move(typeResolver)),
          m_optionsResolver(std::move(optionsResolver)) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        if (index.row() == 0)
            return QStyledItemDelegate::createEditor(parent, option, index);

        const QString type = m_typeResolver ? m_typeResolver(index.column()) : QString();
        if (type == "number") {
            auto* editor = new QDoubleSpinBox(parent);
            editor->setRange(-999999999.0, 999999999.0);
            editor->setDecimals(4);
            editor->setButtonSymbols(QAbstractSpinBox::NoButtons);
            return editor;
        }
        if (type == "bool") {
            auto* editor = new QComboBox(parent);
            editor->addItem("否", false);
            editor->addItem("是", true);
            return editor;
        }
        if (type == "effect" || type == "asset" || type.startsWith("enum:")) {
            auto* editor = new QComboBox(parent);
            editor->setEditable(!type.startsWith("enum:"));
            editor->addItem("");
            const QStringList options = m_optionsResolver ? m_optionsResolver(type) : QStringList();
            for (const QString& optionText : options)
                editor->addItem(optionText);
            return editor;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        const QString type = m_typeResolver ? m_typeResolver(index.column()) : QString();
        const QString text = index.data(Qt::EditRole).toString();
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
            spin->setValue(text.toDouble());
            return;
        }
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            if (type == "bool") {
                const QString v = text.trimmed().toLower();
                combo->setCurrentIndex((v == "true" || v == "1" || v == "是") ? 1 : 0);
                return;
            }
            const int idx = combo->findText(text);
            if (idx >= 0) combo->setCurrentIndex(idx);
            else combo->setEditText(text);
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
            model->setData(index, QString::number(spin->value(), 'g', 15));
            return;
        }
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            const QString type = m_typeResolver ? m_typeResolver(index.column()) : QString();
            if (type == "bool") {
                const bool checked = combo->currentData().toBool();
                model->setData(index, checked ? "是" : "否");
                model->setData(index, checked ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
            } else {
                model->setData(index, combo->currentText());
            }
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

private:
    TypeResolver m_typeResolver;
    OptionsResolver m_optionsResolver;
};

QString projectRootForDataTable(const QString& filePath) {
    QDir dir(QFileInfo(filePath).absolutePath());
    for (int guard = 0; guard < 8; ++guard) {
        if (QFileInfo::exists(dir.filePath("project.json")))
            return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QFileInfo(filePath).absoluteDir().absolutePath();
}

void applySchemaDefaults(DataTable& table) {
    const DataTableSchema schema = DataTableSchema::builtinSchema(table.schema);
    if (!schema.isValid()) return;
    for (DataTableRecord& record : table.records) {
        for (const DataTableSchemaField& field : schema.fields) {
            if (field.defaultValue.isUndefined() || field.defaultValue.isNull()) continue;
            if (field.name == "id") continue;
            const QJsonValue current = record.values.value(field.name);
            if (current.isUndefined() || current.isNull()
                || (current.isString() && current.toString().trimmed().isEmpty())) {
                record.values[field.name] = field.defaultValue;
            }
        }
    }
}
}

DataTableEditor::DataTableEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("dataTableEditor");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* toolbar = new QWidget(this);
    auto* tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(6);

    tl->addWidget(new QLabel("表名", toolbar));
    m_nameEdit = new QLineEdit(toolbar);
    m_nameEdit->setMinimumWidth(180);
    tl->addWidget(m_nameEdit);
    tl->addWidget(new QLabel("结构", toolbar));
    m_schemaCombo = new QComboBox(toolbar);
    m_schemaCombo->addItem("自由表", "");
    for (const QString& schemaName : DataTableSchema::builtinSchemaNames()) {
        const DataTableSchema schema = DataTableSchema::builtinSchema(schemaName);
        const QString label = schema.displayName.isEmpty()
            ? schemaName
            : QString("%1（%2）").arg(schemaName, schema.displayName);
        m_schemaCombo->addItem(label, schemaName);
    }
    tl->addWidget(m_schemaCombo);

    auto* addFieldBtn = new QPushButton("添加字段", toolbar);
    auto* removeFieldBtn = new QPushButton("删除字段", toolbar);
    auto* addRecordBtn = new QPushButton("添加记录", toolbar);
    auto* removeRecordBtn = new QPushButton("删除记录", toolbar);
    auto* saveBtn = new QPushButton("保存", toolbar);
    tl->addWidget(addFieldBtn);
    tl->addWidget(removeFieldBtn);
    tl->addWidget(addRecordBtn);
    tl->addWidget(removeRecordBtn);
    tl->addStretch(1);
    tl->addWidget(saveBtn);
    root->addWidget(toolbar);

    auto* fieldBar = new QWidget(this);
    auto* fl = new QHBoxLayout(fieldBar);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(6);
    fl->addWidget(new QLabel("字段名", fieldBar));
    m_fieldNameEdit = new QLineEdit(fieldBar);
    m_fieldNameEdit->setMinimumWidth(180);
    fl->addWidget(m_fieldNameEdit);
    fl->addWidget(new QLabel("类型", fieldBar));
    m_fieldTypeCombo = new QComboBox(fieldBar);
    m_fieldTypeCombo->addItems({"string", "number", "bool", "asset", "effect", "actor"});
    fl->addWidget(m_fieldTypeCombo);
    fl->addStretch(1);
    root->addWidget(fieldBar);

    m_grid = new QTableWidget(this);
    m_grid->setObjectName("dataTableGrid");
    m_grid->setStyleSheet(
        "#dataTableGrid { background: #151515; color: #d6d6d6; gridline-color: #4a4a4a; selection-background-color: #0d3a6a; selection-color: #ffffff; }"
        "#dataTableGrid::item { background: #1b1b1b; color: #d6d6d6; }"
        "#dataTableGrid::item:selected { background: #0d3a6a; color: #ffffff; }"
        "#dataTableGrid QHeaderView::section { background: #202020; color: #d0d0d0; border: 1px solid #4a4a4a; padding: 4px; }"
        "#dataTableGrid QTableCornerButton::section { background: #202020; border: 1px solid #4a4a4a; }"
    );
    m_grid->setAlternatingRowColors(false);
    m_grid->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->horizontalHeader()->setStretchLastSection(false);
    m_grid->horizontalHeader()->setSectionsClickable(true);
    m_grid->horizontalHeader()->setSectionsMovable(true);
    m_grid->horizontalHeader()->setHighlightSections(true);
    m_grid->verticalHeader()->setVisible(true);
    m_grid->setItemDelegate(new DataTableCellDelegate(
        [this](int col) {
            return (col >= 0 && col < m_table.fields.size()) ? m_table.fields[col].type : QString();
        },
        [this](const QString& type) {
            if (type.startsWith("enum:")) {
                QString enumSpec = type.mid(QString("enum:").size());
                enumSpec.replace('|', ',');
                QStringList values;
                for (const QString& part : enumSpec.split(',', Qt::SkipEmptyParts))
                    values << part.trimmed();
                values.removeDuplicates();
                return values;
            }

            const QString root = projectRootForDataTable(m_table.filePath);
            AssetRegistry registry(root);
            registry.rebuild(false);
            QStringList values;
            for (const AssetRecord& record : registry.records()) {
                if (type == "effect" && record.type != "bp.effect") continue;
                if (type == "asset" && record.type.isEmpty()) continue;
                values << record.path;
            }
            values.removeDuplicates();
            values.sort();
            return values;
        },
        m_grid));
    root->addWidget(m_grid, 1);

    m_statusLabel = new QLabel(this);
    root->addWidget(m_statusLabel);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (m_rebuilding) return;
        m_table.name = text.trimmed();
        markDirty();
    });
    connect(m_schemaCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_rebuilding) return;
        m_table.schema = m_schemaCombo->currentData().toString();
        markDirty();
    });
    connect(addFieldBtn, &QPushButton::clicked, this, &DataTableEditor::addField);
    connect(removeFieldBtn, &QPushButton::clicked, this, &DataTableEditor::removeField);
    connect(addRecordBtn, &QPushButton::clicked, this, &DataTableEditor::addRecord);
    connect(removeRecordBtn, &QPushButton::clicked, this, &DataTableEditor::removeRecord);
    connect(saveBtn, &QPushButton::clicked, this, &DataTableEditor::save);
    connect(m_fieldNameEdit, &QLineEdit::editingFinished, this, &DataTableEditor::renameSelectedField);
    connect(m_fieldTypeCombo, &QComboBox::currentTextChanged, this, &DataTableEditor::changeSelectedFieldType);
    connect(m_grid->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int logicalIndex) {
        selectField(logicalIndex);
    });
    connect(m_grid->horizontalHeader(), &QHeaderView::sectionMoved, this,
            [this](int, int, int) { syncFieldOrderFromHeader(); });
    connect(m_grid, &QTableWidget::currentCellChanged, this,
            [this](int, int currentColumn, int, int) {
        if (!m_rebuilding && currentColumn >= 0 && currentColumn != m_selectedField)
            selectField(currentColumn);
    });
    connect(m_grid, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (m_rebuilding || row < 0 || col < 0) return;
        if (col >= m_table.fields.size()) return;
        const QString field = m_table.fields[col].name;
        const QString text = m_grid->item(row, col) ? m_grid->item(row, col)->text() : QString();
        if (row == 0) {
            m_table.fields[col].comment = text;
            markDirty();
            return;
        }
        const int recordRow = row - 1;
        if (recordRow < 0 || recordRow >= m_table.records.size()) return;
        if (field == "id") {
            if (text.trimmed().isEmpty()) {
                QMessageBox::warning(this, "数据表", "记录 ID 不能为空。");
                rebuildGrid();
                return;
            }
            m_table.records[recordRow].id = text.trimmed();
            m_table.records[recordRow].values["id"] = m_table.records[recordRow].id;
        } else {
            const QString type = m_table.fields[col].type;
            if (type == "number") {
                bool ok = false;
                const double n = text.toDouble(&ok);
                m_table.records[recordRow].values[field] = ok ? QJsonValue(n) : QJsonValue(0);
            } else if (type == "bool") {
                const QTableWidgetItem* changedItem = m_grid->item(row, col);
                if (changedItem && (changedItem->flags() & Qt::ItemIsUserCheckable)) {
                    m_table.records[recordRow].values[field] = changedItem->checkState() == Qt::Checked;
                } else {
                    const QString v = text.trimmed().toLower();
                    m_table.records[recordRow].values[field] = (v == "true" || v == "1" || v == "是");
                }
            } else {
                m_table.records[recordRow].values[field] = text;
            }
        }
        markDirty();
    });
}

bool DataTableEditor::load(const QString& path) {
    DataTable table;
    if (!table.load(path)) return false;
    m_table = table;
    rebuildGrid();
    updateStatus();
    return true;
}

bool DataTableEditor::save() {
    applySchemaDefaults(m_table);
    const QStringList errors = m_table.validateSchema({}, projectRootForDataTable(m_table.filePath));
    if (!errors.isEmpty()) {
        m_statusLabel->setText("结构校验失败：" + errors.join("；"));
        QMessageBox::warning(this, "保存数据表", "数据表结构校验失败：\n" + errors.join("\n"));
        return false;
    }
    if (!m_table.save()) {
        QMessageBox::warning(this, "保存数据表", "保存失败，请检查文件权限。");
        return false;
    }
    m_table.setDirty(false);
    updateStatus();
    emit saved();
    return true;
}

void DataTableEditor::rebuildGrid() {
    m_rebuilding = true;
    m_nameEdit->setText(m_table.name);
    const QSignalBlocker schemaBlocker(m_schemaCombo);
    const int schemaIndex = m_schemaCombo->findData(m_table.schema);
    m_schemaCombo->setCurrentIndex(schemaIndex >= 0 ? schemaIndex : 0);
    m_grid->clear();
    m_grid->setColumnCount(m_table.fields.size());
    m_grid->setRowCount(m_table.records.size() + 1);

    QStringList headers;
    for (const DataTableField& f : m_table.fields)
        headers << QString("%1\n%2").arg(f.name, f.type);
    m_grid->setHorizontalHeaderLabels(headers);

    QStringList rowLabels;
    rowLabels << "字段说明";
    for (int i = 0; i < m_table.records.size(); ++i)
        rowLabels << QString::number(i + 1);
    m_grid->setVerticalHeaderLabels(rowLabels);

    for (int c = 0; c < m_table.fields.size(); ++c) {
        auto* item = new QTableWidgetItem(m_table.fields[c].comment);
        item->setBackground(QColor(45, 45, 45));
        item->setForeground(QColor(190, 190, 190));
        m_grid->setItem(0, c, item);
    }

    for (int r = 0; r < m_table.records.size(); ++r) {
        for (int c = 0; c < m_table.fields.size(); ++c) {
            const QString field = m_table.fields[c].name;
            const QString type = m_table.fields[c].type;
            const QJsonValue rawValue = field == "id"
                ? QJsonValue(m_table.records[r].id)
                : m_table.records[r].values.value(field);
            QString value = rawValue.toVariant().toString();
            if (type == "bool")
                value = rawValue.toBool() ? "是" : "否";
            auto* item = new QTableWidgetItem(value);
            if (type == "bool") {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(rawValue.toBool() ? Qt::Checked : Qt::Unchecked);
            }
            m_grid->setItem(r + 1, c, item);
        }
    }
    m_grid->resizeColumnsToContents();
    m_rebuilding = false;
    if (!m_table.fields.isEmpty()) {
        const int col = qBound(0, m_selectedField < 0 ? 0 : m_selectedField, m_table.fields.size() - 1);
        selectField(col);
    } else {
        selectField(-1);
    }
}

void DataTableEditor::markDirty() {
    m_table.setDirty(true);
    updateStatus();
    emit modified();
}

void DataTableEditor::addField() {
    const QString name = uniqueFieldName("field");
    const QString type = "string";
    m_table.fields << DataTableField{name, type};
    for (DataTableRecord& r : m_table.records) r.values[name] = "";
    m_selectedField = m_table.fields.size() - 1;
    rebuildGrid();
    const int col = m_table.fieldIndex(name);
    if (col >= 0) {
        m_grid->setCurrentCell(0, col);
        if (m_grid->item(0, col))
            m_grid->scrollToItem(m_grid->item(0, col));
    }
    markDirty();
}

void DataTableEditor::removeField() {
    const int col = m_selectedField >= 0 ? m_selectedField : m_grid->currentColumn();
    if (col < 0 || col >= m_table.fields.size()) return;
    const QString field = m_table.fields[col].name;
    if (field == "id") {
        QMessageBox::warning(this, "删除字段", "不能删除主键字段 id。");
        return;
    }
    if (QMessageBox::question(this, "删除字段", QString("确定删除字段「%1」？").arg(field),
                              QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    m_table.fields.removeAt(col);
    for (DataTableRecord& r : m_table.records) r.values.remove(field);
    m_selectedField = qMin(col, m_table.fields.size() - 1);
    rebuildGrid();
    markDirty();
}

void DataTableEditor::addRecord() {
    const QString id = uniqueRecordId("record");
    DataTableRecord r;
    r.id = id;
    for (const DataTableField& f : m_table.fields)
        r.values[f.name] = f.name == "id" ? id : "";
    m_table.records << r;
    rebuildGrid();
    markDirty();
}

void DataTableEditor::removeRecord() {
    const int gridRow = m_grid->currentRow();
    const int row = gridRow - 1; // 第 0 行是字段说明，真实记录从第 1 行开始
    if (row < 0 || row >= m_table.records.size()) return;
    const QString id = m_table.records[row].id;
    if (QMessageBox::question(this, "删除记录", QString("确定删除记录「%1」？").arg(id),
                              QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    m_table.records.removeAt(row);
    rebuildGrid();
    markDirty();
}

void DataTableEditor::updateStatus() {
    const QStringList schemaErrors = m_table.validateSchema();
    const QString schemaText = schemaErrors.isEmpty()
        ? QString()
        : QString("  结构问题：%1").arg(schemaErrors.join("；"));
    m_statusLabel->setText(QString("%1%2  字段 %3  记录 %4%5")
        .arg(m_table.filePath)
        .arg(m_table.isDirty() ? "  （未保存）" : "")
        .arg(m_table.fields.size())
        .arg(m_table.records.size())
        .arg(schemaText));
}

void DataTableEditor::selectField(int col) {
    m_selectedField = (col >= 0 && col < m_table.fields.size()) ? col : -1;
    if (m_selectedField >= 0) {
        m_grid->selectColumn(m_selectedField);
        if (m_grid->rowCount() > 0)
            m_grid->setCurrentCell(qMin(1, m_grid->rowCount() - 1), m_selectedField);
    }
    updateFieldInspector();
}

void DataTableEditor::updateFieldInspector() {
    const QSignalBlocker nameBlocker(m_fieldNameEdit);
    const QSignalBlocker typeBlocker(m_fieldTypeCombo);
    const bool hasField = m_selectedField >= 0 && m_selectedField < m_table.fields.size();
    m_fieldNameEdit->setEnabled(hasField);
    m_fieldTypeCombo->setEnabled(hasField);
    if (!hasField) {
        m_fieldNameEdit->clear();
        m_fieldTypeCombo->setCurrentIndex(0);
        return;
    }
    const DataTableField& f = m_table.fields[m_selectedField];
    m_fieldNameEdit->setText(f.name);
    const int idx = m_fieldTypeCombo->findText(f.type);
    m_fieldTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    const bool isKeyField = f.name == m_table.keyField;
    m_fieldNameEdit->setEnabled(!isKeyField);
    m_fieldTypeCombo->setEnabled(!isKeyField);
}

void DataTableEditor::renameSelectedField() {
    if (m_rebuilding || m_selectedField < 0 || m_selectedField >= m_table.fields.size()) return;
    const QString oldName = m_table.fields[m_selectedField].name;
    if (oldName == m_table.keyField) { updateFieldInspector(); return; }
    const QString newName = m_fieldNameEdit->text().trimmed();
    if (newName.isEmpty() || newName == oldName) { updateFieldInspector(); return; }
    if (m_table.hasField(newName)) { updateFieldInspector(); return; }

    m_table.fields[m_selectedField].name = newName;
    for (DataTableRecord& r : m_table.records) {
        const QJsonValue v = r.values.value(oldName);
        r.values.remove(oldName);
        r.values[newName] = v;
    }
    rebuildGrid();
    markDirty();
}

void DataTableEditor::changeSelectedFieldType(const QString& type) {
    if (m_rebuilding || m_selectedField < 0 || m_selectedField >= m_table.fields.size()) return;
    DataTableField& f = m_table.fields[m_selectedField];
    if (f.name == m_table.keyField || f.type == type) return;
    f.type = type;
    rebuildGrid();
    markDirty();
}

void DataTableEditor::syncFieldOrderFromHeader() {
    if (m_rebuilding || m_movingField) return;
    auto* header = m_grid->horizontalHeader();
    if (!header || header->count() != m_table.fields.size()) return;

    m_movingField = true;
    const QString selectedName = (m_selectedField >= 0 && m_selectedField < m_table.fields.size())
        ? m_table.fields[m_selectedField].name
        : QString();
    QList<DataTableField> ordered;
    for (int visual = 0; visual < header->count(); ++visual) {
        const int logical = header->logicalIndex(visual);
        if (logical >= 0 && logical < m_table.fields.size())
            ordered << m_table.fields[logical];
    }
    if (ordered.size() == m_table.fields.size())
        m_table.fields = ordered;
    m_selectedField = selectedName.isEmpty() ? -1 : m_table.fieldIndex(selectedName);
    rebuildGrid();
    markDirty();
    m_movingField = false;
}

QString DataTableEditor::uniqueFieldName(const QString& base) const {
    QString candidate = base + "_1";
    int i = 1;
    while (m_table.hasField(candidate)) candidate = base + "_" + QString::number(++i);
    return candidate;
}

QString DataTableEditor::uniqueRecordId(const QString& base) const {
    QString candidate = base;
    int i = 1;
    while (m_table.recordIndex(candidate) >= 0) candidate = base + QString::number(++i);
    return candidate;
}
