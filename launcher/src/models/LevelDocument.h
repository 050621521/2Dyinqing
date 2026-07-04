#pragma once
#include <QString>
#include <QList>
#include <QStringList>
#include <QJsonObject>
#include <QColor>
#include <QRect>
#include <QMap>
#include <QSet>
#include <QUndoStack>
#include "editor/GlobalVars.h"   // 复用 GlobalVarDef（name+type）作为局部变量声明

struct ComponentInstance {
    QString id;
    QString blueprint;
    bool enabled = true;
    QJsonObject defaultOverrides;

    QJsonObject toJson() const;
    static ComponentInstance fromJson(const QJsonValue& value);
    static ComponentInstance fromBlueprint(const QString& blueprint);
};

struct ActorData {
    QString id;
    QString name;
    QString bpClass;   // e.g. "builtin/Sprite", "builtin/Camera", "Blueprints/Player.bp"
    float x = 0, y = 0;
    float rotation = 0;
    float scaleX = 1, scaleY = 1;

    bool        active     = true;
    bool        isStatic   = false;
    QString     tag        = "未标记";
    QString     layer      = "默认";
    QStringList components;
    QStringList componentBlueprints; // 挂载的组件蓝图路径，项目相对路径或绝对路径
    QList<ComponentInstance> componentInstances; // 真实组件蓝图实例；旧 componentBlueprints 自动迁移

    // 活继承：已被本实例覆盖的字段（toJson 键名）；未覆盖字段跟随蓝图类默认值
    QSet<QString> overriddenFields;

    // 精灵渲染器属性
    QString spritePath;
    QColor  spriteColor  = QColor(255, 255, 255, 255);
    QString sortingLayer = "默认";
    int     orderInLayer = 0;
    bool    flipX        = false;
    bool    flipY        = false;
    QString drawMode     = "简单";
    bool    spriteVisible = true;

    // 动画器组件属性（持久化）
    QString animAsset;            // .anim 路径（绝对路径，约定同 spritePath）
    QString animDefaultClip;      // 默认播放片段名
    bool    animAutoPlay = true;  // 开始运行即自动播放默认片段

    // 动画器运行时瞬态（不序列化）
    QString animCurClip;
    int     animFrameIndex = 0;
    double  animTimeAccum  = 0.0;
    bool    animPlaying    = false;
    QString animSheetPath;        // 解析出的精灵表路径，空 = 不走动画绘制
    QRect   animSrc;              // 当前帧源矩形

    // 摄像机组件属性
    float   cameraSize         = 540.0f;
    bool    cameraIsMain       = false;
    int     cameraResW         = 1920;
    int     cameraResH         = 1080;
    bool    cameraOrthographic = true;
    QColor  cameraBackground   = QColor(30, 30, 30, 255);

    // 跟随控制组件属性
    QString followTarget;
    float   followLerpSpeed    = 5.0f;
    float   followOffsetX      = 0.0f;
    float   followOffsetY      = 0.0f;

    // 边界限制组件属性
    bool    confinerEnabled    = false;
    QString confinerActor;           // 绑定 Trigger Actor 名称（优先于手填）
    float   confinerMinX       = -500.0f;
    float   confinerMaxX       =  500.0f;
    float   confinerMinY       = -500.0f;
    float   confinerMaxY       =  500.0f;

    // 碰撞盒组件属性（AABB；过滤用 Actor 级 tag）
    bool    colliderEnabled    = false;
    float   colliderW          = 100.0f;
    float   colliderH          = 100.0f;
    float   colliderOffsetX    = 0.0f;
    float   colliderOffsetY    = 0.0f;
    QString colliderResponse   = "阻挡";   // "阻挡" | "重叠"
    QString colliderTargets;               // 逗号分隔的目标标签；空 = 除自身标签外所有

    // 碰撞运行时瞬态（不序列化）：上一帧位置，用于分轴阻挡回退
    float   prevX = 0.0f, prevY = 0.0f;

    QJsonObject toJson() const;
    static ActorData fromJson(const QJsonObject& obj);
};

// ── Blueprint 数据模型 ────────────────────────────────────────────────────
struct BPNode {
    QString id;
    QString type;
    float   x = 100.0f;
    float   y = 100.0f;
    QMap<QString, QString> params;

    QJsonObject toJson() const;
    static BPNode fromJson(const QJsonObject& obj);
};

struct BPConnection {
    QString id;
    QString fromNode;
    QString fromPin;
    QString toNode;
    QString toPin;

    QJsonObject toJson() const;
    static BPConnection fromJson(const QJsonObject& obj);
};

class LevelDocument {
public:
    bool load(const QString& filePath);
    bool save();

    bool isDirty() const { return m_dirty; }

    const QList<ActorData>& actors() const { return m_actors; }
    const QList<ActorData>& sortedActors() const { return m_sortedActors; }
    void addActor(const ActorData& actor);
    void removeActor(const QString& id);
    void updateActor(const ActorData& actor);

    // 蓝图接口
    const QList<BPNode>&       bpNodes()       const { return m_bpNodes; }
    const QList<BPConnection>& bpConnections() const { return m_bpConnections; }
    void addBPNode(const BPNode& node);
    void removeBPNode(const QString& id);
    void updateBPNode(const BPNode& node);
    void addBPConnection(const BPConnection& conn);
    void removeBPConnection(const QString& id);

    // 局部变量（本关卡蓝图私有；声明随关卡持久化，值每次运行重置）
    const QList<GlobalVarDef>& localVars() const { return m_localVars; }
    void setLocalVars(const QList<GlobalVarDef>& vars);

    QString name() const { return m_name; }
    QString filePath() const { return m_filePath; }

    QUndoStack* undoStack();

    ~LevelDocument();

private:
    void rebuildSortedActors();

    QString          m_filePath;
    QString          m_name;
    QList<ActorData> m_actors;
    QList<ActorData> m_sortedActors;
    bool             m_dirty = false;

    QList<BPNode>       m_bpNodes;
    QList<BPConnection> m_bpConnections;
    QList<GlobalVarDef> m_localVars;

    QUndoStack* m_undoStack = nullptr;
};
