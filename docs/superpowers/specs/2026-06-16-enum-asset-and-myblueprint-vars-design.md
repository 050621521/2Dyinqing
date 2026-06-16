# 枚举资产化 + 变量面板「我的蓝图」化 设计文档

日期：2026-06-16
状态：方向已确认（对齐虚幻），待评审

## 背景与动机

当前枚举塞在「全局变量」面板里、存 `project.json`，和虚幻不一致。虚幻的真实分法：

- **变量** 在"我的蓝图"面板声明，类型可选到工程里**已有的枚举**，但**不在这里创建枚举**。
- **枚举** 是**内容浏览器里的独立资产**（蓝图枚举），右键新建，双击进**专门的枚举编辑器**改选项。

本子项目据此重构：**枚举资产化（内容浏览器 + 枚举编辑器）**，**变量面板改成"我的蓝图"风格（只管变量）**。功能（类型联动、分支按枚举、改名同步）保留，只换组织与交互。

## 目标与范围

1. **枚举资产 `.enum`**：JSON `{name, values}`，散在内容浏览器文件夹里。
2. **内容浏览器**：列出 `.enum`、新建枚举、重命名、删除、双击打开。
3. **枚举编辑器**：中央区一个页签（与 UI/蓝图 同机制），编辑某枚举的选项（增/删/改/排序）。
4. **变量面板 → 我的蓝图风格**：分栏列表 + 细节区，只管变量；类型下拉含工程里扫到的枚举。
5. **读取改造**：枚举来源从 `project.json` 改为**扫工程内 `.enum` 文件**。

**不做（后续）：**
- 变量的拖拽到画布生成节点（仍用右键菜单创建）。
- 枚举引用按路径（先按名字引用，改名做同步）。
- project.json 旧 `enums` 的自动迁移（测试数据，重建即可）。

## 数据模型

### 枚举资产 `.enum`

```json
{ "name": "模式", "values": ["单人", "双人"] }
```

- 文件名 = 枚举名（如 `模式.enum`），和 `.ui`/`.level` 一致按名管理。
- 全局变量类型仍是 `"enum:<枚举名>"`，引用按名字。

### 读取

`Enums` 模块改为扫描工程（递归找 `.enum`）：

```cpp
// 旧：Enums::load(projectRoot) 读 project.json —— 改为扫 .enum
QList<EnumDef> Enums::loadAll(const QString& projectRoot);   // 递归扫描 *.enum
bool           EnumDef::save(const QString& filePath) const; // 单个枚举写盘
EnumDef        EnumDef::load(const QString& filePath);
```

`GlobalVars`（变量）仍存 `project.json` 不变。

## 内容浏览器集成

复用现有按扩展名管理资产的机制（`.level`/`.bp`/`.ui`）：

- `populateFolder`：新增列出 `*.enum`（图标 + 名字）。
- 工具栏/右键："**新建枚举**" → 输入名 → 写空 `.enum`（默认 1~2 个选项）→ 打开。
- 网格右键：`.enum` 项 → 打开 / 重命名 / 删除（重命名即改枚举名，触发引用同步）。
- 双击 `.enum` → 发信号 `enumOpenRequested(path)`。

## 枚举编辑器（EnumEditor）

中央区一个页签（与 `.ui` 文档开页签同套路）：

- 顶部：枚举名（只读，改名在内容浏览器做）。
- 主体：**选项列表**（每行一个值，可改名）+ `＋ 添加` / `－ 删除` / 上下移。
- 改动写回该 `.enum` 文件并发"枚举变更"信号 → `EditorWindow` 重扫枚举、推给蓝图编辑器与变量面板。

> 承载：`EditorWindow` 的中央 `QStackedWidget` 新增一页 `EnumEditor`；`DocTabBar` 打开 `.enum` tab 时切到该页并 `load(path)`（多个枚举复用一个编辑器页，按当前 tab 切换数据）。

## 变量面板（我的蓝图风格）

`GlobalVarPanel` 重做（**移除枚举区**）：

- **上半 列表**：分栏「变量」（右侧 `＋`）。每行：**类型色块**（数值=蓝/布尔=红/字符串=粉/枚举=青）+ 变量名。
- **下半 细节**：选中变量 → 名字(可改) + **类型下拉**（数值/布尔/字符串 + 工程里扫到的每个枚举"枚举(X)"）+ 删除。
- 改名仍发 `varRenamed` 同步引用节点；类型/增删写回 `project.json`。

## 读取与联动改造

- `EditorWindow`：枚举来源改 `Enums::loadAll(projectRoot)`；内容浏览器/枚举编辑器的"枚举变更"信号 → 重扫 + `setEnumDefs` 推给所有蓝图编辑器 + 刷新变量面板类型下拉。
- 枚举改名（内容浏览器重命名 `.enum`）：同步更新引用该枚举的全局变量类型 `enum:<旧>`→`enum:<新>`（已加载工程）与分支控制 `params["enum"]`（已打开蓝图）。
- `BlueprintEditor` 的 `EnumRef` 引脚、分支控制按枚举模式：**逻辑不变**，只是枚举声明来源变了。

## 影响文件

- `launcher/src/editor/GlobalVars.h/.cpp`：`EnumDef::save/load`（单文件）+ `Enums::loadAll`（扫描）；移除/替换 `Enums::load(project.json)`。
- `launcher/src/editor/GlobalVarPanel.*`：重做为"列表+细节"、移除枚举区、类型下拉读扫描枚举。
- `launcher/src/editor/ContentBrowser.*`：`.enum` 列出/新建/重命名/删除/双击信号。
- 新增 `launcher/src/editor/EnumEditor.h/.cpp`：枚举编辑页签。
- `launcher/src/editor/EditorWindow.*`：中央 stack 加 EnumEditor 页；`.enum` tab 承载；枚举来源改扫描；变更/改名转发。
- `CMakeLists.txt`：登记 EnumEditor。

## 验证

1. 内容浏览器右键"新建枚举 模式" → 生成 `模式.enum`；双击进枚举编辑器加"单人/双人"。
2. 变量面板新建变量、类型下拉里出现"枚举(模式)"，选它。
3. `设置 模式` 节点值框下拉是 [单人,双人]；分支控制"按枚举 模式"生成分支。
4. 枚举编辑器加一个选项 → 节点下拉同步多一项。
5. 内容浏览器重命名 `模式.enum` → 引用它的变量类型/分支同步；删除有提示。
6. 关卡A `设置 模式=单人` → 跳关卡B → 分支按枚举分流正确。
7. 存盘/重开工程，枚举与变量正确还原。
