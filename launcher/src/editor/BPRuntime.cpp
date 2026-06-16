#include "BPRuntime.h"
#include "UIRuntime.h"
#include <QSet>
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QDir>
#include <QFileInfo>
#include "models/BPMacro.h"
#include <algorithm>
#include <cmath>

static std::pair<QString,QString> splitWidgetRef(const QString& ref) {
    int sep = ref.indexOf("::");
    if (sep < 0) return {ref, {}};
    return {ref.left(sep), ref.mid(sep + 2)};
}

void BPRuntime::setUIRuntime(UIRuntime* ui) {
    m_uiRuntime = ui;
    if (!ui) return;
    connect(ui, &UIRuntime::buttonClicked,
            this, &BPRuntime::triggerButtonClick);
    connect(ui, &UIRuntime::dropdownChanged,
            this, &BPRuntime::triggerDropdownChanged);
}

BPRuntime::BPRuntime(const LevelDocument* doc, QObject* parent)
    : QObject(parent)
{
    if (!doc) return;
    m_nodes       = doc->bpNodes();
    m_connections = doc->bpConnections();
    m_actors      = doc->actors();

    // 工程根：关卡在 {project}/Levels/x.level，上溯一级
    QString projectRoot;
    if (!doc->filePath().isEmpty()) {
        QDir d(QFileInfo(doc->filePath()).absolutePath());
        d.cdUp();
        projectRoot = d.absolutePath();
    }
    flattenMacros(projectRoot);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(16);
    connect(m_tickTimer, &QTimer::timeout, this, &BPRuntime::tick);
    m_tickTimer->start();
    m_elapsedTimer.start();
}

void BPRuntime::flattenMacros(const QString& projectRoot) {
    const QList<BPMacro> libMacros =
        projectRoot.isEmpty() ? QList<BPMacro>{} : BPMacro::listAll(projectRoot);

    auto removeNodeAndConns = [&](const QString& id) {
        m_nodes.removeIf([&](const BPNode& n){ return n.id == id; });
        m_connections.removeIf([&](const BPConnection& c){
            return c.fromNode == id || c.toNode == id; });
    };

    // 反复展开，直到没有 Macro:: 调用节点（支持嵌套）；上限防环
    for (int guard = 0; guard < 2000; ++guard) {
        int idx = -1;
        for (int i = 0; i < m_nodes.size(); ++i)
            if (m_nodes[i].type.startsWith("Macro::")) { idx = i; break; }
        if (idx < 0) break;

        const BPNode call = m_nodes[idx];

        // 解析宏：本地折叠取自 params；库宏按 id 查
        BPMacro macro; bool okMacro = false;
        if (call.type == "Macro::local") {
            const QString sub = call.params.value("subgraph");
            if (!sub.isEmpty()) {
                macro = BPMacro::fromJson(QJsonDocument::fromJson(sub.toUtf8()).object());
                okMacro = true;
            }
        } else {
            const QString id = call.type.mid(7);
            for (const BPMacro& m : libMacros)
                if (m.id == id) { macro = m; okMacro = true; break; }
        }
        if (!okMacro) { removeNodeAndConns(call.id); continue; }

        // 入口/出口 + 接口映射
        QString entryId, exitId;
        for (const BPNode& n : macro.nodes) {
            if (n.type == "Macro.Entry") entryId = n.id;
            else if (n.type == "Macro.Exit") exitId = n.id;
        }
        QMap<QString, QPair<QString,QString>> inMap, outMap;  // pinKey → (insideNode, insidePin)
        for (const BPConnection& c : macro.connections) {
            if (c.fromNode == entryId) inMap[c.fromPin] = {c.toNode, c.toPin};
            if (c.toNode   == exitId)  outMap[c.toPin]  = {c.fromNode, c.fromPin};
        }

        // 内部节点 id 重映射（排除入口/出口）
        QMap<QString,QString> idMap;
        for (const BPNode& n : macro.nodes)
            if (n.id != entryId && n.id != exitId)
                idMap[n.id] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto mapId = [&](const QString& id){ return idMap.value(id, id); };

        // 插入内部节点
        for (const BPNode& n : macro.nodes) {
            if (n.id == entryId || n.id == exitId) continue;
            BPNode nn = n; nn.id = idMap[n.id];
            m_nodes.append(nn);
        }
        // 插入内部连线（排除连入口/出口的）
        for (const BPConnection& c : macro.connections) {
            if (c.fromNode == entryId || c.toNode == exitId) continue;
            if (!idMap.contains(c.fromNode) || !idMap.contains(c.toNode)) continue;
            m_connections.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                                  mapId(c.fromNode), c.fromPin, mapId(c.toNode), c.toPin});
        }
        // 外部连线重定向到内部端点
        for (BPConnection& c : m_connections) {
            if (c.toNode == call.id) {
                auto it = inMap.find(c.toPin);
                if (it != inMap.end()) { c.toNode = mapId(it.value().first); c.toPin = it.value().second; }
            }
            if (c.fromNode == call.id) {
                auto it = outMap.find(c.fromPin);
                if (it != outMap.end()) { c.fromNode = mapId(it.value().first); c.fromPin = it.value().second; }
            }
        }
        // 移除 call 及其残余连线（未映射的引脚）
        removeNodeAndConns(call.id);
    }
}

void BPRuntime::tick() {
    m_lastDt = m_elapsedTimer.restart() / 1000.0f;
    const float dt = m_lastDt;
    tickComponents(dt);
    triggerTick(dt);
    emit stateChanged();
}

void BPRuntime::tickComponents(float dt) {
    // Pass 1：跟随控制组件 — 摄像机向目标插值
    for (ActorData& a : m_actors) {
        if (!a.components.contains("跟随控制组件")) continue;
        if (a.followTarget.isEmpty()) continue;
        const ActorData* target = findActorByName(a.followTarget);
        if (!target) continue;

        const float destX = target->x + a.followOffsetX;
        const float destY = target->y + a.followOffsetY;
        const float t     = 1.0f - std::exp(-a.followLerpSpeed * dt);
        a.x += (destX - a.x) * t;
        a.y += (destY - a.y) * t;
    }

    // Pass 2：边界限制组件 — clamp 摄像机位置
    for (ActorData& a : m_actors) {
        if (!a.components.contains("边界限制组件")) continue;
        if (!a.confinerEnabled) continue;

        float minX = a.confinerMinX, maxX = a.confinerMaxX;
        float minY = a.confinerMinY, maxY = a.confinerMaxY;

        // 优先从绑定的 Trigger Actor 推算边界矩形
        if (!a.confinerActor.isEmpty()) {
            const ActorData* t = findActorByName(a.confinerActor);
            if (t) {
                minX = t->x - t->scaleX * 0.5f;
                maxX = t->x + t->scaleX * 0.5f;
                minY = t->y - t->scaleY * 0.5f;
                maxY = t->y + t->scaleY * 0.5f;
            }
        }

        a.x = std::clamp(a.x, minX, maxX);
        a.y = std::clamp(a.y, minY, maxY);
    }
}

void BPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.Tick") {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void BPRuntime::triggerBeginPlay() {
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.BeginPlay") {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerKeyDown(const QString& key) {
    const QString typeId = "Event.Key." + key;
    for (const BPNode& node : m_nodes) {
        if (node.type == typeId) {
            QSet<QString> visited;
            executeChain(node.id, "pressed", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerKeyUp(const QString& key) {
    const QString typeId = "Event.Key." + key;
    for (const BPNode& node : m_nodes) {
        if (node.type == typeId) {
            QSet<QString> visited;
            executeChain(node.id, "released", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::executeChain(const QString& fromNodeId, const QString& fromPin,
                              QSet<QString>* visited) {
    // 用 "nodeId:pin" 作为访问键，防止循环蓝图导致无限递归
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);

    // exec 输出支持扇出：跟随该出口的所有连线，依次执行
    const QList<BPConnection> conns = m_connections;   // 拷贝，防执行中修改
    for (const BPConnection& c : conns) {
        if (c.fromNode == fromNodeId && c.fromPin == fromPin) {
            QString nextPin = executeNode(c.toNode);
            if (!nextPin.isEmpty())
                executeChain(c.toNode, nextPin, visited);
        }
    }
}

QString BPRuntime::executeNode(const QString& nodeId) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    if (node->type == "Action.Print") {
        m_printLog << resolveDataPin(nodeId, "text");
        return "exec_out";
    }

    if (node->type == "Var.SetNumber" || node->type == "Var.SetBool" || node->type == "Var.SetString") {
        QString name  = resolveDataPin(nodeId, "name");
        QString value = resolveDataPin(nodeId, "value");
        if (!name.isEmpty()) m_varStore[name] = value;
        return "exec_out";
    }

    if (node->type == "Action.MoveActor") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        float dx = resolveDataPin(nodeId, "dx").toFloat();
        float dy = resolveDataPin(nodeId, "dy").toFloat();
        for (ActorData& a : m_actors) {
            if (a.id == actorId) {
                a.x += dx;
                a.y += dy;
                break;
            }
        }
        return "exec_out";
    }

    if (node->type == "Action.LoadLevel") {
        const QString levelName = resolveDataPin(nodeId, "levelName");
        if (!levelName.isEmpty())
            emit loadLevelRequested(levelName);
        return {};
    }

    if (node->type == "Action.BackLevel") {
        emit backLevelRequested();
        return {};
    }

    if (node->type == "Action.SetActive") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        QString val     = resolveDataPin(nodeId, "active").toLower();
        bool active = (val == "true" || val == "1");
        for (ActorData& a : m_actors) {
            if (a.id == actorId) { a.active = active; break; }
        }
        return "exec_out";
    }

    if (node->type == "Flow.Branch") {
        QString cond = resolveDataPin(nodeId, "condition").toLower();
        bool truthy = !cond.isEmpty() && cond != "0" && cond != "false";
        return truthy ? "true" : "false";
    }

    if (node->type == "Flow.Switch") {
        // 取「值」输入，逐个分支比较；每分支比较值可连变量(caseval_<id>)或用字面量
        const QString v = resolveDataPin(nodeId, "value");
        const QJsonDocument d = QJsonDocument::fromJson(node->params.value("branches").toUtf8());
        if (d.isArray()) {
            for (const QJsonValue& bv : d.array()) {
                const QJsonObject o = bv.toObject();
                const QString id = o.value("id").toString();
                if (id.isEmpty()) continue;
                QString cmp = resolveDataPin(nodeId, "caseval_" + id);     // 连线 or params 字面量
                if (cmp.isEmpty()) cmp = o.value("value").toString();      // 兼容旧数据
                if (cmp == v) return "case_" + id;
            }
        }
        const QString hd = node->params.value("hasDefault").toLower();
        if (hd == "true" || hd == "1") return "default";
        return {};
    }

    auto uiRef = [&](const QString& pinKey) -> QString {
        QString v = resolveDataPin(nodeId, pinKey);
        if (v.isEmpty()) v = node->params.value("uiName");
        return v;
    };
    if (node->type == "UI.Create") {
        if (!m_uiRuntime) return "exec_out";
        m_uiRefs[nodeId] = m_uiRuntime->createInstance(splitWidgetRef(uiRef("widgetRef")).first);
        return "exec_out";
    }
    if (node->type == "UI.Show") {
        if (m_uiRuntime) {
            auto [uiName, widgetName] = splitWidgetRef(uiRef("widgetRef"));
            m_uiRuntime->showWidgetByName(uiName, widgetName);
        }
        return "exec_out";
    }
    if (node->type == "UI.Hide") {
        if (m_uiRuntime) {
            auto [uiName, widgetName] = splitWidgetRef(uiRef("widgetRef"));
            m_uiRuntime->hideWidgetByName(uiName, widgetName);
        }
        return "exec_out";
    }
    if (node->type == "UI.Destroy") {
        if (m_uiRuntime) {
            m_uiRuntime->destroyByName(splitWidgetRef(uiRef("widgetRef")).first);
            m_uiRefs.remove(nodeId);
        }
        return "exec_out";
    }
    if (node->type == "UI.SetText") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setTextByName(ui, widget, resolveDataPin(nodeId, "text"));
        }
        return "exec_out";
    }
    if (node->type == "UI.SetValue") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setValueByName(ui, widget, resolveDataPin(nodeId, "value").toFloat());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetPosition") {
        if (m_uiRuntime)
            m_uiRuntime->setPositionByName(splitWidgetRef(resolveDataPin(nodeId, "widgetRef")).first,
                                           resolveDataPin(nodeId, "x").toFloat(),
                                           resolveDataPin(nodeId, "y").toFloat());
        return "exec_out";
    }
    if (node->type == "UI.SetVisible") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            const QString val = resolveDataPin(nodeId, "visible").toLower();
            m_uiRuntime->setWidgetVisibleByName(ui, widget,
                                                !val.isEmpty() && val != "0" && val != "false");
        }
        return "exec_out";
    }

    return {};
}

QString BPRuntime::resolveDataPin(const QString& nodeId, const QString& pinKey) {
    for (const BPConnection& c : m_connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(nodeId);
    return node ? node->params.value(pinKey) : QString();
}

QString BPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    if (node->type == "Var.ActorRef")
        return node->params.value("actorId");

    if (node->type == "UI.Create")
        return m_uiRefs.value(nodeId);
    if (node->type == "UI.Ref") {
        const QString uiName = node->params.value("uiName");
        return uiName + "::" + pinKey;  // 所有控件引脚返回 "uiName::widgetName"
    }

    // 运行时变量读取（数值/布尔/字符串共用同一张表）
    if (node->type == "Var.GetNumber" || node->type == "Var.GetBool" || node->type == "Var.GetString") {
        QString name = resolveDataPin(nodeId, "name");
        return m_varStore.value(name);
    }

    // 数值转字符串（去掉多余的小数零）
    if (node->type == "Var.NumberToString" && pinKey == "text") {
        float v = resolveDataPin(nodeId, "number").toFloat();
        return QString::number(v, 'f', 6).remove(QRegularExpression("0+$")).remove(QRegularExpression("\\.$"));
    }

    // 数学运算
    if (node->type == "Math.Add" && pinKey == "result") {
        float a = resolveDataPin(nodeId, "a").toFloat();
        float b = resolveDataPin(nodeId, "b").toFloat();
        return QString::number(a + b);
    }
    if (node->type == "Math.Sub" && pinKey == "result") {
        float a = resolveDataPin(nodeId, "a").toFloat();
        float b = resolveDataPin(nodeId, "b").toFloat();
        return QString::number(a - b);
    }
    if (node->type == "Math.Mul" && pinKey == "result") {
        float a = resolveDataPin(nodeId, "a").toFloat();
        float b = resolveDataPin(nodeId, "b").toFloat();
        return QString::number(a * b);
    }
    if (node->type == "Math.Div" && pinKey == "result") {
        float a = resolveDataPin(nodeId, "a").toFloat();
        float b = resolveDataPin(nodeId, "b").toFloat();
        return (b != 0.0f) ? QString::number(a / b) : QString("0");
    }
    if (node->type == "Math.Clamp" && pinKey == "result") {
        float v   = resolveDataPin(nodeId, "value").toFloat();
        float mn  = resolveDataPin(nodeId, "min").toFloat();
        float mx  = resolveDataPin(nodeId, "max").toFloat();
        return QString::number(std::clamp(v, mn, mx));
    }

    // 逻辑运算
    auto isTruthy = [](const QString& s) {
        return !s.isEmpty() && s != "0" && s.toLower() != "false";
    };
    if (node->type == "Logic.Not" && pinKey == "result")
        return isTruthy(resolveDataPin(nodeId, "value")) ? "false" : "true";
    if (node->type == "Logic.And" && pinKey == "result") {
        return (isTruthy(resolveDataPin(nodeId, "a")) && isTruthy(resolveDataPin(nodeId, "b")))
               ? "true" : "false";
    }
    if (node->type == "Logic.Or" && pinKey == "result") {
        return (isTruthy(resolveDataPin(nodeId, "a")) || isTruthy(resolveDataPin(nodeId, "b")))
               ? "true" : "false";
    }
    if (node->type == "Logic.Compare" && pinKey == "result") {
        float a   = resolveDataPin(nodeId, "a").toFloat();
        float b   = resolveDataPin(nodeId, "b").toFloat();
        QString op = resolveDataPin(nodeId, "op").trimmed();
        bool res = false;
        if      (op == "==") res = (a == b);
        else if (op == "!=") res = (a != b);
        else if (op == "<")  res = (a <  b);
        else if (op == "<=") res = (a <= b);
        else if (op == ">")  res = (a >  b);
        else if (op == ">=") res = (a >= b);
        return res ? "true" : "false";
    }

    if (node->type == "Var.GetActorPos") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        for (const ActorData& a : m_actors) {
            if (a.id == actorId) {
                if (pinKey == "x") return QString::number(a.x);
                if (pinKey == "y") return QString::number(a.y);
            }
        }
    }

    if (node->type == "Event.Tick")
        return (pinKey == "delta_time") ? QString::number(m_deltaTick) : QString();

    if (node->type == "UI.OnDropdownChanged" && pinKey == "index")
        return QString::number(m_dropdownIndex.value(nodeId, 0));

    return {};
}

const BPNode* BPRuntime::findNode(const QString& id) const {
    for (const BPNode& n : m_nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const ActorData* BPRuntime::findActorByName(const QString& name) const {
    for (const ActorData& a : m_actors)
        if (a.name == name) return &a;
    return nullptr;
}

void BPRuntime::triggerButtonClick(const QString& instanceId, const QString& widgetName) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (node.type != "UI.OnButtonClick") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerDropdownChanged(const QString& instanceId,
                                        const QString& widgetName, int index) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (node.type != "UI.OnDropdownChanged") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            m_dropdownIndex[node.id] = index;
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}
