# 实现计划：蓝图通用变量/数据/运算能力（引擎特性 #1+#2）

对应设计：`docs/superpowers/specs/2026-06-22-blueprint-typed-values-and-operators-design.md`
日期：2026-06-22

## 总原则

- **每个 Phase 结束都必须编译通过并能运行**（用 CLAUDE.md 的编译命令），分步可回归。
- **改动外科手术化**：先做行为不变的底层迁移，再叠加新能力。
- **风险集中在 Phase 1**（求值层迁移，93 个调用点）——用隐式转换把它压成机械重构。
- 每个 Phase 后做对应验证；最后做一次全量旧蓝图回归。

## 回归素材（先准备）

仓库无 `.level` 样例。**Phase 0 前先手建一个回归蓝图**：包含 开始运行→打印字符串、按键→移动对象、获取/设置全局变量、条件分支 等现有节点，存为 `launcher/uitest/regression.level`（或项目内关卡）。每个 Phase 后跑它确认旧行为不变。

---

## Phase 0：`BPValue` 类型地基（纯新增，零行为变化）

**目标**：引入类型，单独可编译，不接入任何现有逻辑。

- 新建 `src/models/BPValue.{h,cpp}`（或放 `editor/`，与运行时同层）：
  - 标签联合体：`Number(double) / Bool / String(QString) / Array(QList<BPValue>) / Null`。
  - 构造：`BPValue()`=Null、`fromNumber/fromBool/fromString/fromArray`；**隐式 `BPValue(const QString&)` 与隐式 `operator QString() const`**（= `toString()`）——这是压缩 Phase 1 改动面的关键。
  - 转换：`toString() / toNumber() / toBool() / toArray()`，规则严格按 spec「强制转换」节（除0→0、解析失败→0、Array→JSON、Null→0/false/""）。
  - 相等：`bool typedEquals(const BPValue&)`（类型感知相等，供 `=/≠` 与「包含」用）。
  - 序列化：`toJsonValue() / fromJsonValue()`（变量默认值、调试用）。
- 更新 `CMakeLists.txt` 的 SOURCES/HEADERS，重新 configure。

**验证**：编译通过；写个临时 main 或断言验证几条转换规则（除0、字符串解析失败、数组转 JSON）。

---

## Phase 1：求值层迁移到 `BPValue`（行为保持）

**目标**：运行时改用 `BPValue` 承载，但所有现有蓝图行为**完全不变**。

- `BPRuntime` / `ActorBPRuntime`：
  - `resolveDataPin()` / `resolveOutputPin()` 返回类型 `QString` → `BPValue`。
  - `m_varStore` / `m_globalVars`（含 `ActorBPRuntime` 对应表）类型 `QMap<QString,QString>` → `QMap<QString,BPValue>`。
  - 因 `BPValue` 与 `QString` 隐式互转，**93 个调用点绝大多数无需改动**；只处理隐式转换产生歧义或 `auto` 推导变样的少数点。
  - 现有节点把 `params[key]`（字符串字面量）按引脚 `ValueKind` 解析为 `BPValue`（此阶段引脚类型还都是旧的，等价于 String，行为不变）。
  - `setGlobalVars(QMap<QString,QString>*)` 接口同步改为 `QMap<QString,BPValue>*`；上游 `EditorWindow` 持有的全局表一并改类型。
- `EditorWindow`：全局变量表成员类型同步为 `BPValue`。

**验证**：跑回归蓝图，打印/移动/分支/全局变量行为与迁移前逐项一致。**这是最关键的回归点**。

---

## Phase 2：`ValueKind` 扩展 + 编辑器类型管线（无新节点）

- `BlueprintEditor::ValueKind` 新增 `Number / Bool / Array / Any`；`kindFromString/kindToString` 同步。
- 引脚配色：按 kind 区分颜色（`drawPin`/`drawBezier`），数组引脚一种、数值/布尔各一种、Any 中性色。
- 连线校验：同类型干净连；标量跨类型允许并显示「自动转换」提示；**数组↔标量禁止连接**；`Any` 接受任意。
- 数组引脚附带「元素类型」标签（仅编辑器用）。

**验证**：手动连不同类型引脚，确认配色与禁止/提示规则；旧蓝图打开无异常。

---

## Phase 3：运算节点（数学/比较/逻辑/取反）

- `nodeDefs()` 新增 4 个 `NodeDef`：
  - 数值运算（op: + − × ÷ 取余；A,B:Number → 结果:Number）
  - 比较（op: > ≥ < ≤ = ≠；A,B:Any → 结果:Bool）
  - 逻辑（op: 与/或；A,B:Bool → 结果:Bool）
  - 取反（A:Bool → 结果:Bool）
- 运算符存 `node.params["op"]`，节点上以小下拉编辑（复用现有 params 下拉/内联机制）。
- `resolveOutputPin` 中实现各节点计算：比较的排序类走 `toNumber`，`=/≠` 走 `typedEquals`；除0→0。

**验证**：搭「A>0 且 B≤10」表达式接入条件分支，运行验证真假两路；除0 等边界。

---

## Phase 4：变量系统升级（类型化 + 局部作用域）

- 声明类型扩展 `array:<元素类型>`：`GlobalVarDef`/解析/`typeLabel` 支持；`GlobalVarPanel` 可选数组类型 + **默认值**编辑。
- 局部变量：
  - `LevelDocument` 增加局部变量声明列表（随蓝图文档持久化，params 字符串格式不变），含 toJson/fromJson。
  - 蓝图编辑器内新增局部变量列表面板（声明/改名/删/默认值）。
- 节点：保留 `获取全局变量/设置全局变量`（支持数组类型）；新增 `获取变量/设置变量`（局部）。
- **动态引脚类型**：Get/Set 选中变量后，数据引脚 `ValueKind` 跟随声明类型（复用 `enumValuesForPin` 那套按实例推导机制）。
- 运行时：局部变量在 `m_varStore`，按声明默认值初始化；全局在 `m_globalVars`。

**验证**：声明 number/bool/string/数组 的全局+局部变量并设默认值；Get/Set 读写正确；动态引脚类型随选择变化。

---

## Phase 5：数组节点

- 数据节点（纯数据，`resolveOutputPin` 计算）：创建空数组 / 数组长度 / 获取元素(越界→Null) / 包含(`typedEquals`)。
- 变量操作节点（exec，复用 Set 的变量选择器，内部读-改-写回，仅可选数组变量）：添加元素 / 移除元素(op:按索引/按值) / 设置元素 / 清空数组。

**验证**：对数组变量增删改、取元素、判包含、读长度，逐项正确；越界返回 Null。

---

## Phase 6：遍历(ForEach)

- `nodeDefs()` 新增「遍历数组」：exec入 + 数组入；exec出 循环体 / 完成；数据出 当前元素(Any) / 当前索引(Number)。
- `executeChain`/`executeNode` 改造：
  - 命中 ForEach 时对数组**取快照**，逐元素：写入按 nodeId 的迭代状态 `QMap<QString,{BPValue 当前元素,int 当前索引}>`，以**独立 visited 上下文**执行「循环体」子链；遍历完清除状态，再走「完成」。
  - 「当前元素/索引」输出引脚从该 nodeId 的迭代状态读取。
- 空数组：循环体 0 次、完成 1 次。嵌套：状态按 nodeId 区分天然支持。
- break/continue 不做。

**验证**：遍历非空/空数组；循环体内读当前元素/索引；嵌套两层 ForEach 互不干扰。

---

## Phase 7：术语表 + 全量回归

- `CLAUDE.md` 蓝图节点中文名表补充：数值运算/比较/逻辑/取反/获取变量/设置变量/创建空数组/数组长度/获取元素/包含/添加元素/移除元素/设置元素/清空数组/遍历数组。
- 全量回归：回归蓝图 + 新搭一个「综合用例」蓝图（用变量+数组+遍历+运算+分支拼一段真实逻辑），确认旧行为不变、新能力可用。
- 旧存档兼容性确认（打开迁移前保存的关卡正常）。

---

## 风险与对策

| 风险 | 对策 |
|---|---|
| Phase 1 隐式转换引发歧义/静默错误 | 编译告警全开；Phase 1 仅迁移不加新行为，逐项跑回归蓝图比对 |
| 93 调用点回归遗漏 | 回归蓝图覆盖现有主要节点；必要时配合「复现录制」机制比对时序 |
| ForEach 破坏 visited 防重入 | 循环体用独立 visited 上下文，单测嵌套与空数组 |
| 存档格式意外变动 | params 始终存字符串；迁移前后对同一关卡存盘做 diff |
