#pragma once
#include "models/LevelDocument.h"
#include "models/BPClass.h"
#include <QObject>
#include <QSet>

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
    void triggerTick(float dt);

private:
    void    triggerEvent(const QString& eventType, const QString& eventParam = {});
    void    executeChain(const QString& fromNodeId, const QString& fromPin,
                         QSet<QString>* visited = nullptr);
    QString executeNode(const QString& nodeId);
    QString resolveDataPin(const QString& nodeId, const QString& pinKey);
    QString resolveOutputPin(const QString& nodeId, const QString& pinKey);
    const BPNode* findNode(const QString& id) const;
    ActorData*    findSelf();

    const BPClass*    m_bpClass;
    QString           m_actorId;
    QList<ActorData>* m_actors;
    float             m_deltaTick = 0.0f;
};
