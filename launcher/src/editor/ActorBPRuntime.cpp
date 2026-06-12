#include "ActorBPRuntime.h"
#include "UIRuntime.h"

ActorBPRuntime::ActorBPRuntime(const BPClass* bpClass,
                                const QString& actorId,
                                QList<ActorData>* actors,
                                QObject* parent)
    : QObject(parent), m_bpClass(bpClass), m_actorId(actorId), m_actors(actors)
{}

void ActorBPRuntime::triggerBeginPlay() {
    triggerEvent("Event.BeginPlay");
}

void ActorBPRuntime::triggerKeyDown(const QString& key) {
    triggerEvent("Event.KeyDown", key);
}

void ActorBPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;
    triggerEvent("Event.Tick");
}

void ActorBPRuntime::triggerEvent(const QString& eventType, const QString& eventParam) {
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != eventType) continue;
        if (eventType == "Event.KeyDown") {
            const QString expected = node.params.value("key");
            if (!expected.isEmpty() && expected.compare(eventParam, Qt::CaseInsensitive) != 0)
                continue;
        }
        QSet<QString> visited;
        executeChain(node.id, "exec_out", &visited);
    }
}

void ActorBPRuntime::executeChain(const QString& fromNodeId, const QString& fromPin,
                                   QSet<QString>* visited) {
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);

    for (const BPConnection& c : m_bpClass->connections) {
        if (c.fromNode == fromNodeId && c.fromPin == fromPin) {
            QString nextPin = executeNode(c.toNode);
            if (!nextPin.isEmpty())
                executeChain(c.toNode, nextPin, visited);
            break;
        }
    }
}

QString ActorBPRuntime::executeNode(const QString& nodeId) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};
    ActorData* self = findSelf();

    // ── 通用节点类型（复用关卡蓝图逻辑）─────────────────────────────────
    if (node->type == "Action.Print") {
        return "exec_out";
    }
    if (node->type == "Flow.Branch") {
        const QString cond = resolveDataPin(nodeId, "condition").toLower();
        const bool truthy = !cond.isEmpty() && cond != "0" && cond != "false";
        return truthy ? "true" : "false";
    }

    if (!self) return {};

    // ── Self 变换节点 ─────────────────────────────────────────────────
    if (node->type == "Self.SetPosition") {
        self->x = resolveDataPin(nodeId, "x").toFloat();
        self->y = resolveDataPin(nodeId, "y").toFloat();
        return "exec_out";
    }
    if (node->type == "Self.SetRotation") {
        self->rotation = resolveDataPin(nodeId, "angle").toFloat();
        return "exec_out";
    }
    if (node->type == "Self.SetActive") {
        const QString v = resolveDataPin(nodeId, "active").toLower();
        self->active = (v == "true" || v == "1");
        return "exec_out";
    }

    // ── Self 精灵渲染器节点 ───────────────────────────────────────────
    if (node->type == "Self.Sprite.SetImage") {
        self->spritePath = resolveDataPin(nodeId, "path");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetColor") {
        self->spriteColor = QColor(
            resolveDataPin(nodeId, "r").toInt(),
            resolveDataPin(nodeId, "g").toInt(),
            resolveDataPin(nodeId, "b").toInt(),
            resolveDataPin(nodeId, "a").toInt()
        );
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetFlipX") {
        const QString v = resolveDataPin(nodeId, "flip").toLower();
        self->flipX = (v == "true" || v == "1");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetFlipY") {
        const QString v = resolveDataPin(nodeId, "flip").toLower();
        self->flipY = (v == "true" || v == "1");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetVisible") {
        const QString v = resolveDataPin(nodeId, "visible").toLower();
        self->active = (v == "true" || v == "1");
        return "exec_out";
    }

    // ── Self 摄像机节点 ───────────────────────────────────────────────
    if (node->type == "Self.Camera.SetSize") {
        self->cameraSize = resolveDataPin(nodeId, "size").toFloat();
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetBackground") {
        self->cameraBackground = QColor(
            resolveDataPin(nodeId, "r").toInt(),
            resolveDataPin(nodeId, "g").toInt(),
            resolveDataPin(nodeId, "b").toInt()
        );
        return "exec_out";
    }

    // ── UI 节点 ──────────────────────────────────────────────────────────
    if (node->type == "UI.Create") {
        if (m_uiRuntime) {
            const QString uiName = resolveDataPin(nodeId, "uiName");
            const QString instanceId = m_uiRuntime->createInstance(uiName);
            m_uiRefs[nodeId] = instanceId;
        }
        return "exec_out";
    }
    if (node->type == "UI.Show") {
        if (m_uiRuntime) m_uiRuntime->showInstance(resolveDataPin(nodeId, "instanceId"));
        return "exec_out";
    }
    if (node->type == "UI.Hide") {
        if (m_uiRuntime) m_uiRuntime->hideInstance(resolveDataPin(nodeId, "instanceId"));
        return "exec_out";
    }
    if (node->type == "UI.Destroy") {
        if (m_uiRuntime) m_uiRuntime->destroyInstance(resolveDataPin(nodeId, "instanceId"));
        return "exec_out";
    }
    if (node->type == "UI.SetText") {
        if (m_uiRuntime)
            m_uiRuntime->setText(resolveDataPin(nodeId, "instanceId"),
                                 resolveDataPin(nodeId, "widgetName"),
                                 resolveDataPin(nodeId, "text"));
        return "exec_out";
    }
    if (node->type == "UI.SetValue") {
        if (m_uiRuntime)
            m_uiRuntime->setValue(resolveDataPin(nodeId, "instanceId"),
                                  resolveDataPin(nodeId, "widgetName"),
                                  resolveDataPin(nodeId, "value").toFloat());
        return "exec_out";
    }
    if (node->type == "UI.SetPosition") {
        if (m_uiRuntime)
            m_uiRuntime->setPosition(resolveDataPin(nodeId, "instanceId"),
                                     resolveDataPin(nodeId, "x").toFloat(),
                                     resolveDataPin(nodeId, "y").toFloat());
        return "exec_out";
    }
    if (node->type == "UI.SetVisible") {
        const QString v = resolveDataPin(nodeId, "visible").toLower();
        if (m_uiRuntime)
            m_uiRuntime->setWidgetVisible(resolveDataPin(nodeId, "instanceId"),
                                          resolveDataPin(nodeId, "widgetName"),
                                          v == "true" || v == "1");
        return "exec_out";
    }

    return {};
}

QString ActorBPRuntime::resolveDataPin(const QString& nodeId, const QString& pinKey) {
    for (const BPConnection& c : m_bpClass->connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(nodeId);
    return node ? node->params.value(pinKey) : QString();
}

QString ActorBPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    ActorData* self = findSelf();

    if (node->type == "Self.GetPosition" && self) {
        if (pinKey == "x") return QString::number(self->x);
        if (pinKey == "y") return QString::number(self->y);
    }
    if (node->type == "Self.GetRotation" && self) {
        if (pinKey == "angle") return QString::number(self->rotation);
    }
    if (node->type == "Self.IsActive" && self) {
        if (pinKey == "active") return self->active ? "true" : "false";
    }
    if (node->type == "Self.GetName" && self) {
        if (pinKey == "name") return self->name;
    }
    if (node->type == "Event.Tick") {
        if (pinKey == "delta_time") return QString::number(m_deltaTick);
    }

    // UI 输出引脚
    if (node->type == "UI.Create" && pinKey == "instanceId")
        return m_uiRefs.value(nodeId);
    if (node->type == "UI.Ref" && pinKey == "instanceId")
        return node->params.value("instanceId");
    if (node->type == "UI.OnDropdownChanged" && pinKey == "index")
        return QString::number(m_dropdownIndex.value(nodeId, 0));

    return {};
}

const BPNode* ActorBPRuntime::findNode(const QString& id) const {
    for (const BPNode& n : m_bpClass->nodes)
        if (n.id == id) return &n;
    return nullptr;
}

ActorData* ActorBPRuntime::findSelf() {
    for (ActorData& a : *m_actors)
        if (a.id == m_actorId) return &a;
    return nullptr;
}

void ActorBPRuntime::triggerButtonClick(const QString& instanceId, const QString& widgetName) {
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnButtonClick") continue;
        if (resolveDataPin(node.id, "instanceId") == instanceId &&
            resolveDataPin(node.id, "widgetName") == widgetName)
            executeChain(node.id, "exec_out");
    }
}

void ActorBPRuntime::triggerDropdownChanged(const QString& instanceId,
                                             const QString& widgetName, int index) {
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDropdownChanged") continue;
        if (resolveDataPin(node.id, "instanceId") == instanceId &&
            resolveDataPin(node.id, "widgetName") == widgetName) {
            m_dropdownIndex[node.id] = index;
            executeChain(node.id, "exec_out");
        }
    }
}
