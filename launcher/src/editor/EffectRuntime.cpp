#include "EffectRuntime.h"

#include "BPEval.h"
#include "BlueprintNodeExecutor.h"
#include "models/BPClass.h"
#include "models/AssetRegistry.h"

#include <QFileInfo>

EffectRuntime::EffectRuntime(QString projectRoot, BattleRuntime* battle)
    : m_projectRoot(std::move(projectRoot)), m_battle(battle) {}

QString EffectRuntime::resolveAssetPath(const QString& assetRef) const {
    AssetRegistry registry(m_projectRoot);
    registry.load();
    return AssetResolver(m_projectRoot, &registry)
        .resolve(SoftAssetRef::fromString(assetRef, "bp.effect"), {}, {});
}

CardEffectResult EffectRuntime::executeBlueprint(const QString& effectRef,
                                                 const EffectContext& context,
                                                 const QString& fallbackName) {
    if (!m_battle) return {false, "战斗未开始"};
    const QString path = resolveAssetPath(effectRef);
    if (!QFileInfo::exists(path)) {
        const QString message = QString("效果蓝图不存在：%1").arg(path);
        report(message);
        return {false, message};
    }
    BPClass effect = BPClass::load(path);
    if (effect.blueprintType != "Effect") {
        const QString message = QString("效果蓝图类型无效：%1").arg(path);
        report(message);
        return {false, "效果蓝图无效"};
    }

    m_context = context;
    if (m_context.target.trimmed().isEmpty()) m_context.target = "enemy";
    if (m_context.source.trimmed().isEmpty()) m_context.source = "player";
    m_execContext.setRuntimeKind(BlueprintRuntimeKind::Effect);
    m_execContext.setBlueprint(&effect);
    m_execContext.setOwnerActor(m_context.source);
    m_execContext.setComponentInstance({});
    m_execContext.setEventName("Event.EffectExecute");
    m_reportedUnknownNodes.clear();
    m_message.clear();
    m_effectVars.clear();
    m_effectVars["source"] = BPValue::fromString(m_context.source);
    m_effectVars["target"] = BPValue::fromString(m_context.target);
    m_effectVars["cardId"] = BPValue::fromString(m_context.cardId);
    m_effectVars["recordId"] = BPValue::fromString(m_context.recordId);
    m_effectVars["value"] = BPValue::fromNumber(m_context.value);
    m_effectScope.setStore(&m_effectVars);

    bool executed = false;
    for (const BPNode& node : effect.nodes) {
        if (node.type != "Event.EffectExecute") continue;
        QSet<QString> visited;
        executeChain(effect, node.id, "exec_out", &visited);
        executed = true;
    }
    if (!executed) {
        const QString message = m_execContext.formatDiagnostic("缺少执行入口 Event.EffectExecute");
        report(message);
        return {false, "效果蓝图缺少执行入口"};
    }
    if (m_message.isEmpty())
        m_message = QString("%1效果已执行").arg(fallbackName.trimmed().isEmpty() ? "卡牌" : fallbackName);
    return {true, m_message};
}

void EffectRuntime::executeChain(const BPClass& effect, const QString& fromNodeId,
                                 const QString& fromPin, QSet<QString>* visited) {
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);

    for (const BPConnection& c : effect.connections) {
        if (c.fromNode != fromNodeId || c.fromPin != fromPin) continue;
        const QString nextPin = executeNode(effect, c.toNode);
        if (!nextPin.isEmpty()) executeChain(effect, c.toNode, nextPin, visited);
    }
}

QString EffectRuntime::executeNode(const BPClass& effect, const QString& nodeId) {
    const BPNode* node = findNode(effect, nodeId);
    if (!node) return {};
    m_execContext.enterNode(*node);

    BlueprintNodeExecutor::ExecState sharedState;
    sharedState.context = &m_execContext;
    sharedState.localScope = &m_effectScope;
    sharedState.print = [this](const QString& text) {
        if (m_printCallback) m_printCallback(text);
    };
    QString sharedNextPin;
    if (BlueprintNodeExecutor::executeSharedNode(
            *node,
            [this, &effect, nodeId](const QString& pinKey) { return resolveDataPin(effect, nodeId, pinKey); },
            sharedState,
            &sharedNextPin)) {
        return sharedNextPin;
    }

    if (node->type == "Action.Print") {
        if (m_printCallback) m_printCallback(resolveDataPin(effect, nodeId, "text").toString());
        return "exec_out";
    }
    if (node->type == "Battle.DamageUnit") {
        appendMessage(m_battle->damageUnit(resolveDataPin(effect, nodeId, "unit").toString(),
                                           (int)resolveDataPin(effect, nodeId, "value").toNumber()));
        return "exec_out";
    }
    if (node->type == "Battle.HealUnit") {
        appendMessage(m_battle->healUnit(resolveDataPin(effect, nodeId, "unit").toString(),
                                         (int)resolveDataPin(effect, nodeId, "value").toNumber()));
        return "exec_out";
    }
    if (node->type == "Battle.AddShield") {
        appendMessage(m_battle->addShield(resolveDataPin(effect, nodeId, "unit").toString(),
                                          (int)resolveDataPin(effect, nodeId, "value").toNumber()));
        return "exec_out";
    }
    if (node->type == "Battle.AddEnergy") {
        appendMessage(m_battle->addEnergy(resolveDataPin(effect, nodeId, "unit").toString(),
                                          (int)resolveDataPin(effect, nodeId, "value").toNumber()));
        return "exec_out";
    }
    if (node->type == "Battle.AddTag") {
        appendMessage(m_battle->addStatus(resolveDataPin(effect, nodeId, "unit").toString(),
                                          resolveDataPin(effect, nodeId, "tag").toString(),
                                          (int)resolveDataPin(effect, nodeId, "stacks").toNumber(),
                                          (int)resolveDataPin(effect, nodeId, "turns").toNumber()));
        return "exec_out";
    }
    if (node->type == "Battle.RemoveTag") {
        appendMessage(m_battle->removeTag(resolveDataPin(effect, nodeId, "unit").toString(),
                                          resolveDataPin(effect, nodeId, "tag").toString()));
        return "exec_out";
    }
    if (node->type == "Battle.SetTagStacks") {
        appendMessage(m_battle->setStatusStacks(resolveDataPin(effect, nodeId, "unit").toString(),
                                                resolveDataPin(effect, nodeId, "tag").toString(),
                                                (int)resolveDataPin(effect, nodeId, "stacks").toNumber()));
        return "exec_out";
    }
    const QString unknownKey = node->id + ":" + node->type;
    if (!m_reportedUnknownNodes.contains(unknownKey)) {
        m_reportedUnknownNodes.insert(unknownKey);
        report(m_execContext.formatDiagnostic(QString("未处理的执行节点：%1").arg(node->type)));
    }
    return {};
}

BPValue EffectRuntime::resolveDataPin(const BPClass& effect, const QString& nodeId, const QString& pinKey) {
    for (const BPConnection& c : effect.connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(effect, c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(effect, nodeId);
    if (!node) return {};
    if (pinKey == "unit" && node->params.value(pinKey).isEmpty())
        return m_context.target;
    if (pinKey == "value" && node->params.value(pinKey).isEmpty())
        return BPValue::fromNumber(m_context.value);
    return node->params.value(pinKey);
}

BPValue EffectRuntime::resolveOutputPin(const BPClass& effect, const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(effect, nodeId);
    if (!node) return {};
    if (node->type == "Event.EffectExecute") {
        if (pinKey == "source") return m_context.source;
        if (pinKey == "target") return m_context.target;
        if (pinKey == "value") return BPValue::fromNumber(m_context.value);
        if (pinKey == "recordId") return m_context.recordId;
    }
    if (node->type == "Battle.HasTag" && pinKey == "has") {
        return BPValue::fromBool(m_battle && m_battle->hasTag(resolveDataPin(effect, nodeId, "unit").toString(),
                                                             resolveDataPin(effect, nodeId, "tag").toString()));
    }
    BlueprintNodeExecutor::ExecState sharedState;
    sharedState.context = &m_execContext;
    sharedState.localScope = &m_effectScope;
    BPValue sharedOut;
    if (BlueprintNodeExecutor::resolveSharedOutput(
            *node,
            pinKey,
            [this, &effect, nodeId](const QString& pk) { return resolveDataPin(effect, nodeId, pk); },
            sharedState,
            &sharedOut)) {
        return sharedOut;
    }
    BPValue out;
    if (evalPureDataNode(node->type, pinKey,
            [&](const QString& pk){ return resolveDataPin(effect, nodeId, pk); }, out))
        return out;
    return {};
}

const BPNode* EffectRuntime::findNode(const BPClass& effect, const QString& id) const {
    for (const BPNode& n : effect.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

void EffectRuntime::appendMessage(const CardEffectResult& result) {
    if (result.message.isEmpty()) return;
    if (!m_message.isEmpty()) m_message += "；";
    m_message += result.message;
}

void EffectRuntime::report(const QString& message) const {
    if (m_diagnosticCallback && !message.isEmpty())
        m_diagnosticCallback(message);
}
