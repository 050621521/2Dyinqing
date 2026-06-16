#include "GlobalVars.h"
#include <QFile>
#include <QSaveFile>
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

QList<EnumDef> Enums::load(const QString& projectRoot) {
    QList<EnumDef> out;
    if (projectRoot.isEmpty()) return out;
    QFile f(projectJsonPath(projectRoot));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : obj.value("enums").toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        if (name.isEmpty()) continue;
        EnumDef e; e.name = name;
        for (const QJsonValue& vv : o.value("values").toArray())
            e.values.append(vv.toString());
        out.append(e);
    }
    return out;
}

bool Enums::save(const QString& projectRoot, const QList<EnumDef>& enums) {
    if (projectRoot.isEmpty()) return false;
    QJsonObject obj;
    QFile rf(projectJsonPath(projectRoot));
    if (rf.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(rf.readAll()).object();
        rf.close();
    }
    QJsonArray arr;
    for (const EnumDef& e : enums) {
        QJsonObject o;
        o["name"] = e.name;
        QJsonArray vals;
        for (const QString& v : e.values) vals.append(v);
        o["values"] = vals;
        arr.append(o);
    }
    obj["enums"] = arr;
    QSaveFile wf(projectJsonPath(projectRoot));
    if (!wf.open(QIODevice::WriteOnly)) return false;
    wf.write(QJsonDocument(obj).toJson());
    return wf.commit();
}

QStringList Enums::valuesOf(const QList<EnumDef>& enums, const QString& name) {
    for (const EnumDef& e : enums) if (e.name == name) return e.values;
    return {};
}

QString GlobalVars::typeLabel(const QString& type) {
    if (type == "number") return "数值";
    if (type == "bool")   return "布尔";
    if (type == "string") return "字符串";
    if (type.startsWith("enum:")) return "枚举(" + type.mid(5) + ")";
    return type;
}
