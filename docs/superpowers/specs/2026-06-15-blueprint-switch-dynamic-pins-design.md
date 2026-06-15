# 分支控制（Switch）+ 动态引脚框架 设计文档

日期：2026-06-15
状态：已确认方向（按虚幻方案），待评审

## 背景与动机

游戏常见需求：玩家在前置关卡做出选择（模式、角色），后续关卡按选择走不同逻辑。其中"按一个值分多路"的节点，就是虚幻里的 **Switch**。

本项目当前只有「条件分支」（`Flow.Branch`，二选一 真/假）。要表达"战士/法师/弓箭手 各走一路"，需要一个**多路分支**节点——用户称之为「**分支控制**」。

关键洞察（来自讨论）：分支控制是"**框架可复用、每个实例的分支各不相同**"的节点。这要求蓝图节点的引脚**不能再写死**，必须能按实例配置动态增减。

## 虚幻先例（设计依据）

虚幻的 Switch 家族（`Switch on String / Int / Enum / Name`）全部继承自同一抽象基类 `UK2Node_Switch`：

- **基类 = 通用框架**：拿"选择值" → 逐个和每个 case 全等比较 → 相等走该出口 → 都不匹配走 Default。
- **子类 = 出口来源可插拔**：`SwitchString` 出口来自用户自定义的字符串数组；`SwitchEnum` 出口来自绑定的枚举。
- 引脚动态生成（`ReconstructNode` 按引脚名重建并尽量保住连线）。

本设计照搬此结构：先做**通用分支控制框架 + 自定义模式（≈Switch on String）**；枚举模式、全局变量作为后续子项目叠加。

## 目标与范围

**本子项目做：**
1. 给蓝图节点系统引入**动态引脚**能力（引脚按实例配置计算，而非静态固定）。
2. 新增「分支控制」节点（`Flow.Switch`），**自定义模式**：手动＋加分支、每个分支填一个比较值、可选 Default 兜底出口。
3. 运行时执行语义：值全等某分支 → 走该出口；都不匹配 → 走 Default（若开）或不执行。

**本子项目不做（后续子项目）：**
- 枚举资产系统（枚举模式：绑枚举自动生成出口、填值下拉防错）。
- 全局变量系统（设置/获取全局变量节点）。
- 编译期 ExpandNode 式展开（我们运行时是解释执行，直接在 `BPRuntime` 解释即可，无需展开）。

## 核心架构：动态引脚抽象

### 现状

所有引脚相关逻辑都直接读静态定义 `findNodeDef(type)->pins`，共约 9 处使用点：
`nodeHeight` / `pinCount` / `pinCenter` / `drawNode` / `hitTest`（含 PinValue 区）/ 连线 / `showInlineEdit` / 右键创建菜单等。

### 改动

引入一个统一入口，**按节点实例**返回其实际引脚列表：

```cpp
// 返回该节点实例当前的有效引脚（静态节点 = def->pins；动态节点按 params 计算）
QList<PinDef> BlueprintEditor::effectivePins(const BPNode& node) const;
```

- 对普通节点：直接返回 `findNodeDef(node.type)->pins`（行为不变）。
- 对 `Flow.Switch`：返回 `[exec_in, value(数据输入)] + 每个分支一个 exec 输出 + (可选)Default 输出`。

所有遍历 `def->pins` 的地方改为遍历 `effectivePins(node)`。这是本设计的**唯一结构性改动**，其余逻辑（绘制/命中/连线/几何）保持原样，只是引脚来源变了。

> 运行时侧（`BPRuntime`）同样需要一个等价的"按节点算引脚/算出口"逻辑，但 `BPRuntime` 的执行只关心"从某出口找连线"，所以只需在 `executeNode` 里专门处理 `Flow.Switch` 即可，不必抽象 effectivePins。

## 数据模型：分支配置存哪

复用 `BPNode.params`（`QMap<QString,QString>`），不改 `BPNode` 结构、不动持久化格式：

| params 键 | 含义 | 示例 |
|---|---|---|
| `branches` | 分支列表（JSON 字符串，有序） | `[{"id":"b1","value":"战士"},{"id":"b2","value":"法师"}]` |
| `hasDefault` | 是否启用 Default 出口 | `"true"` / `"false"` |

**关键：分支用稳定 `id`，引脚 key = `case_<id>`**（不是按下标 `case_0`）。这样删除中间分支时，其余分支的引脚 key 不变，已连的线不会错位丢失。Default 出口 key 固定为 `default`。

`value` 是该分支的比较值（自定义字符串；将来枚举模式下变为从枚举下拉选）。

## 节点定义（框架层）

`Flow.Switch`「分支控制」，header 颜色沿用青色系（如 `#2a6a6a`）。`effectivePins` 计算结果：

| 引脚 key | label | exec? | output? | 说明 |
|---|---|---|---|---|
| `exec_in` | exec | 是 | 否 | 触发判断 |
| `value` | 值 | 否 | 否 | 要比较的数据输入（数据输入，可连线或内联填） |
| `case_<id>` | `= 战士` 等 | 是 | 是 | 每个分支一个出口，label 显示其比较值 |
| `default` | 默认 | 是 | 是 | hasDefault 时存在，兜底 |

`nodeDefs()` 里仍登记一个 `Flow.Switch` 占位定义（供右键菜单创建、新建时给默认 1~2 个分支），但其引脚以 `effectivePins` 动态结果为准。

## 编辑交互

在「分支控制」节点上提供：

1. **＋加分支**：节点底部一个 `＋` 区域，点击向 `branches` 追加一项（生成新 `id`，`value` 默认空，待编辑），重绘后多一个出口。
2. **编辑分支值**：点分支行的值区域 → 原地 `QLineEdit`（复用现有内联编辑），写回该分支的 `value`。
3. **删分支**：分支行末尾一个 `×`（或右键菜单"删除此分支"）→ 从 `branches` 移除该 `id`，并**连带删除连到 `case_<id>` 的连线**。
4. **Default 开关**：节点上一个小复选框切换 `hasDefault`；关闭时若 `default` 出口有连线，一并删除。

所有改动走现有 Undo 栈（`BPConnectionAddCmd` 等同类命令），与现有节点编辑一致并可撤销。

### 连线保留

改配置触发重绘即可——因为引脚 key 稳定（`case_<id>`），`drawConnections`/`hitTest`/运行时都按 key 找引脚，增删其他分支不影响已连分支。仅"删某分支/关 Default"时主动清理指向该 key 的连线。

## 运行时语义（BPRuntime）

`executeNode` 增加分支：

```cpp
if (node->type == "Flow.Switch") {
    const QString v = resolveDataPin(nodeId, "value");   // 取"值"输入
    // 解析 params["branches"]，逐个全等比较
    for (每个分支 b : branches)
        if (v == b.value) return "case_" + b.id;         // 走匹配出口
    if (params["hasDefault"] == "true") return "default";
    return {};                                            // 不匹配且无 Default：不继续
}
```

`executeChain` 现有逻辑已支持"从返回的出口 key 继续往下找连线"，无需改动。`value` 输入沿用 `resolveDataPin`（可连线传入，或内联值）。

> 注意：`executeChain` 对每个出口只跟随**一根**线（现有 `break`），与 exec 输出"单连"约束一致；分支控制的每个出口本就单连，契合。

## 影响文件

- `launcher/src/editor/BlueprintEditor.h`：声明 `effectivePins`、分支增删/Default 相关方法。
- `launcher/src/editor/BlueprintEditor.cpp`：
  - 新增 `effectivePins(node)`，将约 9 处 `def->pins` 改为按实例取引脚；
  - `nodeDefs()` 登记 `Flow.Switch` 占位；
  - `drawNode` 增加 ＋加分支 / 分支值 / × / Default 复选框 的绘制；
  - `hitTest` 增加这些可点区域；
  - 分支增删 / 值编辑 / Default 切换的处理与 Undo。
- `launcher/src/editor/BPRuntime.cpp`：`executeNode` 处理 `Flow.Switch`。
- `CLAUDE.md`：术语表登记 `Flow.Switch → 分支控制`。

## 验证

1. 蓝图里新建「分支控制」，默认有 1~2 个分支。
2. ＋加分支 → 多一个出口；编辑分支值显示为 `= 值`；× 删分支 → 出口消失且连线清理。
3. 开/关 Default → `默认` 出口出现/消失。
4. 连线：给 `值` 接一个数据源、各出口接不同逻辑节点；删中间分支，其余连线不错位。
5. 运行：`值` 等于某分支 → 只走该出口；都不等 → 走 Default（开）或不动（关）。
6. 保存/重开关卡，分支配置与连线正确还原。

## 后续子项目（依赖本框架）

- **全局变量系统**：设置/获取全局变量节点，全局表存 `EditorWindow`（跨关卡持久、点运行清空）。分支控制的 `值` 由"获取全局变量"连入。
- **枚举资产系统**：内容浏览器里的 `.enum` 资产 + 枚举编辑器；分支控制增加"枚举模式"（绑枚举、出口自动、填值下拉防错），全局变量可声明为枚举类型。
