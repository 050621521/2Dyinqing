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
| `LevelDocument` | Actor 列表、蓝图数据（BPNode/BPConnection）、脏标记 | `{project}/Levels/*.level`（JSON） |
| `LayoutManager` | 命名停靠布局的保存/恢复 | `{project}/layouts.json` |
| `ProjectSettingsDialog` | 默认启动关卡、PPU 设置 | `{project}/project.json` |

`LevelDocument` 不自动保存，`EditorWindow` 在 Tab 关闭/窗口关闭时检查 `isDirty()` 并提示。`LevelDocument::sortedActors()` 按 `(sortingLayer 优先级, orderInLayer)` 维护渲染顺序，用于视口绘制。

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

`DocTabBar` 继承自 `QTabBar`，管理多关卡 + 两个特殊 tab：

- 关卡 tab：`tabData()` 为关卡文件绝对路径
- 蓝图 tab：`tabData() == DocTabBar::kBlueprintTabData`（`"::blueprint::"`）
- 游戏视图 tab：`tabData() == DocTabBar::kGameViewTabData`（`"::game::"`）

蓝图 tab 可以被拖拽出 tab bar，触发 `blueprintDraggedOut(globalPos)` 信号，使蓝图编辑器浮动为独立 CDockWidget。`EditorWindow::floatBlueprint()` / `embedBlueprint()` 处理嵌入/浮动切换。

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

**数据模型**（存储于 `LevelDocument`）：
- `BPNode`：节点 id、类型（typeId）、画布坐标、引脚参数值（`QMap<QString, QString> params`）
- `BPConnection`：连线 id、fromNode/fromPin → toNode/toPin

**BlueprintEditor**：可视化编辑画布，支持节点拖拽、引脚连线、内联参数编辑（`QLineEdit` 原地编辑）。节点类型定义在 `nodeDefs()` 静态列表中，包含 `NodeDef`（typeId、显示名、header 颜色、引脚列表）。

**BPRuntime**：解释执行蓝图，通过 `QTimer` 驱动 `tick()`，维护独立的 actor 快照（`m_actors`，不影响 doc）。执行从 `triggerBeginPlay()` 触发「开始运行」执行链，键盘事件通过 `triggerKeyDown(key)` 触发。

运行生命周期（由 `EditorWindow` 管理）：
1. `startRuntime()` → 创建 `BPRuntime`，切换到 game view tab，`triggerBeginPlay()`
2. 每帧 `stateChanged` 信号 → `GameViewport::setRuntimeActors()` 刷新显示
3. `stopRuntime()` → 销毁 `BPRuntime`，恢复静态 actor 显示

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
| Action.Print | 打印字符串 |
| Action.MoveActor | 移动对象 |
| Action.SetActive | 设置激活 |
| Action.LoadLevel | 跳转关卡 |
| Action.BackLevel | 返回上一关 |
| Flow.Branch | 条件分支 |
| Flow.Switch | 分支控制 |
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
| Global.Get | 获取全局变量 |
| Global.Set | 设置全局变量 |
| Macro::* | 自定义节点 |
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
