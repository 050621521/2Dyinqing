# 精灵渲染器颜色「取消/重置」设计

日期：2026-06-25
状态：待实现

## 背景与问题

精灵渲染器的「颜色」是一层**叠在图片上的染色滤镜**（multiply tint）：
- 白色 `#ffffffff` = 不染色，露出图片本来的颜色。
- 没有图片时根本不使用 `spriteColor`（走占位图形分支），所以它纯粹是滤镜，不是纯色填充。

渲染实现（`Viewport2D.cpp` / `GameViewport.cpp`）：先 `drawPixmap`（用 alpha 控整体透明），当 RGB ≠ 白时用 `CompositionMode_SourceAtop` 叠一层 alpha=100 的 tint。

**痛点**：细节面板里颜色只有一个按钮（打开 `QColorDialog`），一旦选了非白颜色，**没有任何一键还原的入口**，只能在调色盘里手动拖回纯白+满透明，很难拖准，用户感觉「设了就取消不掉」。

## 关键概念：类默认值 + 实例覆盖（已存在的机制）

引擎已有一套与虚幻一致的 archetype/override 系统：
- Actor **类**（`BPClass`，`.bp` 文件）持有 `defaults`（字段名→默认值），其中包含 `spriteColor`。一个自定义类可以把默认色设成红/蓝等。
- 场上每个**实例**有 `ActorData.overriddenFields`（`QSet<QString>`）。某字段**不在**该集合里 = 跟随类默认值（活继承）；一旦实例改了该字段，`EditorWindow` 在 `actorModified` 中 diff 后把它加入 `overriddenFields`。
- `resolveInstanceFromClass(inst, cls)`：把类默认值刷进实例所有「未覆盖」字段。
- 内置类（`builtin/*`）实例不参与覆盖标记（`overriddenFields` 保持空），其「默认」即引擎硬编码默认值。

因此「取消颜色」的正确语义 = **取消该实例对颜色的覆盖，回到所属类的默认色**；只有内置精灵（无自定义类）才回到硬默认白色。这正是虚幻「reset to default 箭头」的语义——还原到 archetype，而非硬编码白。

## 参考：虚幻 / Unity 的做法

- **虚幻**：每个属性右侧，当值 ≠ archetype 默认时显示黄色回退箭头（↩），点击单独还原该属性到默认。
- **Unity**：组件标题右键 → Reset，整组件所有字段一次性还原到默认。

本设计两者都做。

## 设计

### 入口一：颜色旁「↩」重置小箭头（虚幻风格，逐字段）

- 在 `DetailsPanel` 精灵渲染器分组的「颜色」行，颜色按钮（`m_spriteColorBtn`）右侧新增一个小箭头按钮（如 `m_spriteColorResetBtn`，objectName 便于 QSS 控制）。
- **可见/启用条件**（在 `showActor` 与颜色改动后更新）：
  - 自定义类实例（`bpClass` 非空且非 `builtin/`）：`overriddenFields.contains("spriteColor")` 为真时亮起。
  - 内置精灵：`spriteColor != QColor(255,255,255,255)` 时亮起。
  - 否则置灰/隐藏。
- **点击效果**：请求把 `spriteColor` 还原成默认：
  - 自定义类实例：从 `overriddenFields` 移除 `spriteColor`，`resolveInstanceFromClass` 重解析 → 回到类默认色。
  - 内置精灵：直接设回 `#ffffffff`。
  - 颜色块、箭头状态实时刷新；纳入撤销栈。

### 入口二：组件标题右键「重置精灵渲染器」（Unity 风格，整组件）

- 在「精灵渲染器」分组标题栏安装右键菜单（`setContextMenuPolicy(Qt::CustomContextMenu)` 或在 titleBar 上装事件），菜单项「重置」。
- 点击：把该组件涉及的所有字段（`spriteColor`、`flipX`、`flipY`、`drawMode`、`spritePath` 等渲染相关字段）一次性还原成默认：
  - 自定义类实例：从 `overriddenFields` 移除这些字段并重解析回类默认。
  - 内置精灵：各字段设回硬默认值。
- 一次撤销可整体还原；刷新视口/大纲/细节面板。

### 职责划分与实现要点

- **还原动作放在 `EditorWindow`**（它持有项目根、能 `BPClass::load` 与 `resolveInstanceFromClass`，并已统一管理撤销栈 `ActorModifyCmd`）。
- **`DetailsPanel` 新增信号**，例如：
  - `void actorFieldsReset(const QString& actorId, const QStringList& fields);`
  - 细节面板只负责：根据 `m_currentActor`（含 `bpClass` / `overriddenFields`）决定箭头亮灭；点击/菜单时发出该信号；收到 `showActor` 回填后刷新控件。
- **`EditorWindow` 接住信号**：定位 before、移除指定 `overriddenFields`、重解析（内置精灵则直接写硬默认）、`push(ActorModifyCmd)`、刷新并 `m_detailsPanel->showActor(after)`。
- 复用现有 `actorModified` → diff → 标记覆盖的链路心智，避免新增并行机制。

## 决策记录

- 「取消颜色」语义 = 取消覆盖回类默认，而非无脑纯白（用户明确）。
- 内置精灵（无自定义类）重置 → 纯白（取消染色、露出图片本色）（用户确认）。
- 两个入口都做：颜色逐字段箭头 + 组件整体右键重置（用户确认）。

## 不做（YAGNI）

- 不为其它组件（摄像机、光源等）的所有字段铺开虚幻式逐字段箭头——本次仅聚焦精灵渲染器的颜色诉求；组件整体重置可顺带覆盖精灵渲染器其余字段。
- 不改动渲染管线与染色算法本身。
