#pragma once
#include "models/LevelDocument.h"
#include "models/BPClass.h"
#include "models/BPValue.h"
#include <QObject>
#include <QSet>
#include <QMap>

class UIRuntime;
class BPRuntime;

class ActorBPRuntime : public QObject {
    Q_OBJECT
public:
    // actors: points to BPRuntime::mutableActors(), shared list；rt 供切换动画素材后重算帧
    explicit ActorBPRuntime(const BPClass* bpClass,
                             const QString& actorId,
                             QList<ActorData>* actors,
                             BPRuntime* rt,
                             QObject* parent = nullptr);

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);
    void triggerKeyUp(const QString& key);
    void triggerTick(float dt);
    void setUIRuntime(UIRuntime* ui) { m_uiRuntime = ui; }
    void triggerButtonClick(const QString& instanceId, const QString& widgetName);
    void triggerDropdownChanged(const QString& instanceId, const QString& widgetName, int index);
    void triggerUIDragStarted(const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerUIDragMoved  (const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerUIDropped    (const QString& instanceId, const QString& widgetName, float x, float y);
    void triggerUIDragCanceled(const QString& instanceId, const QString& widgetName, float x, float y);
    // 碰撞时：仅当 selfId == 本实例 actorId 才触发本蓝图的「碰撞时」事件
    void triggerCollision(const QString& selfId, const QString& otherId, const QString& otherTag);

signals:
    void printOutput(const QString& text);
    void loadLevelRequested(const QString& levelName);   // 跳转关卡（转交 EditorWindow）
    void backLevelRequested();                           // 返回上一关

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
    BPRuntime*        m_rt = nullptr;   // 所属关卡运行时（共享动画素材缓存）
    float             m_deltaTick = 0.0f;
    QSet<QString>     m_heldKeys;     // 当前按住的键，每帧驱动 held 链（与关卡蓝图对齐）
    QString           m_collOther, m_collTag;   // 「碰撞时」事件当前上下文（self = m_actorId）
    UIRuntime*             m_uiRuntime    = nullptr;
    QMap<QString, QString> m_uiRefs;        // nodeId(UI.Create) → instanceId
    QMap<QString, int>     m_dropdownIndex; // nodeId → 最新选中索引
    struct UIDragState { QString widgetName; float x = 0.0f; float y = 0.0f; };
    QMap<QString, UIDragState> m_uiDragState;
};
