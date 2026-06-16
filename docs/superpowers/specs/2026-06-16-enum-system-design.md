# 枚举系统 设计文档

日期：2026-06-16
状态：方向已确认（含分支控制枚举模式），待评审

## 背景与动机

全局变量目前是 数值/布尔/字符串 自由值，分支控制比较值靠打字 —— 容易打错、没约束。**枚举**给"一组固定选项"（模式=[单人,双人]、角色=[战士,法师,弓箭手]）提供**下拉选择、防打错**，并让"按枚举分流"成为闭环。

依赖：建立在已完成的[全局变量系统]之上（`2026-06-16-global-variables-design.md`）。

## 虚幻先例

- 枚举是资产（Blueprint Enumeration），有名字 + 一组命名项。
- 变量/引脚可声明为枚举类型 → 值从**下拉**选，类型安全。
- **Switch on Enum**：绑定一个枚举，**自动为每个枚举值生成一个出口**，比较值即枚举项。

本设计照搬：枚举声明 + 枚举类型的全局变量（值下拉）+ 分支控制"按枚举"模式（比较值下拉 + 一键生成分支）。

## 目标与范围（含 A：分支枚举模式）

1. **枚举声明**：名字 + 一组选项，存项目级 `project.json` 的 `enums`。
2. **管理 UI**：在现有「全局变量」面板加「枚举」分页，增删枚举 / 编辑其选项。
3. **全局变量枚举类型**：类型可选"枚举(<名>)"；其 `获取/设置` 值引脚变为**该枚举选项下拉**。
4. **分支控制枚举模式**：右键"按枚举…"绑定一个枚举 → 各分支比较值变枚举下拉；可"一键为每个枚举值生成分支"。

**不做（后续）：**
- 枚举作为独立 `.enum` 内容浏览器资产（先存 project.json，够用）。
- 枚举在局部 Var 节点上的支持（聚焦全局变量 + 分支）。

## 数据模型

`project.json` 新增 `enums`：

```json
"enums": [
  { "name": "模式", "values": ["单人", "双人"] },
  { "name": "角色", "values": ["战士", "法师", "弓箭手"] }
]
```

代码侧（扩展现有 `GlobalVars` 模块，同管 project.json 声明）：

```cpp
struct EnumDef { QString name; QStringList values; };
namespace Enums {
    QList<EnumDef> load(const QString& projectRoot);
    bool save(const QString& projectRoot, const QList<EnumDef>& enums);  // read-modify-write
}
```

全局变量类型字符串扩展：`"enum:<枚举名>"`（`GlobalVars::typeLabel` 已支持 `enum:` 前缀显示）。

## 引脚值类型：新增 EnumRef

`BlueprintEditor::ValueKind` 增加 `EnumRef`。
- 携带"是枚举下拉"的信息，但**不带枚举名**（名字按节点实例推导）。
- `drawNode` 值框对 `EnumRef` 显示 `▾` 下拉提示（同 LevelRef/ActorRef）。
- 点击值框 → `showListPicker` 弹出该引脚对应枚举的选项列表。

### 按节点推导枚举选项

```cpp
QStringList enumValuesForPin(const BPNode& node, const QString& key) const;
```
- `Global.Get/Set` 的 value：取 `params["varName"]` → `globalVarType` → 若 `enum:X` → 返回枚举 X 的 values。
- `Flow.Switch` 的 `caseval_<id>`：取 `params["enum"]`（绑定的枚举名）→ 返回其 values。
- 其它：空（非枚举）。

`effectivePins` 对上述引脚在"是枚举"时把 `kind` 设为 `EnumRef`。

## 全局变量枚举类型

- 「全局变量」面板的类型下拉，除 数值/布尔/字符串 外，**追加每个已声明枚举**为"枚举(<名>)"，写回 `type = "enum:<名>"`。
- `获取/设置 <枚举变量>` 节点：值引脚 kind = `EnumRef`，编辑时弹该枚举选项下拉。

## 分支控制枚举模式

- 新增 `params["enum"]`（枚举名，可空=自由值模式，行为同现在）。
- **绑定**：分支控制节点右键 → 「**按枚举…**」子菜单列出所有枚举 → 选一个：
  - 设 `params["enum"]`；
  - **一键生成**：为该枚举每个值追加一个分支，`caseval_<id>` 预置为该值（已存在的分支保留）。
- 绑定后：各分支比较值框 kind = `EnumRef`，点开是该枚举的下拉。
- 解绑（选"自由值"）：清 `params["enum"]`，比较值回到打字模式。

## 运行时

**无需改动**：枚举值就是字符串，分支控制运行时仍是字符串全等比较；全局变量存取也是字符串。枚举只是编辑期的"下拉约束"。

## 管理面板（枚举分页）

`GlobalVarPanel` 加一个分页/分区「枚举」：
- 左：枚举名列表（增 / 删 / 改名）；右：选中枚举的**选项列表**（增 / 删 / 改 / 排序）。
- 改动写回 `project.json` 的 `enums` 并发"变更"信号 → `EditorWindow` 重读枚举声明、推给所有蓝图编辑器（刷新类型下拉、菜单、重绘）。
- 枚举改名 → 同步：引用该枚举的全局变量类型 `enum:<旧>`→`enum:<新>`；分支控制 `params["enum"]` 同步（已打开蓝图）。

## 影响文件

- `launcher/src/editor/GlobalVars.h/.cpp`：新增 `EnumDef` + `Enums::load/save`。
- `launcher/src/editor/GlobalVarPanel.*`：枚举分页 + 全局变量类型下拉追加枚举项 + 变更/改名信号。
- `launcher/src/editor/BlueprintEditor.*`：`ValueKind::EnumRef`；`m_enumDefs` 缓存 + `setEnumDefs`；`enumValuesForPin`；`effectivePins`/`drawNode`/PinValue 分发的枚举下拉；分支控制"按枚举"菜单 + 生成分支。
- `launcher/src/editor/EditorWindow.*`：加载/推送枚举声明；面板枚举变更与改名转发。
- `CLAUDE.md`：如需，登记枚举相关说明。

## 验证

1. 枚举面板新增"模式=[单人,双人]"，`project.json` 正确读写、重开还原。
2. 全局变量类型选"枚举(模式)"；`设置 模式` 节点值框点开是 [单人,双人] 下拉。
3. 分支控制右键"按枚举 模式" → 自动生成"单人/双人"两个分支；比较值框是枚举下拉。
4. 关卡A `设置 模式=单人`（下拉选）→ 跳关卡B → 分支控制按枚举分流到"单人"出口。
5. 枚举改名/改选项 → 已打开蓝图相关节点同步、下拉更新。

## 后续子项目

- **函数式封装**：独立作用域、单返回、局部变量（与全局变量/枚举互补）。
- 枚举升级为内容浏览器 `.enum` 资产（可选）。
