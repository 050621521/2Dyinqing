#include "GlobalVars.h"
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static QString projectJsonPath(const QString& projectRoot) {
    return projectRoot + "/project.json";
}

QList<GlobalVarDef> GlobalVars::load(const QString& projectRoot) {
    QList<GlobalVarDef> out;
    if (projectRoot.isEmpty()) return out;
    QFile f(projectJsonPath(projectRoot));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : obj.value("globalVariables").toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        if (name.isEmpty()) continue;
        out.append({name, o.value("type").toString("string")});
    }
    return out;
}

bool GlobalVars::save(const QString& projectRoot, const QList<GlobalVarDef>& vars) {
    if (projectRoot.isEmpty()) return false;
    // read-modify-write：保留 project.json 其它字段
    QJsonObject obj;
    QFile rf(projectJsonPath(projectRoot));
    if (rf.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(rf.readAll()).object();
        rf.close();
    }
    QJsonArray arr;
    for (const GlobalVarDef& d : vars) {
        QJsonObject o;
        o["name"] = d.name;
        o["type"] = d.type;
        arr.append(o);
    }
    obj["globalVariables"] = arr;

    QSaveFile wf(projectJsonPath(projectRoot));
    if (!wf.open(QIODevice::WriteOnly)) return false;
    wf.write(QJsonDocument(obj).toJson());
    return wf.commit();
}

// ── 枚举资产：单个 .enum 文件 ──────────────────────────────────────────

bool EnumDef::save(const QString& fp) const {
    QJsonObject o;
    o["name"] = name;
    QJsonArray vals, disps, descs;
    for (const QString& v : values) vals.append(v);
    for (int i = 0; i < values.size(); ++i) {
        disps.append(i < displays.size()     ? displays[i]     : QString());
        descs.append(i < descriptions.size() ? descriptions[i] : QString());
    }
    o["values"]       = vals;
    o["displays"]     = disps;
    o["descriptions"] = descs;
    QDir().mkpath(QFileInfo(fp).absolutePath());
    QSaveFile wf(fp);
    if (!wf.open(QIODevice::WriteOnly)) return false;
    wf.write(QJsonDocument(o).toJson());
    return wf.commit();
}

EnumDef EnumDef::load(const QString& fp) {
    EnumDef e;
    e.filePath = fp;
    QFile f(fp);
    if (!f.open(QIODevice::ReadOnly)) return e;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    e.name = o.value("name").toString(QFileInfo(fp).baseName());
    for (const QJsonValue& v : o.value("values").toArray())
        e.values.append(v.toString());
    for (const QJsonValue& v : o.value("displays").toArray())
        e.displays.append(v.toString());
    for (const QJsonValue& v : o.value("descriptions").toArray())
        e.descriptions.append(v.toString());
    while (e.displays.size()     < e.values.size()) e.displays.append(QString());
    while (e.descriptions.size() < e.values.size()) e.descriptions.append(QString());
    return e;
}

QList<EnumDef> Enums::loadAll(const QString& projectRoot) {
    QList<EnumDef> out;
    if (projectRoot.isEmpty()) return out;
    QDirIterator it(projectRoot, {"*.enum"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out.append(EnumDef::load(it.next()));
    return out;
}

QStringList Enums::valuesOf(const QList<EnumDef>& enums, const QString& name) {
    for (const EnumDef& e : enums) if (e.name == name) return e.values;
    return {};
}

QString GlobalVars::typeLabel(const QString& type) {
    if (type == "number") return "数值";
    if (type == "bool")   return "布尔";
    if (type == "string") return "字符串";
    if (type.startsWith("enum:"))  return "枚举(" + type.mid(5) + ")";
    if (type.startsWith("array:")) return "数组(" + typeLabel(type.mid(6)) + ")";
    return type;
}
