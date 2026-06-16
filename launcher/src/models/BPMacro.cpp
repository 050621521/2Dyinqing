#include "BPMacro.h"
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>

// ── MacroPin ──────────────────────────────────────────────────────────

QJsonObject MacroPin::toJson() const {
    QJsonObject o;
    o["key"]    = key;
    o["label"]  = label;
    o["isExec"] = isExec;
    o["kind"]   = kind;
    return o;
}

MacroPin MacroPin::fromJson(const QJsonObject& obj) {
    MacroPin p;
    p.key    = obj["key"].toString();
    p.label  = obj["label"].toString();
    p.isExec = obj["isExec"].toBool();
    p.kind   = obj.value("kind").toString("Text");
    return p;
}

// ── BPMacro ───────────────────────────────────────────────────────────

QJsonObject BPMacro::toJson() const {
    QJsonObject obj;
    obj["id"]   = id;
    obj["name"] = name;
    QJsonArray ins;
    for (const MacroPin& p : inputPins)  ins.append(p.toJson());
    obj["inputPins"] = ins;
    QJsonArray outs;
    for (const MacroPin& p : outputPins) outs.append(p.toJson());
    obj["outputPins"] = outs;
    QJsonArray nodesArr;
    for (const BPNode& n : nodes) nodesArr.append(n.toJson());
    obj["nodes"] = nodesArr;
    QJsonArray connsArr;
    for (const BPConnection& c : connections) connsArr.append(c.toJson());
    obj["connections"] = connsArr;
    return obj;
}

BPMacro BPMacro::fromJson(const QJsonObject& obj, const QString& fp) {
    BPMacro m;
    m.id       = obj["id"].toString();
    m.name     = obj["name"].toString();
    m.filePath = fp;
    for (const QJsonValue& v : obj["inputPins"].toArray())
        m.inputPins.append(MacroPin::fromJson(v.toObject()));
    for (const QJsonValue& v : obj["outputPins"].toArray())
        m.outputPins.append(MacroPin::fromJson(v.toObject()));
    for (const QJsonValue& v : obj["nodes"].toArray())
        m.nodes.append(BPNode::fromJson(v.toObject()));
    for (const QJsonValue& v : obj["connections"].toArray())
        m.connections.append(BPConnection::fromJson(v.toObject()));
    return m;
}

bool BPMacro::save() const {
    if (filePath.isEmpty()) return false;
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson());
    return f.commit();
}

BPMacro BPMacro::load(const QString& fp) {
    QFile f(fp);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return fromJson(obj, fp);
}

QString BPMacro::macrosDir(const QString& projectRoot) {
    return projectRoot + "/Blueprints/Macros";
}

QList<BPMacro> BPMacro::listAll(const QString& projectRoot) {
    QList<BPMacro> out;
    if (projectRoot.isEmpty()) return out;
    QDir dir(macrosDir(projectRoot));
    for (const QString& fn : dir.entryList({"*.bpmacro"}, QDir::Files, QDir::Name))
        out.append(load(dir.absoluteFilePath(fn)));
    return out;
}
