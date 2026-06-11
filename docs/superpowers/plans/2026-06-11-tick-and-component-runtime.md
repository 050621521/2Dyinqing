# Tick 循环与组件运行时 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 BPRuntime 内加入 60fps Tick 循环，每帧执行摄像机跟随插值、边界限制 clamp、蓝图 Event.Tick 节点。

**Architecture:** `QTimer`（16ms）驱动 `tick()` 槽，`tickComponents()` 按两次遍历处理跟随→边界，`triggerTick()` 执行蓝图节点链，最后 `emit stateChanged()` 触发 GameViewport 重绘。全部改动集中在 `BPRuntime.h` 和 `BPRuntime.cpp`，其余文件不动。

**Tech Stack:** C++17, Qt6（QTimer, QElapsedTimer）

---

## 涉及文件

| 文件 | 操作 |
|------|------|
| `launcher/src/editor/BPRuntime.h` | 修改：新增 include、成员变量、私有方法声明 |
| `launcher/src/editor/BPRuntime.cpp` | 修改：构造函数、新增 tick() / tickComponents() / triggerTick() / findActorByName()，更新 resolveOutputPin() |

---

## Task 1：更新 BPRuntime.h

**Files:**
- Modify: `launcher/src/editor/BPRuntime.h`

- [ ] **Step 1: 将 BPRuntime.h 替换为以下完整内容**

```cpp
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
};
```

- [ ] **Step 2: 确认编译通过（只改了头文件，先做一次快速检查）**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E "error:|warning:|Built target"
```

预期：提示 `BPRuntime.cpp` 缺少新方法定义的链接错误或编译错误——这是正常的，说明头文件被识别到了。

---

## Task 2：在构造函数中启动 QTimer

**Files:**
- Modify: `launcher/src/editor/BPRuntime.cpp`

- [ ] **Step 1: 在 BPRuntime.cpp 顶部添加 `#include <cmath>` 和 `#include <algorithm>`**

在现有 `#include "BPRuntime.h"` 和 `#include <QSet>` 之后加：

```cpp
#include <algorithm>
#include <cmath>
```

- [ ] **Step 2: 在构造函数末尾启动计时器**

找到构造函数（当前最后一行是 `m_actors = doc->actors();`），在其之后追加：

```cpp
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(16);
    connect(m_tickTimer, &QTimer::timeout, this, &BPRuntime::tick);
    m_tickTimer->start();
    m_elapsedTimer.start();
```

完整构造函数结果：

```cpp
BPRuntime::BPRuntime(const LevelDocument* doc, QObject* parent)
    : QObject(parent)
{
    if (!doc) return;
    m_nodes       = doc->bpNodes();
    m_connections = doc->bpConnections();
    m_actors      = doc->actors();

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(16);
    connect(m_tickTimer, &QTimer::timeout, this, &BPRuntime::tick);
    m_tickTimer->start();
    m_elapsedTimer.start();
}
```

---

## Task 3：实现 tick() 槽

**Files:**
- Modify: `launcher/src/editor/BPRuntime.cpp`

- [ ] **Step 1: 在 `triggerBeginPlay()` 之前插入 tick() 实现**

```cpp
void BPRuntime::tick() {
    const float dt = m_elapsedTimer.restart() / 1000.0f;
    tickComponents(dt);
    triggerTick(dt);
    emit stateChanged();
}
```

---

## Task 4：实现 tickComponents()

**Files:**
- Modify: `launcher/src/editor/BPRuntime.cpp`

- [ ] **Step 1: 在 tick() 之后插入 tickComponents() 实现**

```cpp
void BPRuntime::tickComponents(float dt) {
    // Pass 1：跟随控制组件 — 摄像机向目标插值
    for (ActorData& a : m_actors) {
        if (!a.components.contains("跟随控制组件")) continue;
        if (a.followTarget.isEmpty()) continue;
        const ActorData* target = findActorByName(a.followTarget);
        if (!target) continue;

        const float destX = target->x + a.followOffsetX;
        const float destY = target->y + a.followOffsetY;
        const float t     = 1.0f - std::exp(-a.followLerpSpeed * dt);
        a.x += (destX - a.x) * t;
        a.y += (destY - a.y) * t;
    }

    // Pass 2：边界限制组件 — clamp 摄像机位置
    for (ActorData& a : m_actors) {
        if (!a.components.contains("边界限制组件")) continue;
        if (!a.confinerEnabled) continue;

        a.x = std::clamp(a.x, a.confinerMinX, a.confinerMaxX);
        a.y = std::clamp(a.y, a.confinerMinY, a.confinerMaxY);
    }
}
```

---

## Task 5：实现 triggerTick() 和 findActorByName()

**Files:**
- Modify: `launcher/src/editor/BPRuntime.cpp`

- [ ] **Step 1: 在 tickComponents() 之后插入 triggerTick() 实现**

```cpp
void BPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.Tick") {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}
```

- [ ] **Step 2: 在文件末尾插入 findActorByName() 实现**

```cpp
const ActorData* BPRuntime::findActorByName(const QString& name) const {
    for (const ActorData& a : m_actors)
        if (a.name == name) return &a;
    return nullptr;
}
```

- [ ] **Step 3: 在 resolveOutputPin() 中添加 Event.Tick 的 delta_time 引脚**

找到现有 `resolveOutputPin` 函数，在 `return {};` 之前插入：

```cpp
    if (node->type == "Event.Tick")
        return (pinKey == "delta_time") ? QString::number(m_deltaTick) : QString();
```

完整 `resolveOutputPin` 结果：

```cpp
QString BPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    if (node->type == "Var.ActorRef")
        return node->params.value("actorId");

    if (node->type == "Var.GetActorPos") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        for (const ActorData& a : m_actors) {
            if (a.id == actorId) {
                if (pinKey == "x") return QString::number(a.x);
                if (pinKey == "y") return QString::number(a.y);
            }
        }
    }

    if (node->type == "Event.Tick")
        return (pinKey == "delta_time") ? QString::number(m_deltaTick) : QString();

    return {};
}
```

---

## Task 6：编译、运行、验证

- [ ] **Step 1: 完整编译**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -10
```

预期末行：`[100%] Built target launcher`，无 error。

- [ ] **Step 2: 启动应用**

```bash
open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

- [ ] **Step 3: 验证跟随控制组件**

1. 打开有"主摄像头"Actor（挂了"跟随控制组件"）的关卡
2. 在细节面板将"目标"填写为场景中某个 Sprite Actor 的名称
3. 点击 ▶ 运行
4. 观察游戏视图：摄像机应平滑移动到目标 Actor 位置附近
5. 蓝图触发 `Action.MoveActor` 移动目标 Actor，摄像机应跟随

- [ ] **Step 4: 验证边界限制组件**

1. 在摄像机 Actor 上勾选"边界限制组件"的"启用"
2. 设置 X/Y 范围为较小的值（如 -100 到 100）
3. 点击 ▶ 运行，把目标 Actor 移出边界
4. 观察游戏视图：摄像机应被限制在边界内，不会超出

- [ ] **Step 5: 提交**

```bash
cd /Users/kwy/Documents/2Dyinqing && git add launcher/src/editor/BPRuntime.h launcher/src/editor/BPRuntime.cpp && git commit -m "feat: BPRuntime 加入 60fps Tick 循环，运行摄像机跟随和边界限制组件"
```
