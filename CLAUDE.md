# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
| `LevelDocument` | 单个关卡的 Actor 列表、脏标记 | `{project}/Levels/*.level`（JSON） |
| `ProjectSettingsDialog::readDefaultLevel` | 读取默认启动关卡 | `{project}/project.json` |

`LevelDocument` 不自动保存，`EditorWindow` 在 Tab 关闭/窗口关闭时检查 `isDirty()` 并提示。

### EditorWindow 多 Tab 架构

- `QTabBar` (`m_docTabBar`) + `QMap<QString, LevelDocument*>` (`m_openLevels`) 管理多关卡。
- 每次切换 Tab 时，**断开全部旧连接**（`m_tabConnections`），再重新建立当前 Tab 的信号槽：
  - `SceneOutliner::actorSelected` → `DetailsPanel::showActor` + `Viewport2D::setSelectedId`
  - `Viewport2D::actorDragging` → `DetailsPanel::showActor`（实时）
  - `Viewport2D::actorTransformed` / `actorCreated` → 刷新大纲 + 标记脏
  - `DetailsPanel::actorModified` → `doc->updateActor` + 刷新视口/大纲

### Viewport2D

工具模式（`ToolMode`）：`Select / Move / Rotate / Scale`，通过 `setToolMode()` 切换并更新鼠标样式（`applyToolCursor()`）。

Gizmo 命中检测在 `mousePressEvent` 中按优先级顺序：
1. Move 模式的箭头端点（`ScaleHandle::AxisX/AxisY`）
2. Scale 模式的方块端点（`ScaleHandle::AxisX/AxisY/Center`）
3. Actor 本体（任意模式）

`ScaleHandle` 枚举被 Move 和 Scale 两种模式复用，用于区分拖拽哪个轴。Gizmo 臂长固定为 **60px 屏幕空间**，与 Actor 缩放无关。

坐标转换：`worldToScreen` / `screenToWorld`（含 zoom 和 offset）。

### DetailsPanel

- 使用 `QSignalBlocker` 包裹所有控件赋值，防止 `showActor()` 触发 `onAnyFieldChanged` 形成反馈循环。
- 折叠区块结构：`QGroupBox` → `QVBoxLayout` → `titleBar`（含 `QToolButton` ▼/▶）+ `content`（`QGridLayout`）。`QToolButton::toggled` 控制 `content->setVisible()`。

### ContentBrowser

以**浮动覆盖层**实现（`QWidget` 子控件，不是 `QDockWidget`），通过 `eventFilter` 监听 `leftWrap` 的 `Resize` 事件来重新定位（`positionCBPanel()`）。底部状态栏的 toggle 按钮控制显示/隐藏。

### 样式系统

单一 QSS 文件：`resources/styles/launcher.qss`。所有样式选择器基于 `setObjectName()`，不使用类选择器，便于精确控制。修改 QSS 后需重新编译（通过 `.qrc` 打包）。
