#include "ActorBPRuntime.h"
#include "UIRuntime.h"
#include "BPEval.h"
#include <utility>

static std::pair<QString,QString> splitWidgetRef(const QString& ref) {
    int sep = ref.indexOf("::");
    if (sep < 0) return {ref, {}};
    return {ref.left(sep), ref.mid(sep + 2)};
}

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
    m_heldKeys.insert(key);
    triggerEvent("Event.Key." + key, "pressed");
}

void ActorBPRuntime::triggerKeyUp(const QString& key) {
    m_heldKeys.remove(key);
    triggerEvent("Event.Key." + key, "released");
}

void ActorBPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;
    triggerEvent("Event.Tick");
    // 持续按住：对每个按住的键，每帧驱动其 held 链（与关卡蓝图对齐）
    for (const QString& key : m_heldKeys)
        triggerEvent("Event.Key." + key, "held");
}

void ActorBPRuntime::triggerCollision(const QString& selfId, const QString& otherId,
                                      const QString& otherTag) {
    if (selfId != m_actorId) return;   // 只响应自己的碰撞
    m_collOther = otherId; m_collTag = otherTag;
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "Event.OnCollision") continue;
        QSet<QString> v1; executeChain(node.id, "case_" + otherTag, &v1);  // 按对方标签分路
        QSet<QString> v2; executeChain(node.id, "exec_out",        &v2);  // 通用出口
    }
}

void ActorBPRuntime::triggerEvent(const QString& eventType, const QString& pinName) {
    const QString pin = pinName.isEmpty() ? "exec_out" : pinName;
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != eventType) continue;
        QSet<QString> visited;
        executeChain(node.id, pin, &visited);
    }
}

void ActorBPRuntime::executeChain(const QString& fromNodeId, const QString& fromPin,
                                   QSet<QString>* visited) {
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);

    // exec 输出支持扇出：跟随该出口的所有连线，依次执行（与关卡蓝图对齐）
    const QList<BPConnection> conns = m_bpClass->connections;   // 拷贝，防执行中修改
    for (const BPConnection& c : conns) {
        if (c.fromNode == fromNodeId && c.fromPin == fromPin) {
            QString nextPin = executeNode(c.toNode);
            if (!nextPin.isEmpty())
                executeChain(c.toNode, nextPin, visited);
        }
    }
}

QString ActorBPRuntime::executeNode(const QString& nodeId) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};
    ActorData* self = findSelf();

    // 跳转关卡 / 返回上一关：转交 EditorWindow（与关卡蓝图同一套换关处理）
    if (node->type == "Action.LoadLevel") {
        const QString levelName = resolveDataPin(nodeId, "levelName");
        if (!levelName.isEmpty()) emit loadLevelRequested(levelName);
        return {};
    }
    if (node->type == "Action.BackLevel") {
        emit backLevelRequested();
        return {};
    }

    // ── 通用节点类型（复用关卡蓝图逻辑）─────────────────────────────────
    if (node->type == "Action.Print") {
        emit printOutput(resolveDataPin(nodeId, "text"));
        return "exec_out";
    }
    if (node->type == "Flow.Branch") {
        const QString cond = resolveDataPin(nodeId, "condition").toString().toLower();
        const bool truthy = !cond.isEmpty() && cond != "0" && cond != "false";
        return truthy ? "true" : "false";
    }

    if (!self) return {};

    // ── Self 变换节点 ─────────────────────────────────────────────────
    if (node->type == "Self.SetPosition") {
        self->x = resolveDataPin(nodeId, "x").toString().toFloat();
        self->y = resolveDataPin(nodeId, "y").toString().toFloat();
        return "exec_out";
    }
    if (node->type == "Self.SetRotation") {
        self->rotation = resolveDataPin(nodeId, "angle").toString().toFloat();
        return "exec_out";
    }
    if (node->type == "Self.SetActive") {
        const QString v = resolveDataPin(nodeId, "active").toString().toLower();
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
            resolveDataPin(nodeId, "r").toString().toInt(),
            resolveDataPin(nodeId, "g").toString().toInt(),
            resolveDataPin(nodeId, "b").toString().toInt(),
            resolveDataPin(nodeId, "a").toString().toInt()
        );
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetFlipX") {
        const QString v = resolveDataPin(nodeId, "flip").toString().toLower();
        self->flipX = (v == "true" || v == "1");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetFlipY") {
        const QString v = resolveDataPin(nodeId, "flip").toString().toLower();
        self->flipY = (v == "true" || v == "1");
        return "exec_out";
    }
    // ── Self 动画器节点 ───────────────────────────────────────────────
    if (node->type == "Self.Anim.Play") {
        const QString clip = resolveDataPin(nodeId, "clip").toString();
        if (!clip.isEmpty()) {
            if (self->animCurClip != clip) {   // 切换片段才帧归零，重复调用不打断
                self->animCurClip    = clip;
                self->animFrameIndex = 0;
                self->animTimeAccum  = 0.0;
            }
            self->animPlaying = true;
        }
        return "exec_out";
    }
    if (node->type == "Self.Anim.Stop") {
        self->animPlaying = false;     // 停在当前帧
        return "exec_out";
    }

    if (node->type == "Self.Sprite.SetVisible") {
        const QString v = resolveDataPin(nodeId, "visible").toString().toLower();
        self->spriteVisible = (v == "true" || v == "1");
        return "exec_out";
    }

    // ── Self 摄像机节点 ───────────────────────────────────────────────
    if (node->type == "Self.Camera.SetSize") {
        self->cameraSize = resolveDataPin(nodeId, "size").toString().toFloat();
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetBackground") {
        self->cameraBackground = QColor(
            resolveDataPin(nodeId, "r").toString().toInt(),
            resolveDataPin(nodeId, "g").toString().toInt(),
            resolveDataPin(nodeId, "b").toString().toInt()
        );
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetFollow") {
        self->followTarget = resolveDataPin(nodeId, "target");
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetFollowOffset") {
        self->followOffsetX = resolveDataPin(nodeId, "x").toString().toFloat();
        self->followOffsetY = resolveDataPin(nodeId, "y").toString().toFloat();
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetSmooth") {
        self->followLerpSpeed = resolveDataPin(nodeId, "speed").toString().toFloat();
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetBoundary") {
        self->confinerEnabled = true;
        self->confinerMinX = resolveDataPin(nodeId, "minX").toString().toFloat();
        self->confinerMaxX = resolveDataPin(nodeId, "maxX").toString().toFloat();
        self->confinerMinY = resolveDataPin(nodeId, "minY").toString().toFloat();
        self->confinerMaxY = resolveDataPin(nodeId, "maxY").toString().toFloat();
        return "exec_out";
    }
    if (node->type == "Self.Camera.ClearFollow") {
        self->followTarget.clear();
        return "exec_out";
    }
    if (node->type == "Self.Camera.ClearBoundary") {
        self->confinerEnabled = false;
        return "exec_out";
    }

    // ── UI 节点 ──────────────────────────────────────────────────────────
    auto uiRef = [&](const QString& pinKey) -> QString {
        QString v = resolveDataPin(nodeId, pinKey);
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
            m_uiRuntime->setValueByName(ui, widget, resolveDataPin(nodeId, "value").toString().toFloat());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetPosition") {
        if (m_uiRuntime)
            m_uiRuntime->setPositionByName(splitWidgetRef(resolveDataPin(nodeId, "widgetRef")).first,
                                           resolveDataPin(nodeId, "x").toString().toFloat(),
                                           resolveDataPin(nodeId, "y").toString().toFloat());
        return "exec_out";
    }
    if (node->type == "UI.SetVisible") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            const QString v = resolveDataPin(nodeId, "visible").toString().toLower();
            m_uiRuntime->setWidgetVisibleByName(ui, widget, v == "true" || v == "1");
        }
        return "exec_out";
    }

    return {};
}

BPValue ActorBPRuntime::resolveDataPin(const QString& nodeId, const QString& pinKey) {
    for (const BPConnection& c : m_bpClass->connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(nodeId);
    return node ? node->params.value(pinKey) : QString();
}

BPValue ActorBPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
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
    if (node->type == "Event.OnCollision") {
        if (pinKey == "self")  return m_actorId;
        if (pinKey == "other") return m_collOther;
        if (pinKey == "tag")   return m_collTag;
    }

    // UI 输出引脚
    if (node->type == "UI.Create" && pinKey == "uiRef")
        return m_uiRefs.value(nodeId);
    if (node->type == "UI.Ref") {
        const QString uiName = node->params.value("uiName");
        return uiName + "::" + pinKey;  // 所有控件引脚返回 "uiName::widgetName"
    }
    if (node->type == "UI.OnDropdownChanged" && pinKey == "index")
        return QString::number(m_dropdownIndex.value(nodeId, 0));

    // ── 纯数据节点（数学/比较/逻辑/数组/转换）：共享求值器，两套运行时一致 ──
    BPValue out;
    if (evalPureDataNode(node->type, pinKey,
            [&](const QString& pk){ return resolveDataPin(nodeId, pk); }, out))
        return out;

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
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnButtonClick") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}

void ActorBPRuntime::triggerDropdownChanged(const QString& instanceId,
                                             const QString& widgetName, int index) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != "UI.OnDropdownChanged") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            m_dropdownIndex[node.id] = index;
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}
