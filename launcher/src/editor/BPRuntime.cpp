#include "BPRuntime.h"
#include "UIRuntime.h"
#include <QSet>
#include <algorithm>
#include <cmath>

BPRuntime::BPRuntime(const LevelDocument* doc, QObject* parent)
    : QObject(parent)
{
    if (!doc) return;
    m_nodes       = doc->bpNodes();
    m_connections = doc->bpConnections();
    m_actors      = doc->actors();
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(16);
    connect(m_tickTimer, &QTimer::timeout, this, &BPRuntime::tick);
    m_tickTimer->start();
    m_elapsedTimer.start();
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

        a.x = std::clamp(a.x, a.confinerMinX, a.confinerMaxX);
        a.y = std::clamp(a.y, a.confinerMinY, a.confinerMaxY);
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
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.KeyDown") {
            QString expected = node.params.value("key");
            if (expected.isEmpty() || expected.compare(key, Qt::CaseInsensitive) == 0) {
                QSet<QString> visited;
                executeChain(node.id, "exec_out", &visited);
            }
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

    for (const BPConnection& c : m_connections) {
        if (c.fromNode == fromNodeId && c.fromPin == fromPin) {
            QString nextPin = executeNode(c.toNode);
            if (!nextPin.isEmpty())
                executeChain(c.toNode, nextPin, visited);
            break;
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

    if (node->type == "UI.Create") {
        if (!m_uiRuntime) return "exec_out";
        const QString uiName = resolveDataPin(nodeId, "uiName");
        const QString instId = m_uiRuntime->createInstance(uiName);
        m_uiRefs[nodeId] = instId;
        return "exec_out";
    }
    if (node->type == "UI.Show") {
        if (m_uiRuntime) {
            const QString id = resolveDataPin(nodeId, "instanceId");
            m_uiRuntime->showInstance(id);
        }
        return "exec_out";
    }
    if (node->type == "UI.Hide") {
        if (m_uiRuntime) {
            const QString id = resolveDataPin(nodeId, "instanceId");
            m_uiRuntime->hideInstance(id);
        }
        return "exec_out";
    }
    if (node->type == "UI.Destroy") {
        if (m_uiRuntime) {
            const QString id = resolveDataPin(nodeId, "instanceId");
            m_uiRuntime->destroyInstance(id);
            m_uiRefs.remove(nodeId);
        }
        return "exec_out";
    }
    if (node->type == "UI.SetText") {
        if (m_uiRuntime) {
            const QString id   = resolveDataPin(nodeId, "instanceId");
            const QString name = resolveDataPin(nodeId, "widgetName");
            const QString text = resolveDataPin(nodeId, "text");
            m_uiRuntime->setText(id, name, text);
        }
        return "exec_out";
    }
    if (node->type == "UI.SetValue") {
        if (m_uiRuntime) {
            const QString id   = resolveDataPin(nodeId, "instanceId");
            const QString name = resolveDataPin(nodeId, "widgetName");
            float val = resolveDataPin(nodeId, "value").toFloat();
            m_uiRuntime->setValue(id, name, val);
        }
        return "exec_out";
    }
    if (node->type == "UI.SetPosition") {
        if (m_uiRuntime) {
            const QString id = resolveDataPin(nodeId, "instanceId");
            float x = resolveDataPin(nodeId, "x").toFloat();
            float y = resolveDataPin(nodeId, "y").toFloat();
            m_uiRuntime->setPosition(id, x, y);
        }
        return "exec_out";
    }
    if (node->type == "UI.SetVisible") {
        if (m_uiRuntime) {
            const QString id   = resolveDataPin(nodeId, "instanceId");
            const QString name = resolveDataPin(nodeId, "widgetName");
            const QString val  = resolveDataPin(nodeId, "visible").toLower();
            bool vis = !val.isEmpty() && val != "0" && val != "false";
            m_uiRuntime->setWidgetVisible(id, name, vis);
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
    if (node->type == "UI.Ref")
        return node->params.value("instanceId");

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
