#pragma once

#include "BPValue.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

struct DataTableField {
    QString name;
    QString type = "string";
    QString comment;

    QJsonObject toJson() const;
    static DataTableField fromJson(const QJsonObject& obj);
};

struct DataTableRecord {
    QString id;
    QJsonObject values;

    QJsonObject toJson() const;
    static DataTableRecord fromJson(const QJsonObject& obj);
};

struct DataTableSchemaField {
    QString name;
    QString type = "string";
    bool required = false;
    bool unique = false;
    QJsonValue defaultValue;
    QString assetType;
    QString enumName;
};

struct DataTableSchema {
    QString name;
    QString displayName;
    QList<DataTableSchemaField> fields;

    bool isValid() const { return !name.isEmpty(); }
    static QStringList builtinSchemaNames();
    static DataTableSchema builtinSchema(const QString& schemaName);
};

class DataTable {
public:
    QString name;
    QString filePath;
    QString keyField = "id";
    QString schema;
    QList<DataTableField> fields;
    QList<DataTableRecord> records;

    bool load(const QString& path);
    bool save() const;
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    bool hasField(const QString& fieldName) const;
    int fieldIndex(const QString& fieldName) const;
    int recordIndex(const QString& recordId) const;
    BPValue value(const QString& recordId, const QString& fieldName) const;
    int recordCount() const { return records.size(); }
    QStringList validateSchema(QString schemaName = {}, const QString& projectRoot = {}) const;

    QJsonObject toJson() const;
    static DataTable fromJson(const QJsonObject& obj, const QString& path = {});
    static DataTable makeDefault(const QString& name, const QString& path);

private:
    bool m_dirty = false;
};
