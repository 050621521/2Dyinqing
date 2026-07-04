#include "BlueprintExecutionContext.h"

#include "models/BPClass.h"

#include <QFileInfo>
#include <QStringList>

void BlueprintExecutionContext::setBlueprint(const BPClass* bpClass) {
    if (!bpClass) {
        m_blueprintName.clear();
        m_blueprintPath.clear();
        return;
    }
    setBlueprintInfo(bpClass->name, bpClass->filePath);
}

void BlueprintExecutionContext::setBlueprintInfo(const QString& name, const QString& path) {
    m_blueprintPath = path;
    m_blueprintName = name.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : name;
}

void BlueprintExecutionContext::enterNode(const BPNode& node) {
    m_nodeId = node.id;
    m_nodeType = node.type;
}

void BlueprintExecutionContext::clearNode() {
    m_nodeId.clear();
    m_nodeType.clear();
}

QString BlueprintExecutionContext::kindName() const {
    switch (m_kind) {
    case BlueprintRuntimeKind::Level: return "关卡蓝图";
    case BlueprintRuntimeKind::Actor: return "Actor 蓝图";
    case BlueprintRuntimeKind::Component: return "组件蓝图";
    case BlueprintRuntimeKind::Effect: return "效果蓝图";
    }
    return "蓝图";
}

QString BlueprintExecutionContext::scopeLabel() const {
    QStringList parts;
    parts << kindName();
    if (!m_blueprintName.isEmpty()) parts << m_blueprintName;
    if (!m_ownerActorId.isEmpty()) parts << QString("Owner=%1").arg(m_ownerActorId);
    if (!m_componentInstanceId.isEmpty()) parts << QString("组件实例=%1").arg(m_componentInstanceId);
    if (!m_eventName.isEmpty()) parts << QString("事件=%1").arg(m_eventName);
    return parts.join(" / ");
}

QString BlueprintExecutionContext::nodeLabel() const {
    QStringList parts;
    if (!m_nodeType.isEmpty()) parts << m_nodeType;
    if (!m_nodeId.isEmpty()) parts << QString("节点=%1").arg(m_nodeId);
    return parts.join(" / ");
}

QString BlueprintExecutionContext::formatDiagnostic(const QString& message) const {
    QStringList parts;
    parts << scopeLabel();
    const QString node = nodeLabel();
    if (!node.isEmpty()) parts << node;
    parts << message;
    const QString line = parts.join("：");
    addTrace(message);
    return line;
}

void BlueprintExecutionContext::addTrace(const QString& message) const {
    QStringList parts;
    parts << scopeLabel();
    const QString node = nodeLabel();
    if (!node.isEmpty()) parts << node;
    if (!message.isEmpty()) parts << message;
    m_traceLines << parts.join("：");
    while (m_traceLines.size() > m_traceLimit)
        m_traceLines.removeFirst();
}
