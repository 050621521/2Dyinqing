# Tick 循环与组件运行时 — 设计文档

**日期**：2026-06-11  
**范围**：`BPRuntime.h` / `BPRuntime.cpp`（其余文件不动）

---

## 背景

现有 `BPRuntime` 只响应两个离散事件：`BeginPlay` 和 `KeyDown`。没有连续帧循环，导致：

- 摄像机跟随（`跟随控制组件`）和边界限制（`边界限制组件`）组件的数据存在但完全不运行
- 蓝图无法做持续逻辑（持续移动、计时等）
- 游戏视图本质是静态状态机，而非真正的游戏模拟

---

## 目标

1. 在 `BPRuntime` 内加入 60fps Tick 循环
2. 每帧执行：跟随控制组件 → 边界限制组件 → 蓝图 Event.Tick 节点
3. 与 UE 架构对齐：World（BPRuntime）驱动循环，组件在循环内执行

---

## 架构

```
BPRuntime 构造
  → QTimer (16ms) start
  → QElapsedTimer start

每帧 tick()
  ├── 计算 deltaTime（秒，用 QElapsedTimer）
  ├── tickComponents(deltaTime)
  │     ├── 找所有挂"跟随控制组件"的 Actor
  │     │     → lerp 摄像机向目标移动
  │     └── 找所有挂"边界限制组件"且 confinerEnabled=true 的 Actor
  │           → clamp 摄像机位置
  ├── triggerTick(deltaTime)
  │     → 执行所有 Event.Tick 蓝图节点链
  └── emit stateChanged()
        → EditorWindow → GameViewport 重绘
```

---

## 第一节：Tick 循环

**新增成员（BPRuntime.h）**

```cpp
private:
    QTimer*        m_tickTimer   = nullptr;
    QElapsedTimer  m_elapsedTimer;

private slots:
    void tick();
```

**构造函数末尾**

```cpp
m_tickTimer = new QTimer(this);
m_tickTimer->setInterval(16);
connect(m_tickTimer, &QTimer::timeout, this, &BPRuntime::tick);
m_tickTimer->start();
m_elapsedTimer.start();
```

**tick() 实现**

```cpp
void BPRuntime::tick() {
    const float dt = m_elapsedTimer.restart() / 1000.0f;
    tickComponents(dt);
    triggerTick(dt);
    emit stateChanged();
}
```

析构时 `QTimer` 随 QObject 树自动销毁，无需手动 stop。

---

## 第二节：跟随控制组件（tickComponents — 跟随部分）

**行为**：找到挂了"跟随控制组件"的 Actor（即摄像机），每帧向目标插值。

**逻辑**

```cpp
for (ActorData& a : m_actors) {
    if (!a.components.contains("跟随控制组件")) continue;
    if (a.followTarget.isEmpty()) continue;

    // 找目标 Actor（按 name 匹配）
    const ActorData* target = findActorByName(a.followTarget);
    if (!target) continue;

    float destX = target->x + a.followOffsetX;
    float destY = target->y + a.followOffsetY;
    float t = 1.0f - std::exp(-a.followLerpSpeed * dt);  // 指数平滑，帧率无关
    a.x += (destX - a.x) * t;
    a.y += (destY - a.y) * t;
}
```

**指数平滑**（`1 - exp(-speed * dt)`）比线性 lerp 帧率无关，`speed=5` 时约 0.2 秒追上，与 `followLerpSpeed` 语义一致。

**新增辅助方法**

```cpp
const ActorData* BPRuntime::findActorByName(const QString& name) const {
    for (const ActorData& a : m_actors)
        if (a.name == name) return &a;
    return nullptr;
}
```

---

## 第三节：边界限制组件（tickComponents — 边界部分）

**行为**：跟随插值之后，clamp 摄像机位置，保证拍摄范围不超出设定边界。

**顺序**：先跑跟随，再 clamp，和 UE 一致。

**逻辑**

```cpp
for (ActorData& a : m_actors) {
    if (!a.components.contains("边界限制组件")) continue;
    if (!a.confinerEnabled) continue;

    a.x = std::clamp(a.x, a.confinerMinX, a.confinerMaxX);
    a.y = std::clamp(a.y, a.confinerMinY, a.confinerMaxY);
}
```

`std::clamp` 需要 `#include <algorithm>`（C++17，已满足项目要求）。

---

## 第四节：蓝图 Event.Tick 节点

**triggerTick(float dt)**：和 `triggerBeginPlay` 完全对称，遍历节点找 `Event.Tick` 类型，执行其执行链。

```cpp
void BPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;  // 暂存，供 resolveOutputPin 读取
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.Tick") {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
}
```

**DeltaTime 引脚**：`Event.Tick` 节点输出一个 `delta_time` 数据引脚，在 `resolveOutputPin` 中处理：

```cpp
if (node->type == "Event.Tick")
    return (pinKey == "delta_time") ? QString::number(m_deltaTick) : QString();
```

**新增成员**

```cpp
float m_deltaTick = 0.0f;
```

---

## 执行顺序（每帧）

```
tick()
  1. tickComponents(dt)
       a. 跟随控制组件：lerp 摄像机
       b. 边界限制组件：clamp 摄像机
  2. triggerTick(dt)
       → Event.Tick 蓝图节点链
  3. emit stateChanged()
```

组件先于蓝图运行，确保蓝图 Tick 读到的摄像机位置已经是更新后的值。

---

## 不在本次范围内

- 蓝图编辑器 UI 上添加 `Event.Tick` 节点（UI 改动留后续）
- 每个 Actor 独立的 tick 开关（UE 的 `SetActorTickEnabled`）
- 物理模拟、碰撞
- 精灵动画帧播放
