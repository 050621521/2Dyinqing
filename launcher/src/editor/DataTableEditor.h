#pragma once

#include "models/DataTable.h"
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class DataTableEditor : public QWidget {
    Q_OBJECT
public:
    explicit DataTableEditor(QWidget* parent = nullptr);

    bool load(const QString& path);
    bool save();
    bool isDirty() const { return m_table.isDirty(); }
    QString filePath() const { return m_table.filePath; }
    QString tableName() const { return m_table.name; }

signals:
    void modified();
    void saved();

private:
    void rebuildGrid();
    void markDirty();
    void addField();
    void removeField();
    void addRecord();
    void removeRecord();
    void updateStatus();
    void selectField(int col);
    void updateFieldInspector();
    void renameSelectedField();
    void changeSelectedFieldType(const QString& type);
    void syncFieldOrderFromHeader();
    QString uniqueFieldName(const QString& base) const;
    QString uniqueRecordId(const QString& base) const;

    DataTable m_table;
    bool m_rebuilding = false;
    bool m_movingField = false;
    int m_selectedField = -1;

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_schemaCombo = nullptr;
    QLineEdit* m_fieldNameEdit = nullptr;
    QComboBox* m_fieldTypeCombo = nullptr;
    QTableWidget* m_grid = nullptr;
    QLabel* m_statusLabel = nullptr;
};
