#include "LevelDocument.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QSaveFile>

// ── ActorData ─────────────────────────────────────────────────────────

QJsonObject ActorData::toJson() const {
    QJsonObject pos; pos["x"] = x; pos["y"] = y;
    QJsonObject scale; scale["x"] = scaleX; scale["y"] = scaleY;
    QJsonObject obj;
    obj["id"]          = id;
    obj["name"]        = name;
    obj["bpClass"]     = bpClass;
    obj["position"]    = pos;
    obj["rotation"]    = rotation;
    obj["scale"]       = scale;
    obj["active"]      = active;
    obj["static"]      = isStatic;
    obj["tag"]         = tag;
    obj["layer"]       = layer;
    QJsonArray compArr;
    for (const QString& c : components) compArr.append(c);
    obj["components"]  = compArr;
    if (!overriddenFields.isEmpty()) {
        QJsonArray ovArr;
        for (const QString& f : overriddenFields) ovArr.append(f);
        obj["overriddenFields"] = ovArr;
    }
    obj["spritePath"]  = spritePath;
    obj["spriteColor"] = spriteColor.name(QColor::HexArgb);
    obj["sortingLayer"]= sortingLayer;
    obj["orderInLayer"]= orderInLayer;
    obj["flipX"]         = flipX;
    obj["flipY"]         = flipY;
    obj["drawMode"]      = drawMode;
    obj["spriteVisible"] = spriteVisible;
    obj["animAsset"]       = animAsset;
    obj["animDefaultClip"] = animDefaultClip;
    obj["animAutoPlay"]    = animAutoPlay;
    obj["cameraSize"]         = cameraSize;
    obj["cameraIsMain"]       = cameraIsMain;
    obj["cameraResW"]         = cameraResW;
    obj["cameraResH"]         = cameraResH;
    obj["cameraOrthographic"] = cameraOrthographic;
    obj["cameraBackground"]   = cameraBackground.name(QColor::HexArgb);
    obj["followTarget"]       = followTarget;
    obj["followLerpSpeed"]    = followLerpSpeed;
    obj["followOffsetX"]      = followOffsetX;
    obj["followOffsetY"]      = followOffsetY;
    obj["confinerEnabled"]    = confinerEnabled;
    obj["confinerActor"]      = confinerActor;
    obj["confinerMinX"]       = confinerMinX;
    obj["confinerMaxX"]       = confinerMaxX;
    obj["confinerMinY"]       = confinerMinY;
    obj["confinerMaxY"]       = confinerMaxY;
    return obj;
}

ActorData ActorData::fromJson(const QJsonObject& obj) {
    ActorData a;
    a.id       = obj["id"].toString();
    a.name     = obj["name"].toString();
    // 旧格式迁移："type": "Sprite" → bpClass: "builtin/Sprite"
    if (obj.contains("bpClass"))
        a.bpClass = obj["bpClass"].toString();
    else if (obj.contains("type"))
        a.bpClass = "builtin/" + obj["type"].toString();
    a.rotation = (float)obj["rotation"].toDouble();
    const QJsonObject pos   = obj["position"].toObject();
    const QJsonObject scale = obj["scale"].toObject();
    a.x        = (float)pos["x"].toDouble();
    a.y        = (float)pos["y"].toDouble();
    a.scaleX   = (float)scale["x"].toDouble(1.0);
    a.scaleY   = (float)scale["y"].toDouble(1.0);
    a.active   = obj["active"].toBool(true);
    a.isStatic = obj["static"].toBool(false);
    a.tag      = obj["tag"].toString("未标记");
    a.layer    = obj["layer"].toString("默认");
    for (const QJsonValue& v : obj["components"].toArray())
        a.components.append(v.toString());
    a.spritePath   = obj["spritePath"].toString();
    a.spriteColor  = QColor(obj["spriteColor"].toString("#ffffffff"));
    a.sortingLayer = obj["sortingLayer"].toString("默认");
    a.orderInLayer = obj["orderInLayer"].toInt(0);
    a.flipX          = obj["flipX"].toBool(false);
    a.flipY          = obj["flipY"].toBool(false);
    a.drawMode       = obj["drawMode"].toString("简单");
    a.spriteVisible  = obj["spriteVisible"].toBool(true);
    a.animAsset       = obj["animAsset"].toString();
    a.animDefaultClip = obj["animDefaultClip"].toString();
    a.animAutoPlay    = obj["animAutoPlay"].toBool(true);
    a.cameraSize         = (float)obj["cameraSize"].toDouble(5.0);
    a.cameraIsMain = obj["cameraIsMain"].toBool(false);
    a.cameraResW   = obj["cameraResW"].toInt(1920);
    a.cameraResH   = obj["cameraResH"].toInt(1080);
    a.cameraOrthographic = obj["cameraOrthographic"].toBool(true);
    a.cameraBackground   = QColor(obj["cameraBackground"].toString("#ff1e1e1e"));
    a.followTarget       = obj["followTarget"].toString();
    a.followLerpSpeed    = (float)obj["followLerpSpeed"].toDouble(5.0);
    a.followOffsetX      = (float)obj["followOffsetX"].toDouble(0.0);
    a.followOffsetY      = (float)obj["followOffsetY"].toDouble(0.0);
    a.confinerEnabled    = obj["confinerEnabled"].toBool(false);
    a.confinerActor      = obj["confinerActor"].toString();
    a.confinerMinX       = (float)obj["confinerMinX"].toDouble(-500.0);
    a.confinerMaxX       = (float)obj["confinerMaxX"].toDouble(500.0);
    a.confinerMinY       = (float)obj["confinerMinY"].toDouble(-500.0);
    a.confinerMaxY       = (float)obj["confinerMaxY"].toDouble(500.0);

    // 活继承覆盖集合
    if (obj.contains("overriddenFields")) {
        for (const QJsonValue& v : obj["overriddenFields"].toArray())
            a.overriddenFields.insert(v.toString());
    } else if (!a.bpClass.startsWith("builtin/")) {
        // 旧关卡无此键：视为全部字段已覆盖（保持快照值，不因引入活继承而突变）
        const QJsonObject all = a.toJson();
        for (const QString& k : all.keys())
            if (k != "id" && k != "name" && k != "components" && k != "overriddenFields")
                a.overriddenFields.insert(k);
    }
    return a;
}

// ── BPNode ────────────────────────────────────────────────────────────

QJsonObject BPNode::toJson() const {
    QJsonObject obj;
    obj["id"]   = id;
    obj["type"] = type;
    obj["x"]    = x;
    obj["y"]    = y;
    QJsonObject paramsObj;
    for (auto it = params.begin(); it != params.end(); ++it)
        paramsObj[it.key()] = it.value();
    obj["params"] = paramsObj;
    return obj;
}

BPNode BPNode::fromJson(const QJsonObject& obj) {
    BPNode n;
    n.id   = obj["id"].toString();
    n.type = obj["type"].toString();
    n.x    = (float)obj["x"].toDouble(100.0);
    n.y    = (float)obj["y"].toDouble(100.0);
    const QJsonObject paramsObj = obj["params"].toObject();
    for (auto it = paramsObj.begin(); it != paramsObj.end(); ++it)
        n.params[it.key()] = it.value().toString();
    return n;
}

// ── BPConnection ──────────────────────────────────────────────────────

QJsonObject BPConnection::toJson() const {
    QJsonObject obj;
    obj["id"]       = id;
    obj["fromNode"] = fromNode;
    obj["fromPin"]  = fromPin;
    obj["toNode"]   = toNode;
    obj["toPin"]    = toPin;
    return obj;
}

BPConnection BPConnection::fromJson(const QJsonObject& obj) {
    BPConnection c;
    c.id       = obj["id"].toString();
    c.fromNode = obj["fromNode"].toString();
    c.fromPin  = obj["fromPin"].toString();
    c.toNode   = obj["toNode"].toString();
    c.toPin    = obj["toPin"].toString();
    return c;
}

// ── LevelDocument ─────────────────────────────────────────────────────

static int sortingLayerPriority(const QString& layer) {
    if (layer == "背景") return 0;
    if (layer == "前景") return 2;
    if (layer == "界面") return 3;
    if (layer == "物理") return 4;
    return 1;
}

void LevelDocument::rebuildSortedActors() {
    m_sortedActors = m_actors;
    std::stable_sort(m_sortedActors.begin(), m_sortedActors.end(),
                     [](const ActorData& a, const ActorData& b) {
        int la = sortingLayerPriority(a.sortingLayer);
        int lb = sortingLayerPriority(b.sortingLayer);
        return la != lb ? la < lb : a.orderInLayer < b.orderInLayer;
    });
}

bool LevelDocument::load(const QString& filePath) {
    m_filePath = filePath;
    m_actors.clear();

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;

    const QJsonObject root = doc.object();
    m_name = root["name"].toString();

    for (const QJsonValue& v : root["objects"].toArray())
        m_actors.append(ActorData::fromJson(v.toObject()));

    rebuildSortedActors();

    m_bpNodes.clear();
    m_bpConnections.clear();
    const QJsonObject bp = root["blueprint"].toObject();
    for (const QJsonValue& v : bp["nodes"].toArray())
        m_bpNodes.append(BPNode::fromJson(v.toObject()));
    for (const QJsonValue& v : bp["connections"].toArray())
        m_bpConnections.append(BPConnection::fromJson(v.toObject()));
    m_localVars.clear();
    for (const QJsonValue& v : bp["localVariables"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString nm = o["name"].toString();
        if (!nm.isEmpty()) m_localVars.append({nm, o["type"].toString("string")});
    }

    m_dirty = false;
    return true;
}

bool LevelDocument::save() {
    QJsonArray arr;
    for (const ActorData& a : m_actors)
        arr.append(a.toJson());

    QJsonArray nodesArr, connsArr;
    for (const BPNode& n : m_bpNodes)             nodesArr.append(n.toJson());
    for (const BPConnection& c : m_bpConnections) connsArr.append(c.toJson());
    QJsonArray localVarsArr;
    for (const GlobalVarDef& d : m_localVars) {
        QJsonObject o; o["name"] = d.name; o["type"] = d.type;
        localVarsArr.append(o);
    }
    QJsonObject bp;
    bp["nodes"]          = nodesArr;
    bp["connections"]    = connsArr;
    bp["localVariables"] = localVarsArr;

    QJsonObject root;
    root["name"]      = m_name;
    root["version"]   = "0.1";
    root["objects"]   = arr;
    root["blueprint"] = bp;

    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) return false;
    m_dirty = false;
    return true;
}

void LevelDocument::addActor(const ActorData& actor) {
    ActorData a = actor;
    if (a.id.isEmpty())
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_actors.append(a);
    rebuildSortedActors();
    m_dirty = true;
}

void LevelDocument::removeActor(const QString& id) {
    for (int i = 0; i < m_actors.size(); ++i) {
        if (m_actors[i].id == id) {
            m_actors.removeAt(i);
            rebuildSortedActors();
            m_dirty = true;
            break;
        }
    }
}

void LevelDocument::updateActor(const ActorData& actor) {
    for (ActorData& a : m_actors) {
        if (a.id == actor.id) {
            a = actor;
            rebuildSortedActors();
            m_dirty = true;
            break;
        }
    }
}

// ── Blueprint 增删改 ──────────────────────────────────────────────────

void LevelDocument::addBPNode(const BPNode& node) {
    m_bpNodes.append(node);
    m_dirty = true;
}

void LevelDocument::removeBPNode(const QString& id) {
    for (int i = 0; i < m_bpNodes.size(); ++i) {
        if (m_bpNodes[i].id == id) {
            m_bpNodes.removeAt(i);
            m_dirty = true;
            break;
        }
    }
}

void LevelDocument::updateBPNode(const BPNode& node) {
    for (BPNode& n : m_bpNodes) {
        if (n.id == node.id) {
            n = node;
            m_dirty = true;
            break;
        }
    }
}

void LevelDocument::addBPConnection(const BPConnection& conn) {
    m_bpConnections.append(conn);
    m_dirty = true;
}

void LevelDocument::removeBPConnection(const QString& id) {
    for (int i = 0; i < m_bpConnections.size(); ++i) {
        if (m_bpConnections[i].id == id) {
            m_bpConnections.removeAt(i);
            m_dirty = true;
            break;
        }
    }
}

void LevelDocument::setLocalVars(const QList<GlobalVarDef>& vars) {
    m_localVars = vars;
    m_dirty = true;
}

LevelDocument::~LevelDocument() { delete m_undoStack; }

QUndoStack* LevelDocument::undoStack() {
    if (!m_undoStack) m_undoStack = new QUndoStack();
    return m_undoStack;
}
