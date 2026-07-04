#include "ComponentBPRuntime.h"

#include "BPRuntime.h"
#include "UIRuntime.h"

#include <QColor>
#include <QJsonValue>
#include <utility>

namespace {
std::pair<QString, QString> splitWidgetRef(const QString& ref) {
    const int sep = ref.indexOf("::");
    if (sep < 0) return {ref, {}};
    return {ref.left(sep), ref.mid(sep + 2)};
}

BPValue bpValueFromVariant(const QVariant& v) {
    if (v.typeId() == QMetaType::Bool) return BPValue::fromBool(v.toBool());
    if (v.canConvert<double>() && v.typeId() != QMetaType::QString)
        return BPValue::fromNumber(v.toDouble());
    return BPValue::fromJsonValue(QJsonValue::fromVariant(v));
}
}

ComponentBPRuntime::ComponentBPRuntime(const BPClass* bpClass,
                                       const QString& ownerActorId,
                                       const ComponentInstance& instance,
                                       QList<ActorData>* actors,
                                       BPRuntime* runtime,
                                       QObject* parent)
    : QObject(parent),
      m_bpClass(bpClass),
      m_ownerActorId(ownerActorId),
      m_instance(instance),
      m_actors(actors),
      m_runtime(runtime) {
    m_context.setRuntimeKind(BlueprintRuntimeKind::Component);
    m_context.setBlueprint(m_bpClass);
    m_context.setOwnerActor(m_ownerActorId);
    m_context.setComponentInstance(m_instance.id);
    if (m_bpClass) {
        for (auto it = m_bpClass->defaults.constBegin(); it != m_bpClass->defaults.constEnd(); ++it)
            m_varStore[it.key()] = bpValueFromVariant(it.value());
    }
    for (auto it = m_instance.defaultOverrides.constBegin(); it != m_instance.defaultOverrides.constEnd(); ++it)
        m_varStore[it.key()] = BPValue::fromJsonValue(it.value());
    m_localScope.setStore(&m_varStore);
}

void ComponentBPRuntime::triggerBeginPlay() { if (enabled()) triggerEvent("Event.BeginPlay"); }
void ComponentBPRuntime::triggerKeyDown(const QString& key) {
    if (!enabled()) return;
    m_heldKeys.insert(key);
    triggerEvent("Event.Key." + key, "pressed");
}
void ComponentBPRuntime::triggerKeyUp(const QString& key) {
    if (!enabled()) return;
    m_heldKeys.remove(key);
    triggerEvent("Event.Key." + key, "released");
}
void ComponentBPRuntime::triggerTick(float dt) {
    if (!enabled()) return;
    m_deltaTick = dt;
    triggerEvent("Event.Tick");
    for (const QString& key : m_heldKeys)
        triggerEvent("Event.Key." + key, "held");
}

void ComponentBPRuntime::triggerCollision(const QString& selfId, const QString& otherId, const QString& otherTag) {
    if (!enabled() || selfId != m_ownerActorId || !m_bpClass) return;
    m_context.setEventName("Event.OnCollision");
    m_collOther = otherId;
    m_collTag = otherTag;
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "Event.OnCollision") continue;
        QSet<QString> v1; executeChain(node.id, "case_" + otherTag, &v1);
        QSet<QString> v2; executeChain(node.id, "exec_out", &v2);
    }
}

void ComponentBPRuntime::triggerEvent(const QString& eventType, const QString& pinName) {
    if (!m_bpClass) return;
    m_context.setEventName(eventType);
    const QString pin = pinName.isEmpty() ? "exec_out" : pinName;
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != eventType) continue;
        QSet<QString> visited;
        executeChain(node.id, pin, &visited);
    }
}

void ComponentBPRuntime::executeChain(const QString& fromNodeId, const QString& fromPin, QSet<QString>* visited) {
    if (!m_bpClass) return;
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);
    const QList<BPConnection> conns = m_bpClass->connections;
    for (const BPConnection& c : conns) {
        if (c.fromNode != fromNodeId || c.fromPin != fromPin) continue;
        const QString nextPin = executeNode(c.toNode);
        if (!nextPin.isEmpty()) executeChain(c.toNode, nextPin, visited);
    }
}

QString ComponentBPRuntime::executeNode(const QString& nodeId) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};
    m_context.enterNode(*node);

    BlueprintNodeExecutor::ExecState sharedState;
    sharedState.context = &m_context;
    sharedState.localScope = &m_localScope;
    sharedState.globalScope = m_runtime ? m_runtime->globalScope() : nullptr;
    sharedState.loopState = &m_loopState;
    sharedState.print = [this](const QString& text) { emit printOutput(text); };
    sharedState.diagnostic = [this](const QString& text) { emit printOutput(text); };
    sharedState.loadLevel = [this](const QString& levelName) { emit loadLevelRequested(levelName); };
    sharedState.backLevel = [this]() { emit backLevelRequested(); };
    sharedState.runFromPin = [this](const QString& fromNodeId, const QString& fromPin) {
        QSet<QString> bodyVisited;
        executeChain(fromNodeId, fromPin, &bodyVisited);
    };
    QString sharedNextPin;
    if (BlueprintNodeExecutor::executeSharedNode(
            *node,
            [this, nodeId](const QString& pinKey) { return resolveDataPin(nodeId, pinKey); },
            sharedState,
            &sharedNextPin)) {
        return sharedNextPin;
    }

    ActorData* owner = ownerActor();
    if (!owner) return {};

    if (node->type == "Self.SetPosition") {
        owner->x = resolveDataPin(nodeId, "x").toNumber();
        owner->y = resolveDataPin(nodeId, "y").toNumber();
        return "exec_out";
    }
    if (node->type == "Self.SetRotation") {
        owner->rotation = resolveDataPin(nodeId, "angle").toNumber();
        return "exec_out";
    }
    if (node->type == "Self.SetActive") {
        owner->active = resolveDataPin(nodeId, "active").toBool();
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetImage") {
        owner->spritePath = resolveDataPin(nodeId, "path").toString();
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetColor") {
        owner->spriteColor = QColor((int)resolveDataPin(nodeId, "r").toNumber(),
                                    (int)resolveDataPin(nodeId, "g").toNumber(),
                                    (int)resolveDataPin(nodeId, "b").toNumber(),
                                    (int)resolveDataPin(nodeId, "a").toNumber());
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetVisible") {
        owner->spriteVisible = resolveDataPin(nodeId, "visible").toBool();
        return "exec_out";
    }

    auto uiRef = [&](const QString& pinKey) -> QString {
        QString v = resolveDataPin(nodeId, pinKey).toString();
        if (v.isEmpty()) v = node->params.value("uiName");
        return v;
    };
    if (node->type == "UI.Create") {
        if (m_uiRuntime)
            m_uiRefs[nodeId] = m_uiRuntime->createInstance(splitWidgetRef(uiRef("widgetRef")).first);
        return "exec_out";
    }
    if (node->type == "UI.Show") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(uiRef("widgetRef"));
            m_uiRuntime->showWidgetByName(ui, widget);
        }
        return "exec_out";
    }
    if (node->type == "UI.Hide") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(uiRef("widgetRef"));
            m_uiRuntime->hideWidgetByName(ui, widget);
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
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef").toString());
            m_uiRuntime->setTextByName(ui, widget, resolveDataPin(nodeId, "text").toString());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetValue") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef").toString());
            m_uiRuntime->setValueByName(ui, widget, resolveDataPin(nodeId, "value").toNumber());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetVisible") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef").toString());
            m_uiRuntime->setWidgetVisibleByName(ui, widget, resolveDataPin(nodeId, "visible").toBool());
        }
        return "exec_out";
    }

    const QString unknownKey = node->id + ":" + node->type;
    if (!m_reportedUnknownNodes.contains(unknownKey)) {
        m_reportedUnknownNodes.insert(unknownKey);
        emit printOutput(m_context.formatDiagnostic(QString("未处理的执行节点：%1").arg(node->type)));
    }
    return {};
}

BPValue ComponentBPRuntime::resolveDataPin(const QString& nodeId, const QString& pinKey) {
    if (!m_bpClass) return {};
    for (const BPConnection& c : m_bpClass->connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(nodeId);
    if (!node) return BPValue();
    return BPValue::fromString(node->params.value(pinKey));
}

BPValue ComponentBPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};
    ActorData* owner = ownerActor();

    if (node->type == "Self.GetPosition" && owner) {
        if (pinKey == "x") return BPValue::fromNumber(owner->x);
        if (pinKey == "y") return BPValue::fromNumber(owner->y);
    }
    if (node->type == "Self.GetRotation" && owner && pinKey == "angle")
        return BPValue::fromNumber(owner->rotation);
    if (node->type == "Self.IsActive" && owner && pinKey == "active")
        return BPValue::fromBool(owner->active);
    if (node->type == "Self.GetName" && owner && pinKey == "name")
        return owner->name;
    if (node->type == "Event.Tick" && pinKey == "delta_time")
        return BPValue::fromNumber(m_deltaTick);
    if (node->type == "Event.OnCollision") {
        if (pinKey == "self") return m_ownerActorId;
        if (pinKey == "other") return m_collOther;
        if (pinKey == "tag") return m_collTag;
    }
    if (node->type == "UI.Create" && pinKey == "uiRef")
        return m_uiRefs.value(nodeId);
    if (node->type == "UI.Ref")
        return node->params.value("uiName") + "::" + pinKey;
    if (node->type == "UI.OnDropdownChanged" && pinKey == "index")
        return BPValue::fromNumber(m_dropdownIndex.value(nodeId, 0));
    if (node->type == "UI.OnDragStart" || node->type == "UI.OnDragMove"
     || node->type == "UI.OnDrop" || node->type == "UI.OnDragCancel") {
        const UIDragState s = m_uiDragState.value(nodeId);
        if (pinKey == "x") return BPValue::fromNumber(s.x);
        if (pinKey == "y") return BPValue::fromNumber(s.y);
        if (pinKey == "widgetName") return s.widgetName;
    }

    BlueprintNodeExecutor::ExecState sharedState;
    sharedState.context = &m_context;
    sharedState.localScope = &m_localScope;
    sharedState.globalScope = m_runtime ? m_runtime->globalScope() : nullptr;
    sharedState.loopState = &m_loopState;
    BPValue sharedOut;
    if (BlueprintNodeExecutor::resolveSharedOutput(
            *node,
            pinKey,
            [this, nodeId](const QString& pk) { return resolveDataPin(nodeId, pk); },
            sharedState,
            &sharedOut)) {
        return sharedOut;
    }
    return {};
}

const BPNode* ComponentBPRuntime::findNode(const QString& id) const {
    if (!m_bpClass) return nullptr;
    for (const BPNode& n : m_bpClass->nodes)
        if (n.id == id) return &n;
    return nullptr;
}

ActorData* ComponentBPRuntime::ownerActor() {
    if (!m_actors) return nullptr;
    for (ActorData& a : *m_actors)
        if (a.id == m_ownerActorId) return &a;
    return nullptr;
}

QString ComponentBPRuntime::uiNameForInstance(const QString& instanceId) const {
    if (!m_uiRuntime) return {};
    for (const UIInstance* inst : m_uiRuntime->shownInstances())
        if (inst->instanceId == instanceId) return inst->uiName;
    return {};
}

void ComponentBPRuntime::triggerButtonClick(const QString& instanceId, const QString& widgetName) {
    if (!enabled() || !m_bpClass) return;
    m_context.setEventName("UI.OnButtonClick");
    const QString uiName = uiNameForInstance(instanceId);
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnButtonClick") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef").toString());
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void ComponentBPRuntime::triggerDropdownChanged(const QString& instanceId, const QString& widgetName, int index) {
    if (!enabled() || !m_bpClass) return;
    m_context.setEventName("UI.OnDropdownChanged");
    const QString uiName = uiNameForInstance(instanceId);
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDropdownChanged") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef").toString());
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            m_dropdownIndex[node.id] = index;
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void ComponentBPRuntime::triggerUIDragStarted(const QString& instanceId, const QString& widgetName, float x, float y) {
    if (!enabled() || !m_bpClass) return;
    m_context.setEventName("UI.OnDragStart");
    const QString uiName = uiNameForInstance(instanceId);
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDragStart") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef").toString());
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void ComponentBPRuntime::triggerUIDragMoved(const QString& instanceId, const QString& widgetName, float x, float y) {
    if (!enabled() || !m_bpClass) return;
    m_context.setEventName("UI.OnDragMove");
    const QString uiName = uiNameForInstance(instanceId);
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDragMove") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef").toString());
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void ComponentBPRuntime::triggerUIDropped(const QString& instanceId, const QString& widgetName, float x, float y) {
    if (!enabled() || !m_bpClass) return;
    m_context.setEventName("UI.OnDrop");
    const QString uiName = uiNameForInstance(instanceId);
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDrop") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef").toString());
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void ComponentBPRuntime::triggerUIDragCanceled(const QString& instanceId, const QString& widgetName, float x, float y) {
    if (!enabled() || !m_bpClass) return;
    m_context.setEventName("UI.OnDragCancel");
    const QString uiName = uiNameForInstance(instanceId);
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDragCancel") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef").toString());
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}
