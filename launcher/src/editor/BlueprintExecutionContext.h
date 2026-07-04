#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

struct BPClass;
struct BPNode;

enum class BlueprintRuntimeKind {
    Level,
    Actor,
    Component,
    Effect
};

class BlueprintExecutionContext {
public:
    void setRuntimeKind(BlueprintRuntimeKind kind) { m_kind = kind; }
    void setBlueprint(const BPClass* bpClass);
    void setBlueprintInfo(const QString& name, const QString& path);
    void setOwnerActor(const QString& actorId) { m_ownerActorId = actorId; }
    void setComponentInstance(const QString& instanceId) { m_componentInstanceId = instanceId; }
    void setEventName(const QString& eventName) { m_eventName = eventName; }
    void enterNode(const BPNode& node);
    void clearNode();

    BlueprintRuntimeKind runtimeKind() const { return m_kind; }
    QString kindName() const;
    QString blueprintName() const { return m_blueprintName; }
    QString blueprintPath() const { return m_blueprintPath; }
    QString ownerActorId() const { return m_ownerActorId; }
    QString componentInstanceId() const { return m_componentInstanceId; }
    QString eventName() const { return m_eventName; }
    QString nodeId() const { return m_nodeId; }
    QString nodeType() const { return m_nodeType; }

    QString scopeLabel() const;
    QString nodeLabel() const;
    QString formatDiagnostic(const QString& message) const;
    void addTrace(const QString& message) const;
    QStringList recentTraceLines() const { return m_traceLines; }
    void clearTrace() const { m_traceLines.clear(); }

private:
    BlueprintRuntimeKind m_kind = BlueprintRuntimeKind::Level;
    QString m_blueprintName;
    QString m_blueprintPath;
    QString m_ownerActorId;
    QString m_componentInstanceId;
    QString m_eventName;
    QString m_nodeId;
    QString m_nodeType;
    mutable QStringList m_traceLines;
    int m_traceLimit = 64;
};
