#include "DataTable.h"
#include "AssetRef.h"
#include "AssetRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QSet>

static QString inferProjectRootForTable(const QString& filePath) {
    QDir dir(QFileInfo(filePath).absolutePath());
    while (dir.exists() && !dir.isRoot()) {
        if (QFileInfo::exists(dir.filePath("asset_registry.json"))
            || QFileInfo::exists(dir.filePath("project.json")))
            return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QFileInfo(filePath).absolutePath();
}

static bool valueIsMissing(const QJsonValue& v) {
    if (v.isUndefined() || v.isNull()) return true;
    if (v.isString()) return v.toString().trimmed().isEmpty();
    return false;
}

static bool valueMatchesType(const QJsonValue& v, const QString& type) {
    if (valueIsMissing(v)) return true;
    if (type == "number") {
        if (v.isDouble()) return true;
        if (v.isString()) {
            bool ok = false;
            v.toString().toDouble(&ok);
            return ok;
        }
        return false;
    }
    if (type == "bool") {
        if (v.isBool()) return true;
        if (v.isString()) {
            const QString s = v.toString().trimmed().toLower();
            return QStringList{"true", "false", "1", "0", "是", "否"}.contains(s);
        }
        return false;
    }
    return true;
}

QStringList DataTableSchema::builtinSchemaNames() {
    return {"CardTable"};
}

DataTableSchema DataTableSchema::builtinSchema(const QString& schemaName) {
    if (schemaName != "CardTable") return {};
    DataTableSchema s;
    s.name = "CardTable";
    s.displayName = "卡牌表";
    s.fields = {
        {"id", "string", true, true, {}, {}, {}},
        {"name", "string", true, false, {}, {}, {}},
        {"energy_cost", "number", true, false, 0, {}, {}},
        {"target", "string", true, false, "enemy", {}, "self,enemy"},
        {"effect", "effect", true, false, {}, "bp.effect", {}},
        {"effect_value", "number", true, false, 0, {}, {}},
        {"description", "string", false, false, {}, {}, {}}
    };
    return s;
}

QJsonObject DataTableField::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["type"] = type;
    obj["comment"] = comment;
    return obj;
}

DataTableField DataTableField::fromJson(const QJsonObject& obj) {
    DataTableField f;
    f.name = obj.value("name").toString();
    f.type = obj.value("type").toString("string");
    f.comment = obj.value("comment").toString();
    return f;
}

QJsonObject DataTableRecord::toJson() const {
    QJsonObject obj = values;
    obj["id"] = id;
    return obj;
}

DataTableRecord DataTableRecord::fromJson(const QJsonObject& obj) {
    DataTableRecord r;
    r.id = obj.value("id").toString();
    r.values = obj;
    if (!r.id.isEmpty()) r.values["id"] = r.id;
    return r;
}

bool DataTable::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    *this = fromJson(doc.object(), path);
    m_dirty = false;
    return true;
}

bool DataTable::save() const {
    if (filePath.isEmpty()) return false;
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool DataTable::hasField(const QString& fieldName) const {
    return fieldIndex(fieldName) >= 0;
}

int DataTable::fieldIndex(const QString& fieldName) const {
    for (int i = 0; i < fields.size(); ++i)
        if (fields[i].name == fieldName) return i;
    return -1;
}

int DataTable::recordIndex(const QString& recordId) const {
    for (int i = 0; i < records.size(); ++i)
        if (records[i].id == recordId) return i;
    return -1;
}

BPValue DataTable::value(const QString& recordId, const QString& fieldName) const {
    const int idx = recordIndex(recordId);
    if (idx < 0) return {};
    const QJsonValue v = records[idx].values.value(fieldName);
    if (v.isUndefined()) return {};
    return BPValue::fromJsonValue(v);
}

QJsonObject DataTable::toJson() const {
    QJsonObject obj;
    obj["assetType"] = "数据表";
    obj["name"] = name;
    obj["keyField"] = keyField;
    if (!schema.trimmed().isEmpty()) obj["schema"] = schema;

    QJsonArray fieldArr;
    for (const DataTableField& f : fields) fieldArr.append(f.toJson());
    obj["fields"] = fieldArr;

    QHash<QString, QString> fieldTypes;
    for (const DataTableField& f : fields)
        fieldTypes.insert(f.name, f.type);

    QJsonArray recordArr;
    for (const DataTableRecord& r : records) {
        QJsonObject recordObj = r.toJson();
        for (auto it = recordObj.begin(); it != recordObj.end(); ++it) {
            const QString type = fieldTypes.value(it.key());
            if ((type == "effect" || type == "asset") && it.value().isString())
                it.value() = SoftAssetRef::fromString(it.value().toString(), type == "effect" ? "bp.effect" : QString()).toJson();
        }
        recordArr.append(recordObj);
    }
    obj["records"] = recordArr;
    return obj;
}

DataTable DataTable::fromJson(const QJsonObject& obj, const QString& path) {
    DataTable t;
    t.filePath = path;
    t.name = obj.value("name").toString(QFileInfo(path).baseName());
    t.keyField = obj.value("keyField").toString("id");
    t.schema = obj.value("schema").toString();

    for (const QJsonValue& v : obj.value("fields").toArray()) {
        const DataTableField f = DataTableField::fromJson(v.toObject());
        if (!f.name.isEmpty()) t.fields << f;
    }
    if (!t.hasField("id")) t.fields.prepend({"id", "string", "记录唯一标识"});

    for (const QJsonValue& v : obj.value("records").toArray()) {
        DataTableRecord r = DataTableRecord::fromJson(v.toObject());
        if (!r.id.isEmpty()) t.records << r;
    }
    return t;
}

QStringList DataTable::validateSchema(QString schemaName, const QString& projectRoot) const {
    const QString s = schemaName.trimmed().isEmpty() ? schema.trimmed() : schemaName.trimmed();
    QStringList errors;
    if (s.isEmpty()) return errors;
    const DataTableSchema schemaDef = DataTableSchema::builtinSchema(s);
    if (!schemaDef.isValid()) {
        errors << QString("未知数据表结构：%1").arg(s);
        return errors;
    }

    for (const DataTableSchemaField& want : schemaDef.fields) {
        const int idx = fieldIndex(want.name);
        if (idx < 0) {
            if (want.required)
                errors << QString("缺少字段：%1").arg(want.name);
            continue;
        }
        const QString actualType = fields[idx].type;
        if (actualType != want.type)
            errors << QString("字段类型不匹配：%1 需要 %2，当前 %3").arg(want.name, want.type, actualType);
    }

    const QString root = projectRoot.trimmed().isEmpty() ? inferProjectRootForTable(filePath) : projectRoot;
    AssetRegistry registry(root);
    registry.load();

    for (const DataTableSchemaField& want : schemaDef.fields) {
        if (fieldIndex(want.name) < 0) continue;
        QSet<QString> seen;
        for (int row = 0; row < records.size(); ++row) {
            const DataTableRecord& record = records[row];
            const QString recordLabel = record.id.isEmpty()
                ? QString("第%1条记录").arg(row + 1)
                : QString("记录 %1").arg(record.id);
            const QJsonValue value = want.name == "id" ? QJsonValue(record.id) : record.values.value(want.name);
            if (want.required && valueIsMissing(value)) {
                errors << QString("%1 缺少必填值：%2").arg(recordLabel, want.name);
                continue;
            }
            if (!want.enumName.isEmpty() && !valueIsMissing(value)) {
                QStringList allowed;
                QString enumSpec = want.enumName;
                enumSpec.replace('|', ',');
                for (const QString& part : enumSpec.split(',', Qt::SkipEmptyParts))
                    allowed << part.trimmed();
                if (!allowed.isEmpty() && !allowed.contains(value.toVariant().toString()))
                    errors << QString("%1 枚举值无效：%2 = %3").arg(recordLabel, want.name, value.toVariant().toString());
            }
            if (!valueMatchesType(value, want.type)) {
                errors << QString("%1 字段值类型错误：%2 需要 %3").arg(recordLabel, want.name, want.type);
                continue;
            }
            if (want.unique && !valueIsMissing(value)) {
                const QString key = value.toVariant().toString();
                if (seen.contains(key))
                    errors << QString("字段值重复：%1 = %2").arg(want.name, key);
                seen.insert(key);
            }
            if (!want.assetType.isEmpty() && !valueIsMissing(value)) {
                const SoftAssetRef ref = SoftAssetRef::fromVariant(value, want.assetType);
                AssetRecord asset;
                if (!ref.assetId.isEmpty()) asset = registry.findById(ref.assetId);
                if (asset.path.isEmpty() && !ref.path.isEmpty()) asset = registry.findByPath(ref.path);
                if (asset.path.isEmpty()) {
                    errors << QString("%1 资产引用不存在：%2").arg(recordLabel, ref.path.isEmpty() ? ref.assetId : ref.path);
                } else if (asset.type != want.assetType) {
                    errors << QString("%1 资产引用类型错误：%2 需要 %3，当前 %4")
                                  .arg(recordLabel, want.name, want.assetType, asset.type);
                }
            }
        }
    }
    return errors;
}

DataTable DataTable::makeDefault(const QString& name, const QString& path) {
    DataTable t;
    t.name = name;
    t.filePath = path;
    t.keyField = "id";
    t.fields = {
        {"id", "string", "记录唯一标识"},
        {"name", "string", "显示名称"},
        {"description", "string", "说明文本"}
    };
    t.records = {
        {"sample", QJsonObject{{"id", "sample"}, {"name", "示例记录"}, {"description", ""}}}
    };
    return t;
}
