#pragma once
#include "models/LevelDocument.h"
#include "models/BPValue.h"
#include "models/AnimationAsset.h"
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

    // 碰撞 pass：每帧所有移动完成后调用（分轴阻挡解析 + 重叠事件）
    void runCollisionPass();
    // 触发关卡蓝图的「碰撞时」事件（self 撞到 other）
    void triggerCollision(const QString& selfId, const QString& otherId, const QString& otherTag);

signals:
    void stateChanged();
    void printAppended(const QString& text);  // 运行时打印 → 复现录制时序记录
    void loadLevelRequested(const QString& levelName);
    void backLevelRequested();   // 返回上一关：由 EditorWindow 弹关卡历史栈处理
    // 重叠事件：self 的碰撞盒重叠到 other（响应=重叠）；EditorWindow 分发给各运行时
    void overlapDetected(const QString& selfId, const QString& otherId, const QString& otherTag);

private slots:
    void tick();

private:
    // 装载时把 Macro:: 调用节点内联展开为其子图（宏=内联语义，支持嵌套）
    void    flattenMacros(const QString& projectRoot);
    void    tickComponents(float dt);
    void    triggerTick(float dt);
    void    advanceAnimations(float dt);     // 每帧推进动画器当前片段
    void    initAnimations();                // BeginPlay：按自动播放设置初始帧
    const AnimationAsset& animAssetFor(const QString& path);  // 缓存加载 .anim
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

    // ForEach 迭代状态：nodeId → (当前元素, 当前索引)，循环体内的输出引脚读取它
    struct LoopState { BPValue element; int index = 0; };
    QHash<QString, LoopState> m_loopState;

    UIRuntime*             m_uiRuntime = nullptr;
    QMap<QString, QString> m_uiRefs;      // nodeId(UI.Create) → instanceId
    QMap<QString, int>     m_dropdownIndex; // UI.OnDropdownChanged nodeId → 最新索引
    QSet<QString>          m_heldKeys;     // 当前被按住的按键集合，每帧驱动按键节点的 held 链
    // 「碰撞时」事件当前上下文（执行链读取输出引脚时用）
    QString m_collSelf, m_collOther, m_collTag;
    // 接触状态：actorId → 当前正接触的对方 id 集合（用于「刚接触触发一次」）
    QHash<QString, QSet<QString>> m_overlapState;
    QHash<QString, AnimationAsset> m_animCache;  // .anim 路径 → 已加载资源

    QTimer*       m_tickTimer  = nullptr;
    QElapsedTimer m_elapsedTimer;
    float         m_deltaTick  = 0.0f;
    float         m_lastDt     = 0.016f;
};
