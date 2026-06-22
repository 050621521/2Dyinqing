#pragma once
#include "models/LevelDocument.h"
#include "models/BPValue.h"
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
    void appendPrintLog(const QString& text) { m_printLog << text; emit printAppended(text); }

    void setUIRuntime(UIRuntime* ui);
    // 全局变量表（由 EditorWindow 持有，跨关卡保留）
    void setGlobalVars(QMap<QString, BPValue>* g) { m_globalVars = g; }

signals:
    void stateChanged();
    void printAppended(const QString& text);  // 运行时打印 → 复现录制时序记录
    void loadLevelRequested(const QString& levelName);
    void backLevelRequested();   // 返回上一关：由 EditorWindow 弹关卡历史栈处理

private slots:
    void tick();

private:
    // 装载时把 Macro:: 调用节点内联展开为其子图（宏=内联语义，支持嵌套）
    void    flattenMacros(const QString& projectRoot);
    void    tickComponents(float dt);
    void    triggerTick(float dt);
    void    executeChain(const QString& fromNodeId, const QString& fromPin,
                         QSet<QString>* visited = nullptr);
    QString executeNode(const QString& nodeId);
    BPValue resolveDataPin(const QString& nodeId, const QString& pinKey);
    BPValue resolveOutputPin(const QString& nodeId, const QString& pinKey);
    const BPNode*    findNode(const QString& id) const;
    const ActorData* findActorByName(const QString& name) const;

    QList<BPNode>       m_nodes;
    QList<BPConnection> m_connections;
    QList<ActorData>    m_actors;
    QStringList         m_printLog;
    QMap<QString, BPValue> m_varStore;  // 本关运行内变量表（name → value）
    QMap<QString, BPValue>* m_globalVars = nullptr;  // 全局变量表（EditorWindow 持有）

    UIRuntime*             m_uiRuntime = nullptr;
    QMap<QString, QString> m_uiRefs;      // nodeId(UI.Create) → instanceId
    QMap<QString, int>     m_dropdownIndex; // UI.OnDropdownChanged nodeId → 最新索引

    QTimer*       m_tickTimer  = nullptr;
    QElapsedTimer m_elapsedTimer;
    float         m_deltaTick  = 0.0f;
    float         m_lastDt     = 0.016f;
};
