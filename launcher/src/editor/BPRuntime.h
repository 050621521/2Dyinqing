#pragma once
#include "models/LevelDocument.h"
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>

class BPRuntime : public QObject {
    Q_OBJECT
public:
    explicit BPRuntime(const LevelDocument* doc, QObject* parent = nullptr);

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);

    const QList<ActorData>& actors()   const { return m_actors; }
    // mutableActors() 只在运行时由 ActorBPRuntime 持有指针，运行期间不允许增删 Actor，列表不会重分配
    QList<ActorData>&       mutableActors()  { return m_actors; }
    float                   lastDt()   const { return m_lastDt; }
    const QStringList&      printLog() const { return m_printLog; }

signals:
    void stateChanged();

private slots:
    void tick();

private:
    void    tickComponents(float dt);
    void    triggerTick(float dt);
    void    executeChain(const QString& fromNodeId, const QString& fromPin,
                         QSet<QString>* visited = nullptr);
    QString executeNode(const QString& nodeId);
    QString resolveDataPin(const QString& nodeId, const QString& pinKey);
    QString resolveOutputPin(const QString& nodeId, const QString& pinKey);
    const BPNode*    findNode(const QString& id) const;
    const ActorData* findActorByName(const QString& name) const;

    QList<BPNode>       m_nodes;
    QList<BPConnection> m_connections;
    QList<ActorData>    m_actors;
    QStringList         m_printLog;

    QTimer*       m_tickTimer  = nullptr;
    QElapsedTimer m_elapsedTimer;
    float         m_deltaTick  = 0.0f;
    float         m_lastDt     = 0.016f;
};
