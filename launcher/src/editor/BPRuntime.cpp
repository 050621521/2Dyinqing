#include "BPRuntime.h"
#include <QSet>

BPRuntime::BPRuntime(const LevelDocument* doc, QObject* parent)
    : QObject(parent)
{
    if (!doc) return;
    m_nodes       = doc->bpNodes();
    m_connections = doc->bpConnections();
    m_actors      = doc->actors();
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

    if (node->type == "Var.GetActorPos") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        for (const ActorData& a : m_actors) {
            if (a.id == actorId) {
                if (pinKey == "x") return QString::number(a.x);
                if (pinKey == "y") return QString::number(a.y);
            }
        }
    }

    return {};
}

const BPNode* BPRuntime::findNode(const QString& id) const {
    for (const BPNode& n : m_nodes)
        if (n.id == id) return &n;
    return nullptr;
}
