#pragma once
#include "models/LevelDocument.h"
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QMap>
#include <QString>

class UIRuntime;

class BPRuntime : public QObject {
    Q_OBJECT
public:
    explicit BPRuntime(const LevelDocument* doc, QObject* parent = nullptr);

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);
    void triggerKeyUp(const QString& key);
    void triggerButtonClick(const QString& instanceId, const QString& widgetName);
    void triggerDropdownChanged(const QString& instanceId, const QString& widgetName, int index);

    const QList<ActorData>& actors()   const { return m_actors; }
    // mutableActors() 只在运行时由 ActorBPRuntime 持有指针，运行期间不允许增删 Actor，列表不会重分配
    QList<ActorData>&       mutableActors()  { return m_actors; }
    float                   lastDt()   const { return m_lastDt; }
    const QStringList&      printLog() const { return m_printLog; }
    void appendPrintLog(const QString& text) { m_printLog << text; }

    void setUIRuntime(UIRuntime* ui);

signals:
    void stateChanged();
    void loadLevelRequested(const QString& levelName);

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
    QMap<QString, QString> m_varStore;  // 运行时变量表（name → value）

    UIRuntime*             m_uiRuntime = nullptr;
    QMap<QString, QString> m_uiRefs;      // nodeId(UI.Create) → instanceId
    QMap<QString, int>     m_dropdownIndex; // UI.OnDropdownChanged nodeId → 最新索引

    QTimer*       m_tickTimer  = nullptr;
    QElapsedTimer m_elapsedTimer;
    float         m_deltaTick  = 0.0f;
    float         m_lastDt     = 0.016f;
};
