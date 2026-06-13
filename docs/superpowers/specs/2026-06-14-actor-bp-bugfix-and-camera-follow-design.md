# Actor 蓝图 Bug 修复 + 摄像机跟随/边界功能设计

**日期：** 2026-06-14  
**状态：** 待实现

---

## 一、Actor 蓝图 Bug 修复（5 个）

### Bug A — 「打印字符串」节点在 Actor 蓝图中无效

**位置：** `ActorBPRuntime.cpp:66-68`

**问题：** `Action.Print` 只返回 `"exec_out"`，没有任何输出。

**修复：** `ActorBPRuntime` 新增信号 `printOutput(QString text)`，`Action.Print` 执行时 `emit printOutput(text)`。`BPRuntime` 在创建每个 `ActorBPRuntime` 实例时连接该信号，追加到自己的 `m_printLog`，与关卡蓝图的打印日志合并，由 `GameViewport` 统一显示。

**对比参考：** `BPRuntime.cpp:125-127`

---

### Bug B — 「设置精灵可见」与「设置激活」行为完全相同

**位置：** `ActorBPRuntime.cpp:117-121`

**问题：** `Self.Sprite.SetVisible` 写的是 `self->active`，与 `Self.SetActive` 完全一致，导致两个节点行为相同。

**修复：**
1. `ActorData` 新增字段 `bool spriteVisible = true`，序列化到 `.level` JSON。
2. `Self.Sprite.SetVisible` 改为写 `self->spriteVisible`。
3. `GameViewport` 渲染精灵时条件改为 `active && spriteVisible`。
4. `Viewport2D`（编辑器视口）同步处理，保持一致。

---

### Bug C — UI 按钮/下拉事件在 Actor 蓝图中触发不了

**位置：** `ActorBPRuntime.cpp:256-262`

**问题：** `triggerButtonClick` 用 `refUi == instanceId` 匹配，但 `UI.Ref` 节点输出的是 `uiName::widgetName`，`refUi` 实际上是 `uiName`，而 `instanceId` 是实例 ID，两者格式不同，导致永远匹配失败。

**修复：** 对齐关卡蓝图 `BPRuntime.cpp:366` 的做法，加 `|| refUi == uiName` 兜底：

```cpp
if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName)
```

`triggerDropdownChanged` 同理。

---

### Bug D — Actor 蓝图修改后关闭窗口不提示保存

**位置：** `EditorWindow.cpp:840-856`（closeEvent），`EditorWindow.cpp:191`（信号连接）

**问题：** `closeEvent` 只检查关卡文档脏状态；`bpClassModified` 信号未被连接，`m_openBpClasses` 没有脏标记机制。

**修复：**
1. `BPClass` 新增 `bool dirty` 字段（或在 `EditorWindow` 用 `QSet<QString> m_dirtyBpClasses` 记录路径）。
2. 连接 `BlueprintEditor::bpClassModified` 信号，将对应 `.bp` 路径加入脏集合。
3. `closeEvent` 在遍历关卡之后，继续遍历 `m_openBpClasses`，对脏的 `.bp` 文件弹确认框（保存 / 丢弃 / 取消），取消时 `e->ignore()` 并立即返回。

---

### Bug E — 按钮/下拉事件执行链缺少循环保护

**位置：** `ActorBPRuntime.cpp:261, 272`

**问题：** `triggerButtonClick` 和 `triggerDropdownChanged` 调用 `executeChain` 时未传 `visited` 集合，若节点连线构成环路会无限递归导致栈溢出。

**修复：** 在两个方法内部创建局部 `QSet<QString> visited`，传入 `executeChain`：

```cpp
QSet<QString> visited;
executeChain(node.id, "exec_out", &visited);
```

---

## 二、摄像机跟随/边界功能

### 2.1 数据层（ActorData 新增字段）

仅对 `type == "Camera"` 的 Actor 生效，其他类型忽略。

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `cameraFollowTarget` | `QString` | `""` | 跟随目标 Actor 名称，空字符串表示不跟随 |
| `cameraFollowOffsetX` | `float` | `0.0` | 跟随偏移 X（世界坐标） |
| `cameraFollowOffsetY` | `float` | `0.0` | 跟随偏移 Y（世界坐标） |
| `cameraSmooth` | `float` | `1.0` | 平滑插值系数，1.0 = 瞬时，0.05 = 慢速平滑 |
| `cameraBoundaryActor` | `QString` | `""` | 边界来源：绑定 Trigger Actor 名称（优先于手填） |
| `cameraBoundMinX` | `float` | `0.0` | 手填边界：左（世界坐标） |
| `cameraBoundMaxX` | `float` | `0.0` | 手填边界：右 |
| `cameraBoundMinY` | `float` | `0.0` | 手填边界：上 |
| `cameraBoundMaxY` | `float` | `0.0` | 手填边界：下 |

字段仅在不为默认值时写入 JSON，保持 `.level` 文件简洁。

---

### 2.2 细节面板新增区块「跟随控制」

仅当选中 Actor 类型为摄像机时显示此区块，折叠标题为「跟随控制」。

**控件列表：**
- **跟随目标**：`QLineEdit`（手填 Actor 名称）+ 未来可扩展为下拉
- **偏移 X / 偏移 Y**：两个 `QDoubleSpinBox`，范围 `-9999 ~ 9999`
- **平滑度**：`QDoubleSpinBox`，范围 `0.01 ~ 1.0`，步长 `0.05`；旁边标注「1.0 = 瞬时」
- **边界来源**：`QComboBox`（无边界 / 手填数值 / 绑定 Trigger Actor）
  - 选「手填数值」时显示 4 个 `QDoubleSpinBox`（左/右/上/下）
  - 选「绑定 Trigger Actor」时显示一个 `QLineEdit`（填 Trigger Actor 名称）

---

### 2.3 新增蓝图节点

节点定义加入 `BlueprintEditor::nodeDefs()` 静态列表，Header 颜色与摄像机现有节点一致。

| 中文名 | typeId | 引脚 |
|---|---|---|
| 设置跟随目标 | `Self.Camera.SetFollow` | exec_in → exec_out；data: `target`（Actor名称） |
| 设置跟随偏移 | `Self.Camera.SetFollowOffset` | exec_in → exec_out；data: `x`, `y` |
| 设置跟随平滑度 | `Self.Camera.SetSmooth` | exec_in → exec_out；data: `smooth`（0.01~1.0） |
| 设置摄像机边界 | `Self.Camera.SetBoundary` | exec_in → exec_out；data: `minX`, `maxX`, `minY`, `maxY` |
| 清除跟随目标 | `Self.Camera.ClearFollow` | exec_in → exec_out |
| 清除摄像机边界 | `Self.Camera.ClearBoundary` | exec_in → exec_out |

以上节点在 `ActorBPRuntime::executeNode` 中实现，直接修改 `self` 的对应字段。

---

### 2.4 运行时逻辑

在 `BPRuntime::tick()` 末尾（所有 Actor BP 执行完毕后），对主摄像机执行跟随和边界计算：

```
1. 找到 cameraIsMain == true 的摄像机 Actor（camActor）
2. 若 camActor.cameraFollowTarget 非空：
     找到目标 Actor（targetActor）
     targetX = targetActor.x + camActor.cameraFollowOffsetX
     targetY = targetActor.y + camActor.cameraFollowOffsetY
     alpha   = camActor.cameraSmooth（每帧 Lerp 系数）
     camActor.x = camActor.x + (targetX - camActor.x) * alpha
     camActor.y = camActor.y + (targetY - camActor.y) * alpha
3. 计算有效边界矩形：
     若 cameraBoundaryActor 非空：从场景中找到该 Trigger Actor，
         用其 (x, y, colliderW, colliderH) 推算 minX/maxX/minY/maxY
     否则：使用 cameraBoundMinX/MaxX/MinY/MaxY（若四个值均为 0 则无边界）
4. 若有效边界存在：
     halfW = cameraSize * aspectRatio / 2
     halfH = cameraSize / 2
     camActor.x = clamp(camActor.x, minX + halfW, maxX - halfW)
     camActor.y = clamp(camActor.y, minY + halfH, maxY - halfH)
5. 通过 stateChanged 信号通知 GameViewport 使用更新后的摄像机位置渲染
```

---

### 2.5 影响范围汇总

| 文件 | 改动内容 |
|---|---|
| `ActorData` (模型) | 新增 9 个摄像机跟随/边界字段 |
| `LevelDocument` | JSON 序列化/反序列化新字段 |
| `DetailsPanel` | 摄像机区块新增「跟随控制」折叠区块 |
| `BlueprintEditor` | nodeDefs() 新增 6 个节点 |
| `ActorBPRuntime` | executeNode 新增 6 个节点处理 |
| `BPRuntime` | tick() 末尾新增摄像机跟随/边界逻辑 |
| `GameViewport` | 渲染时使用摄像机 Actor 的 x/y 作为视口中心 |
| `Viewport2D` | 渲染时同步处理 spriteVisible 字段（Bug B） |
