#pragma once
#include "models/LevelDocument.h"
#include "models/BPClass.h"
#include "models/BPValue.h"
#include <QObject>
#include <QSet>
#include <QMap>

class UIRuntime;

class ActorBPRuntime : public QObject {
    Q_OBJECT
public:
    // actors: points to BPRuntime::mutableActors(), shared list
    explicit ActorBPRuntime(const BPClass* bpClass,
                             const QString& actorId,
                             QList<ActorData>* actors,
                             QObject* parent = nullptr);

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);
    void triggerKeyUp(const QString& key);
    void triggerTick(float dt);
    void setUIRuntime(UIRuntime* ui) { m_uiRuntime = ui; }
    void triggerButtonClick(const QString& instanceId, const QString& widgetName);
    void triggerDropdownChanged(const QString& instanceId, const QString& widgetName, int index);

signals:
    void printOutput(const QString& text);

private:
    void    triggerEvent(const QString& eventType, const QString& eventParam = {});
    void    executeChain(const QString& fromNodeId, const QString& fromPin,
                         QSet<QString>* visited = nullptr);
    QString executeNode(const QString& nodeId);
    BPValue resolveDataPin(const QString& nodeId, const QString& pinKey);
    BPValue resolveOutputPin(const QString& nodeId, const QString& pinKey);
    const BPNode* findNode(const QString& id) const;
    ActorData*    findSelf();

    const BPClass*    m_bpClass;
    QString           m_actorId;
    QList<ActorData>* m_actors;
    float             m_deltaTick = 0.0f;
    UIRuntime*             m_uiRuntime    = nullptr;
    QMap<QString, QString> m_uiRefs;        // nodeId(UI.Create) → instanceId
    QMap<QString, int>     m_dropdownIndex; // nodeId → 最新选中索引
};
