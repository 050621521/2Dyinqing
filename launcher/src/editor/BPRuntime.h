#pragma once
#include "models/LevelDocument.h"
#include <QObject>
#include <QSet>

class BPRuntime : public QObject {
    Q_OBJECT
public:
    explicit BPRuntime(const LevelDocument* doc, QObject* parent = nullptr);

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);

    const QList<ActorData>& actors()   const { return m_actors; }
    const QStringList&      printLog() const { return m_printLog; }

signals:
    void stateChanged();

private:
    void    executeChain(const QString& fromNodeId, const QString& fromPin,
                         QSet<QString>* visited = nullptr);
    QString executeNode (const QString& nodeId);
    QString resolveDataPin  (const QString& nodeId, const QString& pinKey);
    QString resolveOutputPin(const QString& nodeId, const QString& pinKey);
    const BPNode* findNode(const QString& id) const;

    QList<BPNode>       m_nodes;
    QList<BPConnection> m_connections;
    QList<ActorData>    m_actors;
    QStringList         m_printLog;
};
