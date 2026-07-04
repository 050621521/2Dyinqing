#pragma once

#include "BlueprintExecutionContext.h"
#include "BlueprintNodeExecutor.h"
#include "BlueprintVariableScope.h"
#include "models/BPClass.h"
#include "models/LevelDocument.h"

#include <QObject>
#include <QMap>
#include <QSet>

class BPRuntime;
class UIRuntime;

class ComponentBPRuntime : public QObject {
    Q_OBJECT
public:
    ComponentBPRuntime(const BPClass* bpClass,
                       const QString& ownerActorId,
                       const ComponentInstance& instance,
                       QList<ActorData>* actors,
                       BPRuntime* runtime,
                       QObject* parent = nullptr);

    QString componentInstanceId() const { return m_instance.id; }
    QString ownerActorId() const { return m_ownerActorId; }
    bool enabled() const { return m_instance.enabled; }

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);
    void triggerKeyUp(const QString& key);
    void triggerTick(float dt);
    void setUIRuntime(UIRuntime* ui) { m_uiRuntime = ui; }
    void triggerButtonClick(const QString& instanceId, const QString& widgetName);
    void triggerDropdownChanged(const QString& instanceId, const QString& widgetName, int index);
    void triggerUIDragStarted(const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerUIDragMoved(const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerUIDropped(const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerUIDragCanceled(const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerCollision(const QString& selfId, const QString& otherId, const QString& otherTag);

signals:
    void printOutput(const QString& text);
    void loadLevelRequested(const QString& levelName);
    void backLevelRequested();

private:
    void triggerEvent(const QString& eventType, const QString& pinName = {});
    void executeChain(const QString& fromNodeId, const QString& fromPin, QSet<QString>* visited = nullptr);
    QString executeNode(const QString& nodeId);
    BPValue resolveDataPin(const QString& nodeId, const QString& pinKey);
    BPValue resolveOutputPin(const QString& nodeId, const QString& pinKey);
    const BPNode* findNode(const QString& id) const;
    ActorData* ownerActor();
    QString uiNameForInstance(const QString& instanceId) const;

    const BPClass* m_bpClass = nullptr;
    QString m_ownerActorId;
    ComponentInstance m_instance;
    QList<ActorData>* m_actors = nullptr;
    BPRuntime* m_runtime = nullptr;
    UIRuntime* m_uiRuntime = nullptr;
    float m_deltaTick = 0.0f;
    QSet<QString> m_heldKeys;
    QString m_collOther;
    QString m_collTag;
    QMap<QString, QString> m_uiRefs;
    QMap<QString, int> m_dropdownIndex;
    struct UIDragState { QString widgetName; float x = 0.0f; float y = 0.0f; };
    QMap<QString, UIDragState> m_uiDragState;
    QMap<QString, BPValue> m_varStore;
    BlueprintVariableScope m_localScope;
    QHash<QString, BlueprintLoopState> m_loopState;
    BlueprintExecutionContext m_context;
    QSet<QString> m_reportedUnknownNodes;
};
