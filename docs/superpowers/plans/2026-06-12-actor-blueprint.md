# Actor 级蓝图系统 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让每个 Actor 实例持有独立蓝图（BPClass），蓝图类取代原有 type 枚举，ActorBPRuntime 在运行时为每个有节点的 Actor 独立执行逻辑。

**Architecture:** 新增 `BPClass` 数据模型（组件列表 + 节点图），`ActorData.type` 字段改为 `bpClass`（如 `"builtin/Sprite"`）。运行时新增 `ActorBPRuntime`，每个含节点的 Actor 实例化一个，持有指向共享 actor 列表中自身元素的指针并执行 self 节点。关卡蓝图 `BPRuntime` 保持不变。

**Tech Stack:** C++17, Qt6 Widgets, CMake, QtADS。无单元测试框架——"测试"步骤均为编译 + 手动运行验证。

---

## 文件映射

| 操作 | 文件 | 职责 |
|---|---|---|
| 新建 | `src/models/BPClass.h` | BPClass 数据结构，内置类工厂 |
| 新建 | `src/models/BPClass.cpp` | toJson/fromJson/save/load，builtinClasses() |
| 新建 | `src/editor/ActorBPRuntime.h` | 单 Actor 蓝图执行器接口 |
| 新建 | `src/editor/ActorBPRuntime.cpp` | self 节点执行、tick/事件触发 |
| 修改 | `src/models/LevelDocument.h` | `type` → `bpClass` |
| 修改 | `src/models/LevelDocument.cpp` | toJson/fromJson + 旧格式迁移 |
| 修改 | `src/models/ActorTypeUtils.h` | 新增 bpClass 版工具函数 |
| 修改 | `src/editor/Viewport2D.cpp` | 8 处 type 引用 → bpClass |
| 修改 | `src/editor/GameViewport.cpp` | 3 处 type 引用 → bpClass |
| 修改 | `src/editor/DetailsPanel.cpp` | type → bpClass；加"编辑蓝图"按钮 |
| 修改 | `src/editor/DetailsPanel.h` | 新增 editBpClassRequested 信号 |
| 修改 | `src/editor/SceneOutliner.cpp` | 2 处 type 引用 → bpClass |
| 修改 | `src/editor/ContentBrowser.h` | 新增 bpClassOpenRequested 信号 |
| 修改 | `src/editor/ContentBrowser.cpp` | .bp 文件显示、双击、新建 |
| 修改 | `src/editor/BlueprintEditor.h` | loadBpClass()，bpClassModified 信号 |
| 修改 | `src/editor/BlueprintEditor.cpp` | bpClass 模式，self 节点定义与过滤 |
| 修改 | `src/editor/EditorWindow.h` | ActorBPRuntime 列表，openBpClassTab() |
| 修改 | `src/editor/EditorWindow.cpp` | startRuntime/stop，.bp tab 处理 |
| 修改 | `launcher/CMakeLists.txt` | 加入 4 个新文件 |

---

## Task 1: BPClass 数据模型

**Files:**
- Create: `src/models/BPClass.h`
- Create: `src/models/BPClass.cpp`
- Modify: `launcher/CMakeLists.txt`

- [ ] **Step 1: 写 BPClass.h**

```cpp
// src/models/BPClass.h
#pragma once
#include "LevelDocument.h"
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <QJsonObject>

struct BPClass {
    QString                  name;
    QString                  filePath;   // 空字符串 = 内置类
    QStringList              components;
    QMap<QString, QVariant>  defaults;
    QList<BPNode>            nodes;
    QList<BPConnection>      connections;

    bool isBuiltin() const { return filePath.isEmpty(); }
    bool hasNodes()  const { return !nodes.isEmpty(); }

    bool save() const;
    static BPClass load(const QString& filePath);

    QJsonObject toJson() const;
    static BPClass fromJson(const QJsonObject& obj, const QString& filePath = {});

    // 内置类工厂：bpClass 字符串如 "builtin/Sprite" → BPClass
    static const BPClass* findBuiltin(const QString& bpClass);
    static QList<BPClass> builtinClasses();
};
```

- [ ] **Step 2: 写 BPClass.cpp**

```cpp
// src/models/BPClass.cpp
#include "BPClass.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>

QJsonObject BPClass::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    QJsonArray comps;
    for (const QString& c : components) comps.append(c);
    obj["components"] = comps;
    QJsonArray nodesArr;
    for (const BPNode& n : nodes) nodesArr.append(n.toJson());
    obj["nodes"] = nodesArr;
    QJsonArray connsArr;
    for (const BPConnection& c : connections) connsArr.append(c.toJson());
    obj["connections"] = connsArr;
    return obj;
}

BPClass BPClass::fromJson(const QJsonObject& obj, const QString& fp) {
    BPClass bc;
    bc.name     = obj["name"].toString();
    bc.filePath = fp;
    for (const QJsonValue& v : obj["components"].toArray())
        bc.components.append(v.toString());
    for (const QJsonValue& v : obj["nodes"].toArray())
        bc.nodes.append(BPNode::fromJson(v.toObject()));
    for (const QJsonValue& v : obj["connections"].toArray())
        bc.connections.append(BPConnection::fromJson(v.toObject()));
    return bc;
}

bool BPClass::save() const {
    if (filePath.isEmpty()) return false;
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson());
    return f.commit();
}

BPClass BPClass::load(const QString& fp) {
    QFile f(fp);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return fromJson(obj, fp);
}

const BPClass* BPClass::findBuiltin(const QString& bpClass) {
    static const QList<BPClass> list = builtinClasses();
    for (const BPClass& bc : list) {
        if ("builtin/" + bc.name == bpClass) return &bc;
    }
    return nullptr;
}

QList<BPClass> BPClass::builtinClasses() {
    return {
        { "Sprite",  {}, {"变换", "精灵渲染器"},  {}, {}, {} },
        { "Camera",  {}, {"变换", "摄像机组件"},   {}, {}, {} },
        { "Light",   {}, {"变换", "点光源"},       {}, {}, {} },
        { "Trigger", {}, {"变换", "碰撞盒"},       {}, {}, {} },
        { "Empty",   {}, {"变换"},                {}, {}, {} },
    };
}
```

- [ ] **Step 3: 更新 CMakeLists.txt，在 SOURCES 和 HEADERS 列表中加入新文件**

在 `SOURCES` 列表末尾加：
```cmake
    src/models/BPClass.cpp
```
在 `HEADERS` 列表末尾加：
```cmake
    src/models/BPClass.h
```

- [ ] **Step 4: 编译验证**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```
期望：`[100%] Linking CXX executable launcher.app/Contents/MacOS/launcher` 无错误。

- [ ] **Step 5: 提交**

```bash
git add src/models/BPClass.h src/models/BPClass.cpp launcher/CMakeLists.txt
git commit -m "feat: 新增 BPClass 数据模型与内置类工厂"
```

---

## Task 2: ActorData 迁移（type → bpClass）

**Files:**
- Modify: `src/models/LevelDocument.h`
- Modify: `src/models/LevelDocument.cpp`
- Modify: `src/models/ActorTypeUtils.h`
- Modify: `src/editor/Viewport2D.cpp`
- Modify: `src/editor/GameViewport.cpp`
- Modify: `src/editor/DetailsPanel.cpp`
- Modify: `src/editor/SceneOutliner.cpp`
- Modify: `src/editor/ContentBrowser.cpp`

- [ ] **Step 1: 修改 LevelDocument.h — 把 `QString type` 改为 `QString bpClass`**

在 `src/models/LevelDocument.h` 第 12 行，将：
```cpp
    QString type;
```
替换为：
```cpp
    QString bpClass;   // e.g. "builtin/Sprite", "builtin/Camera", "Blueprints/Player.bp"
```

- [ ] **Step 2: 修改 LevelDocument.cpp toJson — `type` → `bpClass`**

在 `ActorData::toJson()` 中，将：
```cpp
    obj["type"]        = type;
```
替换为：
```cpp
    obj["bpClass"]     = bpClass;
```

- [ ] **Step 3: 修改 LevelDocument.cpp fromJson — 加旧格式迁移**

在 `ActorData::fromJson()` 中，将：
```cpp
    a.type     = obj["type"].toString();
```
替换为：
```cpp
    // 旧格式迁移："type": "Sprite" → bpClass: "builtin/Sprite"
    if (obj.contains("bpClass"))
        a.bpClass = obj["bpClass"].toString();
    else if (obj.contains("type"))
        a.bpClass = "builtin/" + obj["type"].toString();
```

- [ ] **Step 4: 更新 ActorTypeUtils.h — 新增 bpClass 版工具函数**

在 `src/models/ActorTypeUtils.h` 末尾，在最后一个 `}` 之后追加：

```cpp
// ── bpClass 版工具函数 ────────────────────────────────────────────────

inline QString bpClassToTypeName(const QString& bpClass) {
    // "builtin/Sprite" → "Sprite"，自定义类返回空
    return bpClass.startsWith("builtin/") ? bpClass.mid(8) : QString();
}

inline QStringList defaultComponentsForBpClass(const QString& bpClass) {
    return defaultComponents(bpClassToTypeName(bpClass));
}

inline QString bpClassLabel(const QString& bpClass) {
    if (!bpClass.startsWith("builtin/")) return QFileInfo(bpClass).baseName();
    return typeLabel(bpClassToTypeName(bpClass));
}

inline QColor bpClassColor(const QString& bpClass) {
    return actorTypeColor(bpClassToTypeName(bpClass));
}
```

（需要在文件顶部 `#include` 中加 `#include <QFileInfo>`）

- [ ] **Step 5: 更新 Viewport2D.cpp — 8 处 type 引用**

| 行（近似） | 旧代码 | 新代码 |
|---|---|---|
| `actorTypeColor(a.type)` | `actorTypeColor(a.type)` | `bpClassColor(a.bpClass)` |
| `a.type == "Camera" \|\|` | `a.type == "Camera" \|\|` | `a.bpClass == "builtin/Camera" \|\|` |
| `if (a.type == "Empty")` | `a.type == "Empty"` | `a.bpClass == "builtin/Empty"` |
| `} else if (a.type == "Trigger")` | `a.type == "Trigger"` | `a.bpClass == "builtin/Trigger"` |
| `} else if (a.type == "Light")` | `a.type == "Light"` | `a.bpClass == "builtin/Light"` |
| `if (a.type == "Camera")` | `a.type == "Camera"` | `a.bpClass == "builtin/Camera"` |
| `a.type != "Empty"` | `a.type != "Empty"` | `a.bpClass != "builtin/Empty"` |
| `a.type == "Empty" && !drewPixmap` | `a.type == "Empty"` | `a.bpClass == "builtin/Empty"` |

在 Actor 创建的地方（右键 addMenu 回调，约第 580 行）：
```cpp
// 旧：
a.type       = type;
a.components = defaultComponents(type);
// 新：
a.bpClass    = "builtin/" + type;
a.components = defaultComponents(type);
```

- [ ] **Step 6: 更新 GameViewport.cpp — 3 处**

```cpp
// 旧：
if (a.cameraIsMain && (a.type == "Camera" || a.components.contains("摄像机组件")))
// 新：
if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件")))

// 旧：
if (a.type == "Camera" || a.components.contains("摄像机组件")) continue;
// 新：
if (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件")) continue;

// 旧：
const QColor fill = actorTypeColor(a.type);
// 新：
const QColor fill = bpClassColor(a.bpClass);
```

- [ ] **Step 7: 更新 DetailsPanel.cpp — 2 处**

```cpp
// 旧（约第 347 行）：
m_currentActor.components = defaultComponents(m_currentActor.type);
// 新：
m_currentActor.components = defaultComponentsForBpClass(m_currentActor.bpClass);

// 旧（约第 350 行）：
px.fill(typeColor(actor.type));
// 新：
px.fill(bpClassColor(actor.bpClass));
```

- [ ] **Step 8: 更新 SceneOutliner.cpp — 2 处**

```cpp
// 旧（约第 82 行）：
auto* item = new QTreeWidgetItem(m_tree, QStringList{a.name, typeLabel(a.type)});
// 新：
auto* item = new QTreeWidgetItem(m_tree, QStringList{a.name, bpClassLabel(a.bpClass)});

// 旧（约第 145 行，addMenu 回调）：
a.type       = type;
a.components = defaultComponents(type);
// 新：
a.bpClass    = "builtin/" + type;
a.components = defaultComponents(type);
```

- [ ] **Step 9: 更新 ContentBrowser.cpp — 1 处（约第 509 行）**

```cpp
// 旧：
cameraActor["type"] = "Camera";
// 新：
cameraActor["bpClass"] = "builtin/Camera";
```

- [ ] **Step 10: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -10
```
期望：无错误。若有 `error: 'type' is not a member` 等，找到对应行补全替换。

- [ ] **Step 11: 运行验证**

```bash
open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```
打开已有项目，大纲面板中 Actor 的类型标签显示正常（摄像机/精灵/光源）。放置新 Actor 正常。

- [ ] **Step 12: 提交**

```bash
git add src/models/LevelDocument.h src/models/LevelDocument.cpp \
        src/models/ActorTypeUtils.h \
        src/editor/Viewport2D.cpp src/editor/GameViewport.cpp \
        src/editor/DetailsPanel.cpp src/editor/SceneOutliner.cpp \
        src/editor/ContentBrowser.cpp
git commit -m "feat: ActorData.type 迁移为 bpClass，兼容旧 level 文件格式"
```

---

## Task 3: ActorBPRuntime

**Files:**
- Create: `src/editor/ActorBPRuntime.h`
- Create: `src/editor/ActorBPRuntime.cpp`
- Modify: `src/editor/BPRuntime.h`（加 `mutableActors()`）
- Modify: `launcher/CMakeLists.txt`

- [ ] **Step 1: 写 ActorBPRuntime.h**

```cpp
// src/editor/ActorBPRuntime.h
#pragma once
#include "models/LevelDocument.h"
#include "models/BPClass.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>

class ActorBPRuntime : public QObject {
    Q_OBJECT
public:
    // actors：指向 BPRuntime::mutableActors()，运行时共享列表
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
```

- [ ] **Step 2: 写 ActorBPRuntime.cpp**

```cpp
// src/editor/ActorBPRuntime.cpp
#include "ActorBPRuntime.h"
#include <QSet>

ActorBPRuntime::ActorBPRuntime(const BPClass* bpClass,
                                const QString& actorId,
                                QList<ActorData>* actors,
                                QObject* parent)
    : QObject(parent), m_bpClass(bpClass), m_actorId(actorId), m_actors(actors)
{}

void ActorBPRuntime::triggerBeginPlay() {
    triggerEvent("Event.BeginPlay");
}

void ActorBPRuntime::triggerKeyDown(const QString& key) {
    triggerEvent("Event.KeyDown", key);
}

void ActorBPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;
    triggerEvent("Event.Tick");
}

void ActorBPRuntime::triggerEvent(const QString& eventType, const QString& eventParam) {
    for (const BPNode& node : m_bpClass->nodes) {
        if (node.type != eventType) continue;
        if (eventType == "Event.KeyDown") {
            const QString expected = node.params.value("key");
            if (!expected.isEmpty() && expected.compare(eventParam, Qt::CaseInsensitive) != 0)
                continue;
        }
        QSet<QString> visited;
        executeChain(node.id, "exec_out", &visited);
    }
}

void ActorBPRuntime::executeChain(const QString& fromNodeId, const QString& fromPin,
                                   QSet<QString>* visited) {
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);

    for (const BPConnection& c : m_bpClass->connections) {
        if (c.fromNode == fromNodeId && c.fromPin == fromPin) {
            QString nextPin = executeNode(c.toNode);
            if (!nextPin.isEmpty())
                executeChain(c.toNode, nextPin, visited);
            break;
        }
    }
}

QString ActorBPRuntime::executeNode(const QString& nodeId) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};
    ActorData* self = findSelf();

    // ── 复用关卡蓝图现有节点 ──────────────────────────────────────────
    if (node->type == "Action.Print") {
        return "exec_out";   // 仅返回 exec，actor 级暂不输出 log
    }
    if (node->type == "Flow.Branch") {
        const QString cond = resolveDataPin(nodeId, "condition").toLower();
        const bool truthy = !cond.isEmpty() && cond != "0" && cond != "false";
        return truthy ? "true" : "false";
    }

    if (!self) return {};

    // ── Self 变换节点 ─────────────────────────────────────────────────
    if (node->type == "Self.SetPosition") {
        self->x = resolveDataPin(nodeId, "x").toFloat();
        self->y = resolveDataPin(nodeId, "y").toFloat();
        return "exec_out";
    }
    if (node->type == "Self.SetRotation") {
        self->rotation = resolveDataPin(nodeId, "angle").toFloat();
        return "exec_out";
    }
    if (node->type == "Self.SetActive") {
        const QString v = resolveDataPin(nodeId, "active").toLower();
        self->active = (v == "true" || v == "1");
        return "exec_out";
    }

    // ── Self 精灵渲染器 ───────────────────────────────────────────────
    if (node->type == "Self.Sprite.SetImage") {
        self->spritePath = resolveDataPin(nodeId, "path");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetColor") {
        self->spriteColor = QColor(
            resolveDataPin(nodeId, "r").toInt(),
            resolveDataPin(nodeId, "g").toInt(),
            resolveDataPin(nodeId, "b").toInt(),
            resolveDataPin(nodeId, "a").toInt()
        );
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetFlipX") {
        const QString v = resolveDataPin(nodeId, "flip").toLower();
        self->flipX = (v == "true" || v == "1");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetFlipY") {
        const QString v = resolveDataPin(nodeId, "flip").toLower();
        self->flipY = (v == "true" || v == "1");
        return "exec_out";
    }
    if (node->type == "Self.Sprite.SetVisible") {
        const QString v = resolveDataPin(nodeId, "visible").toLower();
        self->active = (v == "true" || v == "1");
        return "exec_out";
    }

    // ── Self 摄像机 ───────────────────────────────────────────────────
    if (node->type == "Self.Camera.SetSize") {
        self->cameraSize = resolveDataPin(nodeId, "size").toFloat();
        return "exec_out";
    }
    if (node->type == "Self.Camera.SetBackground") {
        self->cameraBackground = QColor(
            resolveDataPin(nodeId, "r").toInt(),
            resolveDataPin(nodeId, "g").toInt(),
            resolveDataPin(nodeId, "b").toInt()
        );
        return "exec_out";
    }

    return {};
}

QString ActorBPRuntime::resolveDataPin(const QString& nodeId, const QString& pinKey) {
    for (const BPConnection& c : m_bpClass->connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(nodeId);
    return node ? node->params.value(pinKey) : QString();
}

QString ActorBPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    ActorData* self = findSelf();

    if (node->type == "Self.GetPosition" && self) {
        if (pinKey == "x") return QString::number(self->x);
        if (pinKey == "y") return QString::number(self->y);
    }
    if (node->type == "Self.GetRotation" && self) {
        if (pinKey == "angle") return QString::number(self->rotation);
    }
    if (node->type == "Self.IsActive" && self) {
        if (pinKey == "active") return self->active ? "true" : "false";
    }
    if (node->type == "Self.GetName" && self) {
        if (pinKey == "name") return self->name;
    }
    if (node->type == "Event.Tick") {
        if (pinKey == "delta_time") return QString::number(m_deltaTick);
    }

    return {};
}

const BPNode* ActorBPRuntime::findNode(const QString& id) const {
    for (const BPNode& n : m_bpClass->nodes)
        if (n.id == id) return &n;
    return nullptr;
}

ActorData* ActorBPRuntime::findSelf() {
    for (ActorData& a : *m_actors)
        if (a.id == m_actorId) return &a;
    return nullptr;
}
```

- [ ] **Step 3: 在 BPRuntime.h 加 `mutableActors()` 和 `lastDt()`**

在 `src/editor/BPRuntime.h` 的 `actors()` 行之后加：
```cpp
    QList<ActorData>&       mutableActors() { return m_actors; }
    float                   lastDt()  const { return m_lastDt; }
```

在 `src/editor/BPRuntime.h` 的 `m_deltaTick` 成员附近加：
```cpp
    float m_lastDt = 0.016f;
```

在 `src/editor/BPRuntime.cpp` 的 `tick()` 函数中，把 `const float dt = ...` 那行改为：
```cpp
    m_lastDt = m_elapsedTimer.restart() / 1000.0f;
    const float dt = m_lastDt;
```

- [ ] **Step 4: 更新 CMakeLists.txt**

在 `SOURCES` 末尾加：
```cmake
    src/editor/ActorBPRuntime.cpp
```
在 `HEADERS` 末尾加：
```cmake
    src/editor/ActorBPRuntime.h
```

- [ ] **Step 5: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```
期望：无错误。

- [ ] **Step 6: 提交**

```bash
git add src/editor/ActorBPRuntime.h src/editor/ActorBPRuntime.cpp \
        src/editor/BPRuntime.h launcher/CMakeLists.txt
git commit -m "feat: 新增 ActorBPRuntime，支持 self 节点执行"
```

---

## Task 4: EditorWindow Runtime 集成

**Files:**
- Modify: `src/editor/EditorWindow.h`
- Modify: `src/editor/EditorWindow.cpp`

- [ ] **Step 1: 更新 EditorWindow.h — 加 ActorBPRuntime 列表和 openBpClassTab**

在 `#include "BPRuntime.h"` 后面加：
```cpp
#include "ActorBPRuntime.h"
#include "models/BPClass.h"
```

在私有成员区，`BPRuntime* m_runtime = nullptr;` 一行之后加：
```cpp
    QList<ActorBPRuntime*>   m_actorRuntimes;
    QMap<QString, BPClass*>  m_openBpClasses;
```

在 `private:` 方法区加：
```cpp
    void openBpClassTab(const QString& bpClassPath);
```

- [ ] **Step 2: 更新 EditorWindow.cpp startRuntime() — 创建 ActorBPRuntime**

在 `startRuntime()` 末尾（`if (m_runBtn)...` 那几行之前），在 `m_runtime->triggerBeginPlay();` 之后加：

```cpp
    // 为每个有节点的 Actor 创建 ActorBPRuntime
    for (const ActorData& actor : doc->actors()) {
        const BPClass* bc = nullptr;
        if (actor.bpClass.startsWith("builtin/")) {
            bc = BPClass::findBuiltin(actor.bpClass);
        } else if (!actor.bpClass.isEmpty()) {
            bc = m_openBpClasses.value(actor.bpClass, nullptr);
            if (!bc) {
                auto* loaded = new BPClass(BPClass::load(
                    m_project.path + "/" + actor.bpClass));
                m_openBpClasses[actor.bpClass] = loaded;
                bc = loaded;
            }
        }
        if (bc && bc->hasNodes()) {
            auto* ar = new ActorBPRuntime(bc, actor.id,
                                          &m_runtime->mutableActors(), this);
            m_actorRuntimes.append(ar);
        }
    }
    // BeginPlay 也触发各 Actor 蓝图
    for (ActorBPRuntime* ar : m_actorRuntimes)
        ar->triggerBeginPlay();
```

在 `connect(m_runtime, &BPRuntime::stateChanged, ...)` 的 lambda 里，在 `m_viewport->updateRuntimeActors(...)` 之前加：

```cpp
        // 触发本帧 Actor Tick（每次关卡 runtime tick 时同步触发）
        const float dt = m_runtime->lastDt();
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerTick(dt);
```

- [ ] **Step 3: 更新 EditorWindow.cpp stopRuntime() — 销毁 ActorBPRuntime**

找到 `stopRuntime()` 函数，在 `delete m_runtime; m_runtime = nullptr;` 之前加：

```cpp
    qDeleteAll(m_actorRuntimes);
    m_actorRuntimes.clear();
```

- [ ] **Step 4: 更新 EditorWindow.cpp — 按键事件转发给 ActorBPRuntime**

找到：
```cpp
    connect(m_viewport, &Viewport2D::keyPressed, this, [this](const QString& key) {
        if (m_runtime && m_pauseBtn && !m_pauseBtn->isChecked())
            m_runtime->triggerKeyDown(key);
    });
```
改为：
```cpp
    connect(m_viewport, &Viewport2D::keyPressed, this, [this](const QString& key) {
        if (!m_runtime || (m_pauseBtn && m_pauseBtn->isChecked())) return;
        m_runtime->triggerKeyDown(key);
        for (ActorBPRuntime* ar : m_actorRuntimes)
            ar->triggerKeyDown(key);
    });
```

- [ ] **Step 5: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

- [ ] **Step 6: 运行验证**

启动编辑器，运行一个场景，确认不崩溃（此时无 Actor 蓝图节点，runtime 正常工作）。

- [ ] **Step 7: 提交**

```bash
git add src/editor/EditorWindow.h src/editor/EditorWindow.cpp
git commit -m "feat: EditorWindow 运行时集成 ActorBPRuntime"
```

---

## Task 5: Self 节点定义 + BlueprintEditor bpClass 模式

**Files:**
- Modify: `src/editor/BlueprintEditor.h`
- Modify: `src/editor/BlueprintEditor.cpp`

- [ ] **Step 1: 更新 BlueprintEditor.h — 加 loadBpClass 和 bpClassModified 信号**

在 `#include "models/LevelDocument.h"` 后加：
```cpp
#include "models/BPClass.h"
```

在 `void loadLevel(LevelDocument* doc);` 后加：
```cpp
    void loadBpClass(BPClass* bpClass);
    void saveBpClass();
```

在 `signals:` 里的 `void documentModified();` 后加：
```cpp
    void bpClassModified();
```

在 `private:` 里的 `LevelDocument* m_doc = nullptr;` 后加：
```cpp
    BPClass* m_bpClass = nullptr;
```

加两个私有 helper 方法（在现有私有方法里）：
```cpp
    // 活跃节点/连线访问（level 模式用 m_doc，bpClass 模式用 m_bpClass）
    const QList<BPNode>&       activeNodes() const;
    const QList<BPConnection>& activeConns() const;
    void notifyModified();     // 根据模式 emit 对应信号
    
    // 节点过滤：bpClass 模式下根据组件列表隐藏不适用的 Self 节点
    bool isSelfNodeVisible(const QString& typeId) const;
```

- [ ] **Step 2: 在 BlueprintEditor.cpp 中实现新方法**

在文件末尾追加：

```cpp
void BlueprintEditor::loadBpClass(BPClass* bpClass) {
    m_bpClass = bpClass;
    m_doc     = nullptr;
    m_selectedNodeId.clear();
    m_dragState = DragState::None;
    hideWireDropPopup();
    cancelInlineEdit();
    update();
}

void BlueprintEditor::saveBpClass() {
    if (m_bpClass) m_bpClass->save();
}

const QList<BPNode>& BlueprintEditor::activeNodes() const {
    if (m_bpClass) return m_bpClass->nodes;
    static QList<BPNode> empty;
    return m_doc ? m_doc->bpNodes() : empty;
}

const QList<BPConnection>& BlueprintEditor::activeConns() const {
    if (m_bpClass) return m_bpClass->connections;
    static QList<BPConnection> empty;
    return m_doc ? m_doc->bpConnections() : empty;
}

void BlueprintEditor::notifyModified() {
    if (m_bpClass) { m_bpClass->save(); emit bpClassModified(); }
    else           { emit documentModified(); }
}

bool BlueprintEditor::isSelfNodeVisible(const QString& typeId) const {
    if (!typeId.startsWith("Self.")) return true;
    if (!m_bpClass) return false;   // level 模式不显示 Self 节点
    if (typeId.startsWith("Self.Sprite.") && !m_bpClass->components.contains("精灵渲染器"))
        return false;
    if (typeId.startsWith("Self.Camera.") && !m_bpClass->components.contains("摄像机组件"))
        return false;
    return true;
}
```

- [ ] **Step 3: 更新 BlueprintEditor.cpp — 把所有对 m_doc 的节点/连线访问替换为 activeNodes()/activeConns()**

全文搜索以下模式，替换为对应 helper：

```
// 读取节点列表（已有多处）：
m_doc->bpNodes()       → activeNodes()
m_doc->bpConnections() → activeConns()

// 写入（添加/修改/删除节点和连线）：
m_doc->addBPNode(node)          → if(m_bpClass) m_bpClass->nodes.append(node); else m_doc->addBPNode(node);
m_doc->updateBPNode(node)       → if(m_bpClass) { 替换; } else m_doc->updateBPNode(node);
m_doc->removeBPNode(id)         → if(m_bpClass) { 删除; } else m_doc->removeBPNode(id);
m_doc->addBPConnection(conn)    → if(m_bpClass) m_bpClass->connections.append(conn); else m_doc->addBPConnection(conn);
m_doc->removeBPConnection(id)   → if(m_bpClass) { 删除; } else m_doc->removeBPConnection(id);
emit documentModified()         → notifyModified()
```

为避免大范围替换出错，用 helper 方法封装写操作：在 `.cpp` 文件中加：

```cpp
// 写操作 helpers（在 notifyModified 定义后追加）
static void addNodeToActive(BPClass* bc, LevelDocument* doc, const BPNode& node) {
    if (bc) bc->nodes.append(node);
    else if (doc) doc->addBPNode(node);
}
static void removeNodeFromActive(BPClass* bc, LevelDocument* doc, const QString& id) {
    if (bc) { bc->nodes.removeIf([&](const BPNode& n){ return n.id == id; }); }
    else if (doc) doc->removeBPNode(id);
}
static void updateNodeInActive(BPClass* bc, LevelDocument* doc, const BPNode& node) {
    if (bc) {
        for (BPNode& n : bc->nodes) if (n.id == node.id) { n = node; return; }
    } else if (doc) doc->updateBPNode(node);
}
static void addConnToActive(BPClass* bc, LevelDocument* doc, const BPConnection& conn) {
    if (bc) bc->connections.append(conn);
    else if (doc) doc->addBPConnection(conn);
}
static void removeConnFromActive(BPClass* bc, LevelDocument* doc, const QString& id) {
    if (bc) { bc->connections.removeIf([&](const BPConnection& c){ return c.id == id; }); }
    else if (doc) doc->removeBPConnection(id);
}
```

然后替换所有写操作调用为对应 static helper，传入 `m_bpClass, m_doc`，末尾改为 `notifyModified()`。

- [ ] **Step 4: 在 nodeDefs() 末尾追加 Self 节点定义**

在 `nodeDefs()` 的 `defs` 初始化列表末尾（`}` 之前）加：

```cpp
        // ── Self 变换（所有 Actor）──────────────────────────────────────
        {
            "Self.GetPosition", "获取自身位置", QColor("#1a4a4a"),
            {{"x","X",false,true},{"y","Y",false,true}}
        },
        {
            "Self.SetPosition", "设置自身位置", QColor("#1a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"x","X",false,false},{"y","Y",false,false}}
        },
        {
            "Self.GetRotation", "获取自身旋转", QColor("#1a4a4a"),
            {{"angle","角度",false,true}}
        },
        {
            "Self.SetRotation", "设置自身旋转", QColor("#1a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"angle","角度",false,false}}
        },
        {
            "Self.IsActive", "获取激活状态", QColor("#1a4a4a"),
            {{"active","激活",false,true}}
        },
        {
            "Self.SetActive", "设置激活状态(Self)", QColor("#1a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"active","激活",false,false}}
        },
        {
            "Self.GetName", "获取自身名称", QColor("#1a4a4a"),
            {{"name","名称",false,true}}
        },
        // ── Self 精灵渲染器 ─────────────────────────────────────────────
        {
            "Self.Sprite.SetImage", "设置精灵图片", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"path","路径",false,false}}
        },
        {
            "Self.Sprite.SetColor", "设置精灵颜色", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"r","R",false,false},{"g","G",false,false},
             {"b","B",false,false},{"a","A",false,false}}
        },
        {
            "Self.Sprite.SetFlipX", "水平翻转(Self)", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"flip","翻转",false,false}}
        },
        {
            "Self.Sprite.SetFlipY", "垂直翻转(Self)", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"flip","翻转",false,false}}
        },
        {
            "Self.Sprite.SetVisible", "设置精灵可见", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"visible","可见",false,false}}
        },
        // ── Self 摄像机 ─────────────────────────────────────────────────
        {
            "Self.Camera.SetSize", "设置摄像机尺寸", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"size","尺寸",false,false}}
        },
        {
            "Self.Camera.SetBackground", "设置背景色(Self)", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"r","R",false,false},{"g","G",false,false},{"b","B",false,false}}
        },
```

- [ ] **Step 5: 更新右键弹窗 `showWireDropPopup` 的节点列表，加 Self 分类并应用过滤**

在 `showWireDropPopup` 中，节点列表生成处（通常是遍历 `nodeDefs()` 加入 `QTreeWidget`），加上过滤：

```cpp
// 在遍历 nodeDefs 加入分类时，对每个 def 判断：
if (!isSelfNodeVisible(def.typeId)) continue;
```

Self 节点放入独立分类 `"Self"`，与现有 Event/Action/Flow/Var 分类并列。

- [ ] **Step 6: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

- [ ] **Step 7: 提交**

```bash
git add src/editor/BlueprintEditor.h src/editor/BlueprintEditor.cpp
git commit -m "feat: BlueprintEditor 支持 bpClass 模式，新增 Self 节点"
```

---

## Task 6: ContentBrowser .bp 文件支持 + DetailsPanel 更新

**Files:**
- Modify: `src/editor/ContentBrowser.h`
- Modify: `src/editor/ContentBrowser.cpp`
- Modify: `src/editor/DetailsPanel.h`
- Modify: `src/editor/DetailsPanel.cpp`

- [ ] **Step 0: EditorWindow.h 加 `ContentBrowser* m_contentBrowser` 成员**

在 `src/editor/EditorWindow.h` 中，在 `SceneOutliner*` 等成员变量附近加：
```cpp
    ContentBrowser*  m_contentBrowser  = nullptr;
```
并在顶部加 `#include "ContentBrowser.h"`（如尚未包含）。

在 `src/editor/EditorWindow.cpp` 中，找到：
```cpp
    auto* cb = new ContentBrowser(m_project.path, cbContainer);
```
改为：
```cpp
    m_contentBrowser = new ContentBrowser(m_project.path, cbContainer);
    auto* cb = m_contentBrowser;
```

- [ ] **Step 1: 更新 ContentBrowser.h — 新增 bpClassOpenRequested 信号**

在 `signals:` 里加：
```cpp
    void bpClassOpenRequested(const QString& bpFilePath);
```

- [ ] **Step 2: 更新 ContentBrowser.cpp populateFolder — 显示 .bp 文件**

在 `populateFolder()` 中，扫描文件夹时，找到处理 `.level` 文件的 `if` 分支，在其后加同级 `else if`：

```cpp
} else if (info.suffix() == "bp") {
    m_currentEntries.append(info.absoluteFilePath());
    m_currentTypes.append("bp");
}
```

- [ ] **Step 3: 更新 ContentBrowser.cpp makeBpClassIcon + 显示图标**

在 `makeLevelIcon()` 方法实现之后，加：

```cpp
static QIcon ContentBrowser::makeBpClassIcon() {
    QPixmap px(64, 64);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(100, 60, 160));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(8, 8, 48, 48, 8, 8);
    p.setPen(Qt::white);
    QFont f; f.setPixelSize(18); f.setBold(true);
    p.setFont(f);
    p.drawText(px.rect(), Qt::AlignCenter, "BP");
    return QIcon(px);
}
```

在 `populateFolder` 或网格填充处，对 type == "bp" 的条目使用 `makeBpClassIcon()`。

在 ContentBrowser.h 的 private 区加：
```cpp
    static QIcon makeBpClassIcon();
```

- [ ] **Step 4: 更新 ContentBrowser.cpp onGridItemDoubleClicked — .bp 双击**

在 `onGridItemDoubleClicked` 中，找到处理 `.level` 的 `if` 分支，加同级 `else if`：

```cpp
} else if (type == "bp") {
    emit bpClassOpenRequested(path);
}
```

- [ ] **Step 5: 更新 ContentBrowser.cpp 右键菜单 — 新建蓝图类**

在 `showGridContextMenu` 中，找到 "新建关卡" 的 action，在其后加：

```cpp
menu.addAction("新建蓝图类", this, [this]() {
    bool ok;
    const QString name = QInputDialog::getText(this, "新建蓝图类", "蓝图类名称：",
                                                QLineEdit::Normal, "NewBlueprint", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const QString dir = m_projectRoot + "/Blueprints";
    QDir().mkpath(dir);
    const QString path = dir + "/" + name.trimmed() + ".bp";
    BPClass bc;
    bc.name     = name.trimmed();
    bc.filePath = path;
    bc.save();
    populateFolder(m_currentPath);
});
```

在 ContentBrowser.cpp 顶部加：
```cpp
#include "models/BPClass.h"
```

- [ ] **Step 6: 更新 DetailsPanel.h — 新增信号和"编辑蓝图"按钮成员**

在 `signals:` 里加：
```cpp
    void editBpClassRequested(const QString& bpClass);
```

在私有成员区加：
```cpp
    QPushButton* m_editBpBtn = nullptr;
```

- [ ] **Step 7: 更新 DetailsPanel.cpp buildHeader — 加"编辑蓝图"按钮**

在 `buildHeader` 中，row1 构建后（加入布局之后），在 `vl->addLayout(row1)` 之前加：

```cpp
    m_editBpBtn = new QPushButton("编辑蓝图", headerWrap);
    m_editBpBtn->setObjectName("editBpBtn");
    m_editBpBtn->setFixedHeight(22);
    m_editBpBtn->setVisible(false);
    connect(m_editBpBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentActor.bpClass.isEmpty() && !m_currentActor.bpClass.startsWith("builtin/"))
            emit editBpClassRequested(m_currentActor.bpClass);
    });
    row1->addWidget(m_editBpBtn);
```

在 `showActor()` 中，找到赋值控件的代码块（有 `QSignalBlocker` 的地方），在其中加：

```cpp
    m_editBpBtn->setVisible(!actor.bpClass.isEmpty() && !actor.bpClass.startsWith("builtin/"));
```

- [ ] **Step 8: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

- [ ] **Step 9: 提交**

```bash
git add src/editor/ContentBrowser.h src/editor/ContentBrowser.cpp \
        src/editor/DetailsPanel.h src/editor/DetailsPanel.cpp
git commit -m "feat: ContentBrowser 支持 .bp 文件，DetailsPanel 加编辑蓝图按钮"
```

---

## Task 7: DocTabBar .bp Tab + EditorWindow 蓝图类打开

**Files:**
- Modify: `src/editor/EditorWindow.h`（已在 Task 4 修改，此处补充）
- Modify: `src/editor/EditorWindow.cpp`

- [ ] **Step 1: 实现 EditorWindow::openBpClassTab()**

在 EditorWindow.cpp 中，找到 `openLevelTab()` 实现，在其后加：

```cpp
void EditorWindow::openBpClassTab(const QString& bpFilePath) {
    // 已打开则激活
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        if (m_docTabBar->tabData(i).toString() == bpFilePath) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }
    // 加载或复用 BPClass
    if (!m_openBpClasses.contains(bpFilePath)) {
        auto* bc = new BPClass(BPClass::load(bpFilePath));
        m_openBpClasses[bpFilePath] = bc;
    }
    int idx;
    {
        QSignalBlocker b(m_docTabBar);
        idx = m_docTabBar->addTab("  " + QFileInfo(bpFilePath).baseName());
        m_docTabBar->setTabData(idx, bpFilePath);
    }
    m_docTabBar->setCurrentIndex(idx);
}
```

- [ ] **Step 2: 更新 onTabChanged() — 识别 .bp tab**

在 `onTabChanged()` 中，在处理 `kBlueprintTabData` 的 `if` 块之后，加：

```cpp
    // .bp 蓝图类 Tab
    if (path.endsWith(".bp")) {
        if (m_centralStack->indexOf(m_bpWrapper) < 0)
            m_centralStack->addWidget(m_bpWrapper);
        m_centralStack->setCurrentWidget(m_bpWrapper);
        BPClass* bc = m_openBpClasses.value(path, nullptr);
        if (bc && m_blueprintEditor) m_blueprintEditor->loadBpClass(bc);
        return;
    }
```

- [ ] **Step 3: 更新 onTabClosed() — .bp tab 关闭处理**

在 `onTabClosed()` 中，在处理 `kBlueprintTabData` 的 `if` 块之后加：

```cpp
    if (path.endsWith(".bp")) {
        m_docTabBar->removeTab(index);
        if (m_centralStack) m_centralStack->setCurrentWidget(m_viewportPage);
        return;
    }
```

- [ ] **Step 4: 连接 ContentBrowser 和 DetailsPanel 信号**

在 EditorWindow 构造函数里，找到连接 ContentBrowser `levelOpenRequested` 信号的地方，在其后加：

```cpp
    connect(m_contentBrowser, &ContentBrowser::bpClassOpenRequested,
            this, &EditorWindow::openBpClassTab);
```

找到连接 DetailsPanel 相关信号的地方，加：

```cpp
    connect(m_detailsPanel, &DetailsPanel::editBpClassRequested,
            this, [this](const QString& bpClass) {
        openBpClassTab(m_project.path + "/" + bpClass);
    });
```

（`m_contentBrowser` 在 Task 6 Step 0 中已改为成员变量。）

- [ ] **Step 5: 编译验证**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

- [ ] **Step 6: 运行端到端验证**

```bash
open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

验证流程：
1. 打开项目，在 ContentBrowser 里找到 Blueprints/ 目录（若不存在，右键创建）
2. 右键 → 新建蓝图类，命名 "TestBP"
3. 双击 TestBP.bp，Tab 栏出现新 Tab，蓝图编辑器打开，页眉显示 "TestBP"
4. 右键画布，看到 Self 分类（当前 TestBP 无组件，只显示变换节点）
5. 放一个 Sprite Actor，在 DetailsPanel 中 bpClass 显示 `builtin/Sprite`，"编辑蓝图"按钮不可见（内置类）
6. 在 ContentBrowser 新建蓝图类 "PlayerBP"，手动将某 Actor 的 bpClass 改为 `Blueprints/PlayerBP.bp`（暂通过 JSON 编辑），在 DetailsPanel 中"编辑蓝图"按钮可见

- [ ] **Step 7: 提交**

```bash
git add src/editor/EditorWindow.h src/editor/EditorWindow.cpp
git commit -m "feat: DocTabBar 支持 .bp Tab，EditorWindow 连接蓝图类打开流程"
```
