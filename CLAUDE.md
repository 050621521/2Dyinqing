# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Brainstorming 优先原则

**每次对话开始时，在做任何功能开发、组件创建、行为修改之前，必须先调用 `/brainstorming` 技能**，探索用户意图、需求和设计方案，再进入实现阶段。

## 编译与运行

修改任何 `.cpp` / `.h` / `.qss` / `.qrc` 文件后，**必须重新编译**再运行：

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

只运行（不重新编译）：

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

> **每次修改功能后，必须自动在终端执行上方编译命令，编译成功后立即启动应用，无需等用户提醒。**
> **重要：`pkill -x launcher` 会先关闭旧进程，避免 macOS 把 `open` 命令路由到旧的已运行实例。**

构建目录：`launcher/build`（cmake 已配置，直接 `cmake --build` 即可，无需重新 configure）。

新增 `.cpp`/`.h` 文件时，需同步更新 `launcher/CMakeLists.txt` 的 `SOURCES` 和 `HEADERS` 列表，然后重新 configure（cmake 会自动检测到 CMakeLists.txt 变化并重新 configure）。

## 技术栈

- **语言**：C++17
- **GUI**：Qt6 Widgets（`/opt/homebrew/opt/qt`）
- **构建**：CMake 3.20+，`CMAKE_AUTOMOC ON`（MOC 自动处理）
- **停靠系统**：Qt Advanced Docking System 4.3.1（`QtADS`，通过 `FetchContent` 自动拉取）
- **持久化**：QJsonDocument 读写 JSON 文件

IDE 诊断（`file not found`、`Unknown type name` 等）是 CMake include 路径未被 IDE 识别的**误报**，实际编译不受影响，忽略即可。

## 架构总览

### 两窗口流程

```
main.cpp
  LauncherWindow ──projectSelected──▶ EditorWindow(ProjectInfo)
                                        │
                  ◀──editorClosed───────┘
```

`LauncherWindow` 负责项目列表与新建；用户选择项目后，`main.cpp` 创建 `EditorWindow` 并隐藏 Launcher。编辑器关闭后再显示 Launcher。

### 数据层

| 类 | 职责 | 持久化位置 |
|---|---|---|
| `ProjectManager`（单例） | 最近项目列表、模板分类 | `~/.config/2DYinqing/projects.json` |
| `LevelDocument` | Actor 列表、**关卡蓝图**数据（BPNode/BPConnection）、脏标记 | `{project}/Levels/*.level`（JSON） |
| `BPClass` | **Actor 级蓝图（类）**：组件列表 + 默认值 + 节点图 | `{project}/Blueprints/*.bp`（JSON）；内置类无文件 |
| `BPMacro` | 宏（自定义节点）：可复用子图 + 对外引脚 | `{project}/Blueprints/Macros/*.bpmacro` |
| `UIDocument` | UI 控件树（UIWidget 层级、布局、类型专属属性） | `{project}/UI/*.ui`（JSON） |
| `GlobalVars` / `EnumDef` | 全局变量声明 / 枚举资产 | 全局变量存 `project.json`；枚举存 `*.enum` |
| `LayoutManager` | 命名停靠布局的保存/恢复 | `{project}/layouts.json` |
| `ProjectSettingsDialog` | 默认启动关卡、PPU 设置 | `{project}/project.json` |

`LevelDocument` / `UIDocument` 不自动保存，`EditorWindow` 在 Tab 关闭/窗口关闭时检查 `isDirty()` 并提示。`LevelDocument::sortedActors()` 按 `(sortingLayer 优先级, orderInLayer)` 维护渲染顺序，用于视口绘制。

`ActorData.type` 现为 **bpClass 路径**：内置类如 `"builtin/Sprite"`，自定义类如 `"Blueprints/Player.bp"`（相对项目根）；旧 level 文件自动兼容迁移。

### EditorWindow 架构

#### 停靠系统（QtADS）

所有面板都包装在 `ads::CDockWidget` 中，由 `ads::CDockManager`（`m_dockManager`）统一管理：

| CDockWidget 变量 | 内容 |
|---|---|
| `m_viewportDock` | `Viewport2D`（2D 场景编辑视口） |
| `m_outlineDockW` | `SceneOutliner`（大纲面板） |
| `m_detailsDockW` | `DetailsPanel`（细节面板） |
| `m_cbDockW` | `ContentBrowser`（内容浏览器） |
| `m_bpDockW` | `BlueprintEditor`（蓝图编辑器） |

`LayoutManager` 在 `EditorWindow` 构造时捕获默认布局（`captureDefault()`），通过 "窗口" 菜单可保存/加载/重置布局。

#### 中央区域（QStackedWidget）

`m_centralStack` 有四个页面，通过 `DocTabBar` 的 tab 切换：

- index 0 **视口页**：`Viewport2D` + 工具栏（选择/移动/旋转/缩放 工具按钮）
- index 1 **蓝图包装页**：`BlueprintEditor`（也可通过 tab 拖拽变为浮动 CDockWidget）
- index 2 **游戏视图页**：`GameViewport` + 工具栏（摄像机名称、分辨率标签、运行控制按钮）
- index 3 **UI 编辑器页**：`UIEditor`（控件树 + 画布 + 属性面板）

#### DocTabBar（多关卡 Tab）

`DocTabBar` 继承自 `QTabBar`，管理多关卡 tab、文件资产 tab 与两个特殊 tab。`tabData()` 取值：

- 关卡 tab：关卡文件绝对路径
- 关卡蓝图 tab：以 `DocTabBar::kBlueprintTabData`（`"::blueprint::"`）为**前缀**
- 游戏视图 tab：`DocTabBar::kGameViewTabData`（`"::gameview::"`）
- 文件资产 tab：`.bp`（Actor 蓝图）/ `.ui`（UI 文档）/ `.enum`（枚举编辑器）文件路径，按后缀判别

蓝图 tab（关卡蓝图 **或** `.bp` Actor 蓝图，判定 `data.startsWith(kBlueprintTabData) || data.endsWith(".bp")`）可被拖出 tab bar，触发 `blueprintDraggedOut` 信号，使蓝图编辑器浮动为独立 CDockWidget。`EditorWindow::floatBlueprint()` / `embedBlueprint()` 处理嵌入/浮动切换。

每次切换关卡 tab 时，**断开全部旧连接**（`m_tabConnections`），再重新建立当前 tab 的信号槽：
- `SceneOutliner::actorSelected` → `DetailsPanel::showActor` + `Viewport2D::setSelectedId`
- `Viewport2D::actorDragging` → `DetailsPanel::showActor`（实时）
- `Viewport2D::actorTransformed` / `actorCreated` → 刷新大纲 + 标记脏
- `DetailsPanel::actorModified` → `doc->updateActor` + 刷新视口/大纲

### Viewport2D（场景编辑视口）

工具模式（`ToolMode`）：`Select / Move / Rotate / Scale`，通过 `setToolMode()` 切换并更新鼠标样式（`applyToolCursor()`）。

Gizmo 命中检测在 `mousePressEvent` 中按优先级顺序：
1. Move 模式的箭头端点（`ScaleHandle::AxisX/AxisY`）
2. Scale 模式的方块端点（`ScaleHandle::AxisX/AxisY/Center`）
3. Actor 本体（任意模式）

`ScaleHandle` 枚举被 Move 和 Scale 两种模式复用。Gizmo 臂长固定为 **60px 屏幕空间**，与 Actor 缩放无关。坐标转换：`worldToScreen` / `screenToWorld`（含 zoom 和 offset）。

### GameViewport（游戏预览视口）

仅渲染、不可交互。使用场景中 `cameraIsMain == true` 的摄像机 Actor 参数（cameraSize、分辨率、背景色）来确定视口裁切区域。`setPixelsPerUnit(ppu)` 控制世界坐标到像素的换算。

运行时（`m_runtimeMode == true`）显示 `BPRuntime` 的 actor 快照；非运行时直接显示 `LevelDocument::sortedActors()`。图片资源通过 `m_pixmapCache` 缓存。

### 蓝图系统

**核心心智模型**（与虚幻引擎一致，沟通时务必区分）：

| 概念 | 定位 | 载体 | 作用域 |
|---|---|---|---|
| **关卡蓝图** | 当前关卡的「导演脚本」，不绑定单个对象 | 存于 `LevelDocument`，`BPRuntime` 执行 | 全局：开始运行、按键、跳转关卡、调度场上对象 |
| **Actor 蓝图（类）** | 某个对象自己的行为脚本 | `BPClass`（`.bp`），`ActorBPRuntime` 执行（每实例一份） | 仅自身：`Self.*` 操作自身组件/位置/精灵 |
| **组件** | 装在对象（类）身上的功能模块 | `BPClass.components` | 当前仅**精灵渲染器**有真实行为，其余占位 |
| **蓝图节点** | 逻辑里的一条语句/函数调用 | `BPNode` | — |

**数据模型**：
- `BPNode`：节点 id、类型（typeId）、画布坐标、引脚参数值（`QMap<QString, QString> params`）
- `BPConnection`：连线 id、fromNode/fromPin → toNode/toPin

**BPValue**（`src/models/BPValue.h`）：运行时「类型化值」，数据引脚与变量统一用它承载真实类型（`Null/Number/Bool/String/Array`）。刻意保留与 `QString` 的隐式互转以压缩历史求值调用点的迁移面。`=`/`≠`/「包含」用 `typedEquals`（类型不同即不等，`3 ≠ "3"`）。

**BlueprintEditor**（双模式）：
- **level 模式**：编辑关卡蓝图（原有）
- **bpClass 模式**：编辑某个 `.bp` 类；右键菜单的 `Self.*` 分类按该类的组件过滤
- 节点类型定义在 `nodeDefs()` 静态列表（`NodeDef`：typeId、显示名、header 颜色、引脚列表）。支持节点拖拽、引脚连线、内联参数编辑。

**宏 / 自定义节点**（`BPMacro`）：可复用子图 + 对外输入/输出引脚，存为 `.bpmacro`。运行时**内联展开**（`BPRuntime::flattenMacros()`），支持嵌套。调用节点 typeId 形如 `Macro::<id>`。

**全局变量与枚举**（`GlobalVars.h`）：全局变量声明存 `project.json`，运行期变量表由 `EditorWindow` 持有（`m_globalVars`），**跨关卡保留**、整局重置。枚举为 `.enum` 资产（键值 `values` 用于逻辑比较，`displays` 用于 UI 显示）。

**两套运行时**：
- `BPRuntime`（关卡）：`QTimer` 驱动 `tick()`，维护独立 actor 快照 `m_actors`（不改 doc）；持有本关变量表 `m_varStore`、`m_loopState`（ForEach 迭代）、`m_heldKeys`（按住键每帧驱动）。
- `ActorBPRuntime`（每个有节点的 Actor 一个实例）：共享 `BPRuntime::mutableActors()` 指针（运行期不增删 Actor，列表不重分配），各自跑 `Self.*` 逻辑。

运行生命周期（由 `EditorWindow::startRuntime()` 编排）：
1. 创建 `BPRuntime`，注入全局变量表与 `UIRuntime`；为每个有节点的 Actor 创建 `ActorBPRuntime`
2. 切到 game view tab，触发各运行时的 `triggerBeginPlay()`
3. 每帧 `stateChanged` → `GameViewport` 刷新；按键/UI 事件分发给关卡运行时**与**所有 `ActorBPRuntime`
4. `loadLevelRequested` / `backLevelRequested` 信号驱动换关（关卡历史栈）
5. `stopRuntime()` → `qDeleteAll(m_actorRuntimes)` + 销毁 `BPRuntime`/`UIRuntime`，恢复静态显示

### DetailsPanel

- 使用 `QSignalBlocker` 包裹所有控件赋值，防止 `showActor()` 触发 `onAnyFieldChanged` 形成反馈循环。
- 折叠区块结构：`QGroupBox` → `QVBoxLayout` → `titleBar`（含 `QToolButton` ▼/▶）+ `content`（`QGridLayout`）。
- 摄像机大小默认值为 `540.0`（对应 1080p 分辨率下 PPU=1 的半高）。

### Actor 类型与组件

Actor 类型定义在 `src/models/ActorTypeUtils.h`（跨文件共享，避免重复）：

| 类型 | 默认组件 |
|---|---|
| Camera | 变换、摄像机组件 |
| Sprite | 变换、精灵渲染器 |
| Light | 变换、点光源 |
| Trigger | 变换、碰撞盒 |
| Empty | 变换 |

### UI 系统

**UIEditor**（中央区 index 3）：控件树 + 画布 + 属性面板三栏，编辑 `UIDocument`（`.ui` 文件）。`UIWidget` 为层级结构（`parentId` 空=根），屏幕空间布局以 `anchor`（左上/居中/…九宫格）为基准做相对偏移；类型专属字段（文本、进度值、网格列数、下拉选中项等）集中在同一结构体内按 `type` 取用。`UIDocument` 自带 `QUndoStack`。

**UIRuntime**：运行时按蓝图的 `UI.*` 节点创建/操作控件实例。`UI.Create` 节点 id → 实例 id 的映射（`m_uiRefs`）由各运行时各自持有。按钮点击 / 下拉改变通过 `UIRuntime` 信号回流到 `BPRuntime` 与所有 `ActorBPRuntime`，触发 `UI.OnButtonClick` / `UI.OnDropdownChanged` 事件链。

### 复现录制（修 bug 工作流）

`Recorder`（单例，`src/editor/Recorder.cpp`）：手动开始/停止，期间接管 `qDebug` 并收集语义化操作时间线 + 状态快照 + 控制台日志，停止时写出一份 **AI 可直接读取的 Markdown**（`{project}/.recordings/`，`latest.md` 为最新）。连续相同条目自动合并为「×N」防帧级刷屏。

> **修 bug 前**：先让用户用引擎内「复现录制」录一遍复现路径，再读 `.recordings/latest.md` 定位问题，而不是凭空猜测。

### 样式系统

单一 QSS 文件：`resources/styles/launcher.qss`。所有样式选择器基于 `setObjectName()`，不使用类选择器，便于精确控制。修改 QSS 后需重新编译（通过 `.qrc` 打包）。

---

## 项目术语：始终使用中文名

与用户沟通时，**必须使用项目内的中文名称，禁止用英文 typeId 或代码内部名称**。

### 蓝图节点名（右键菜单中的显示名）

| typeId | 中文名 |
|---|---|
| Event.BeginPlay | 开始运行 |
| Event.KeyDown | 按键按下 |
| Event.Tick | 每帧 |
| Action.Print | 打印字符串 |
| Action.MoveActor | 移动对象 |
| Action.SetActive | 设置激活 |
| Action.LoadLevel | 跳转关卡 |
| Action.BackLevel | 返回上一关 |
| Flow.Branch | 条件分支 |
| Flow.Switch | 分支控制 |
| Flow.ForEach | 遍历数组 |
| Math.Add | 加法（+） |
| Math.Sub | 减法（-） |
| Math.Mul | 乘法（×） |
| Math.Div | 除法（÷） |
| Math.Mod | 取余（%） |
| Math.Clamp | 数值夹取 |
| Cmp.GT | 大于（>） |
| Cmp.GE | 大于等于（≥） |
| Cmp.LT | 小于（<） |
| Cmp.LE | 小于等于（≤） |
| Cmp.EQ | 等于（=） |
| Cmp.NE | 不等于（≠） |
| Logic.And | 与 |
| Logic.Or | 或 |
| Logic.Not | 非 |
| Logic.Compare | 数值比较 |
| Array.Make | 创建空数组 |
| Array.Length | 数组长度 |
| Array.Get | 获取元素 |
| Array.Contains | 包含 |
| Array.Add | 添加元素 |
| Array.RemoveAt | 按索引移除 |
| Array.RemoveValue | 按值移除 |
| Array.SetAt | 设置元素 |
| Array.Clear | 清空数组 |
| Global.Get | 获取全局变量 |
| Global.Set | 设置全局变量 |
| Local.Get | 获取 <局部变量>（动态名） |
| Local.Set | 设置 <局部变量>（动态名） |
| Macro::* | 自定义节点 |
| Var.GetNumber | 获取数值变量 |
| Var.SetNumber | 设置数值变量 |
| Var.GetBool | 获取布尔变量 |
| Var.SetBool | 设置布尔变量 |
| Var.GetString | 获取字符串变量 |
| Var.SetString | 设置字符串变量 |
| Var.NumberToString | 数值转字符串 |
| Var.GetActorPos | 获取位置 |
| Var.ActorRef | Actor引用 |
| Self.GetPosition | 获取自身位置 |
| Self.SetPosition | 设置自身位置 |
| Self.GetRotation | 获取自身旋转 |
| Self.SetRotation | 设置自身旋转 |
| Self.IsActive | 获取激活状态 |
| Self.SetActive | 设置激活状态 |
| Self.GetName | 获取自身名称 |
| Self.Sprite.SetImage | 设置精灵图片 |
| Self.Sprite.SetColor | 设置精灵颜色 |
| Self.Sprite.SetFlipX | 水平翻转 |
| Self.Sprite.SetFlipY | 垂直翻转 |
| Self.Sprite.SetVisible | 设置精灵可见 |
| Self.Anim.Play | 播放动画 |
| Self.Anim.Stop | 停止动画 |
| Self.Camera.SetSize | 设置摄像机尺寸 |
| Self.Camera.SetBackground | 设置背景色 |
| UI.Create | 创建UI |
| UI.Show | 显示UI |
| UI.Hide | 隐藏UI |
| UI.Destroy | 销毁UI |
| UI.SetText | 设置文本 |
| UI.SetValue | 设置进度值 |
| UI.SetPosition | 设置UI位置 |
| UI.SetVisible | 设置控件可见 |
| UI.Ref | UI引用变量 |
| UI.OnButtonClick | 按钮点击时 |
| UI.OnDropdownChanged | 下拉选项改变时 |

### Actor 类型名

| 代码值 | 中文名 |
|---|---|
| Camera | 摄像机 |
| Sprite | 精灵 |
| Light | 光源 |
| Trigger | 触发器 |
| Empty | 空对象 |

### UI 控件类型名

`UI.面板` / `UI.文本` / `UI.图片` / `UI.按钮` / `UI.进度条` / `UI.下拉菜单` / `UI.竖向布局` / `UI.横向布局` / `UI.网格布局` / `UI.滚动视图`
