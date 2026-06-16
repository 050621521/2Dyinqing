# 全局变量系统 设计文档

日期：2026-06-16
状态：方向已确认（照虚幻 Game Instance 思路），待评审

## 背景与动机

游戏常见需求：玩家在前置关卡做出选择（模式、角色），后续关卡按选择走不同逻辑。这需要**跨关卡持久的状态**。

现状：已有 `Var.Set/Get 数值/布尔/字符串` 节点，但其存储 `BPRuntime::m_varStore` 在**每个 BPRuntime 实例里**——一跳关卡就重建、清空。所以现有 Var 是"**本关运行内**"的临时变量，**跳关不保留**。

本子项目补上**跨关卡持久层**——即虚幻的 **Game Instance** 模式：一个在整局游戏存活、跨所有关卡加载不销毁的状态。

## 虚幻先例（设计依据）

- **Game Instance** 在整局游戏存活，跨关卡加载不销毁；玩家选择存它身上。
- 变量是**声明式 + 带类型**的：在"我的蓝图"**常驻面板**里声明（名字 + 类型），从下拉选类型，强类型防错。
- 用法：把变量拖到图里 → 生成"绑定该变量、带类型引脚"的 Get/Set 节点。

本设计照搬其精神：**常驻面板声明（名字+类型）→ 每变量各自的带类型 Get/Set 节点**；存储跨关卡持久、点运行清空。

## 目标与范围

**本子项目做：**
1. 全局变量**声明**（名字 + 类型：数值 / 布尔 / 字符串），存项目级 `project.json`。
2. 常驻「全局变量」**停靠面板**：增 / 删 / 改名 / 改类型。
3. **每变量绑定**的 `获取/设置全局变量` 节点（带类型引脚），右键菜单"全局变量"分类创建。
4. **运行时**：`EditorWindow` 持有全局变量表，跨关卡跳转保留、点 ▶ 清空；`BPRuntime` 读写它。

**本子项目不做（后续）：**
- 枚举类型变量（第②子项目；本设计的类型系统预留枚举位）。
- 结构体 / 对象引用 / 数组类型。
- 从面板拖到画布的交互（先用右键菜单创建；拖拽可后续叠加）。
- 全局变量值的存盘持久（跨游戏会话）——那是存档功能，不在此列。

## 数据模型

### 声明（项目级）

`project.json` 新增 `globalVariables` 数组：

```json
"globalVariables": [
  { "name": "模式",  "type": "string" },
  { "name": "角色",  "type": "string" },
  { "name": "分数",  "type": "number" }
]
```

类型取值：`"number"` / `"bool"` / `"string"`（后续加 `"enum:<枚举名>"`）。

代码侧用一个轻量结构承载并提供读写 + 变更信号：

```cpp
struct GlobalVarDef { QString name; QString type; };   // type: number/bool/string
// 读：从 project.json 加载；写：回存 project.json
// 变更时发信号，蓝图编辑器据此刷新菜单/重绘
```

> 归属：复用现有 `project.json` 的读写位置（`ProjectSettingsDialog` / 项目加载流程）。声明表由「全局变量」面板增删改，并通知 `EditorWindow`。

### 运行时值

`EditorWindow` 持有 `QMap<QString, QString> m_globalVars`（字符串编码值，与现有 `m_varStore` 一致的简单存法）。
- **生命周期**：点 ▶ `startRuntime()` 时 `clear()`（新的一局）；`loadLevelRequested` 跳关时**不清**（保留）；停止运行后保留至下次运行清空。
- 与现有 `m_levelNavStack`（运行时清空、跳关保留）生命周期一致，放一起管理。

## 节点设计（每变量绑定、带类型）

两个节点类型，实例通过 `params["varName"]` 绑定到具体变量：

| 类型 | 显示名 | 引脚（由变量声明类型决定） |
|---|---|---|
| `Global.Get` | `获取 <变量名>` | 一个值输出，kind = 变量类型 |
| `Global.Set` | `设置 <变量名>` | exec_in + exec_out + 一个值输入，kind = 变量类型 |

- `effectivePins` 对 `Global.Get/Set`：读 `params["varName"]` → 查项目声明的类型 → 算值引脚的 `kind`（number/string→Text 打字，bool→勾选框，enum→后续下拉）。
- `drawNode` 标题：`获取/设置 ` + `varName`（参照 `Macro::` 调用节点的动态标题做法）。
- 变量被删除或改类型：实例引脚按当前声明重算；标题对已删变量显示"(未定义)"提示，连线按 key 稳定保留/清理。

### 创建

右键"添加节点"菜单新增「**全局变量**」分类，遍历项目声明，为每个变量加 `获取 <名>` / `设置 <名>` 两项；点击创建对应节点并写入 `params["varName"]`。

## 运行时（BPRuntime）

- `EditorWindow` 创建每个 `BPRuntime` 时传入全局变量表指针：`BPRuntime::setGlobalVars(QMap<QString,QString>* g)`。跨关卡跳转重建 `BPRuntime`，但表由 `EditorWindow` 持有、指针不变、值保留。
- `executeNode`：`Global.Set` → `(*m_globalVars)[varName] = resolveDataPin(nodeId, "value")`，返回 `exec_out`。
- `resolveOutputPin`：`Global.Get` → 返回 `(*m_globalVars).value(varName)`。
- 分支控制的"值"/"比较值"即可连「获取 全局变量」实现按选择分流。

## 管理面板（GlobalVarPanel）

新增停靠面板 `GlobalVarPanel`（接入现有 QtADS 停靠系统，与 大纲/细节/内容浏览器 同级）：

- 一张表：每行 **变量名（可改）+ 类型下拉（数值/布尔/字符串）**，底部 `＋ 添加` / 选中行 `－ 删除`。
- 改动即时写回 `project.json` 并发 `变更` 信号 → `EditorWindow` 通知 `BlueprintEditor` 刷新右键菜单、重绘节点。
- 名字唯一性校验；改名时同步更新引用该变量的节点 `params["varName"]`（避免悬空引用），或保留旧名让节点显示"(未定义)"由用户处理 —— 取**同步改名**，更不易出错。

## 影响文件

- 新增 `launcher/src/editor/GlobalVarPanel.h/.cpp`：停靠面板 + 声明表编辑。
- `project.json` 读写：新增 `globalVariables` 字段（在现有项目读写处扩展）。
- `launcher/src/editor/BlueprintEditor.*`：`Global.Get/Set` 节点定义占位 + `effectivePins` + 动态标题 + 右键"全局变量"菜单；需要拿到项目声明（经 `setProjectRoot` 或新增 setter）。
- `launcher/src/editor/BPRuntime.*`：`setGlobalVars` 指针 + `Global.Set/Get` 执行。
- `launcher/src/editor/EditorWindow.*`：持有 `m_globalVars`（运行清空、跳关保留）+ 传给 `BPRuntime` + 承载 `GlobalVarPanel` + 转发声明变更。
- `CLAUDE.md`：术语 `Global.Get → 获取全局变量`、`Global.Set → 设置全局变量`。
- `CMakeLists.txt`：登记新文件。

## 验证

1. 全局变量面板增/删/改名/改类型 → `project.json` 正确读写，重开工程还原。
2. 右键"全局变量"菜单按声明列出 获取/设置；放置后标题与引脚类型正确。
3. 改变量类型 → 已放置节点引脚类型同步变化；改名 → 节点绑定同步。
4. 关卡A `设置 模式 = 单人` → 跳关卡B → `获取 模式` 读到"单人"（跨关卡保留）。
5. 点 ▶ 重新运行 → 全局变量被清空（新一局）。
6. 分支控制"值"连「获取 模式」→ 运行按全局值正确分流。

## 后续子项目（依赖本系统）

- **枚举系统**：内容浏览器 `.enum` 资产 + 枚举编辑器；全局变量类型支持 `enum:<名>`；获取/设置与分支控制比较值变为枚举下拉防错。
- **函数式封装**：独立作用域、单返回、局部变量（与全局变量互补）。
