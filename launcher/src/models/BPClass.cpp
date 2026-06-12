#include "BPClass.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>

QJsonObject BPClass::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    QJsonArray comps;
    for (const QString& c : components) comps.append(c);
    obj["components"] = comps;
    QJsonArray nodesArr;
    for (const BPNode& n : nodes) nodesArr.append(n.toJson());
    obj["nodes"] = nodesArr;
    QJsonArray connsArr;
    for (const BPConnection& c : connections) connsArr.append(c.toJson());
    obj["connections"] = connsArr;
    return obj;
}

BPClass BPClass::fromJson(const QJsonObject& obj, const QString& fp) {
    BPClass bc;
    bc.name     = obj["name"].toString();
    bc.filePath = fp;
    for (const QJsonValue& v : obj["components"].toArray())
        bc.components.append(v.toString());
    for (const QJsonValue& v : obj["nodes"].toArray())
        bc.nodes.append(BPNode::fromJson(v.toObject()));
    for (const QJsonValue& v : obj["connections"].toArray())
        bc.connections.append(BPConnection::fromJson(v.toObject()));
    return bc;
}

bool BPClass::save() const {
    if (filePath.isEmpty()) return false;
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson());
    return f.commit();
}

BPClass BPClass::load(const QString& fp) {
    QFile f(fp);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return fromJson(obj, fp);
}

const BPClass* BPClass::findBuiltin(const QString& bpClass) {
    static const QList<BPClass> list = builtinClasses();
    for (const BPClass& bc : list) {
        if ("builtin/" + bc.name == bpClass) return &bc;
    }
    return nullptr;
}

QList<BPClass> BPClass::builtinClasses() {
    return {
        { "Sprite",  {}, {"变换", "精灵渲染器"},  {}, {}, {} },
        { "Camera",  {}, {"变换", "摄像机组件"},   {}, {}, {} },
        { "Light",   {}, {"变换", "点光源"},       {}, {}, {} },
        { "Trigger", {}, {"变换", "碰撞盒"},       {}, {}, {} },
        { "Empty",   {}, {"变换"},                {}, {}, {} },
    };
}
