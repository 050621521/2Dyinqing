# AGENTS.md

## 项目目标

本项目是 `2DYinqing` 的桌面端 2D 游戏引擎/编辑器，核心程序为 `launcher`。

整体流程为：

```text
main.cpp
  LauncherWindow ──projectSelected──▶ EditorWindow(ProjectInfo)
                                        │
                  ◀──editorClosed───────┘
```

`LauncherWindow` 负责项目列表、新建项目和打开项目；`EditorWindow` 负责场景编辑、蓝图编辑、游戏预览、UI 编辑、内容浏览、布局管理和运行时调试。

项目核心能力包括：

- 项目管理与最近项目列表
- 2D 场景编辑视口
- 多关卡 Tab
- 关卡蓝图
- Actor 蓝图类
- UI 编辑器与 UI 运行时
- 游戏预览视口
- 全局变量、局部变量与枚举资产
- 复现录制与调试记录

## 开发约定

在做功能开发、组件创建或行为修改前，先澄清目标、需求和设计方案，再进入实现。

修改任何 `.cpp`、`.h`、`.qss`、`.qrc` 文件后，必须重新编译并运行应用验证。

新增 `.cpp` 或 `.h` 文件时，必须同步更新 `launcher/CMakeLists.txt` 的 `SOURCES` 和 `HEADERS` 列表。

构建目录为 `launcher/build`。该目录已经配置过 CMake，通常直接执行 `cmake --build` 即可；修改 `CMakeLists.txt` 后，CMake 会自动检测并重新 configure。

修 bug 前，优先让用户用引擎内“复现录制”录制复现路径，再读取 `{project}/.recordings/latest.md` 定位问题，不要凭空猜测。

对外沟通和说明项目概念时，必须使用项目内中文名称，禁止用英文 `typeId` 或代码内部名称代替用户可见名称。

## 分阶段设计约束

在做分阶段功能设计时，必须区分“长期能力目标”和“当前可验收交付”。

凡是依赖尚未实现的核心链路才能测试的能力，不应放进当前阶段实现或验收标准，只能写入延后项。

如果必须做视觉占位，也要避免使用会暗示功能已实现的命名，并明确不参与规则。

当前阶段只承诺能被现有系统真实触发、真实观察、真实验收的内容。

## 启动 / 构建 / 测试命令

修改 C++、QSS 或 QRC 后重新编译并运行：

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

只运行现有构建产物，不重新编译：

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

`pkill -x launcher` 用于先关闭旧进程，避免 macOS 把 `open` 命令路由到旧的已运行实例。

当前技术栈：

- 语言：`C++17`
- GUI：`Qt6 Widgets`
- Qt 路径：`/opt/homebrew/opt/qt`
- 构建：`CMake 3.20+`
- Qt MOC：`CMAKE_AUTOMOC ON`
- 停靠系统：`Qt Advanced Docking System 4.3.1`
- 持久化：`QJsonDocument` 读写 JSON 文件

IDE 中的 `file not found`、`Unknown type name` 等诊断可能是 CMake include 路径未被 IDE 识别导致的误报。以实际 CMake 编译结果为准。

## 代码风格

样式集中在 `launcher/resources/styles/launcher.qss`。所有样式选择器基于 `setObjectName()`，不要使用类选择器。修改 QSS 后需要重新编译，因为样式通过 `.qrc` 打包。

`DetailsPanel` 中给控件赋值时应使用 `QSignalBlocker`，避免 `showActor()` 触发 `onAnyFieldChanged` 形成反馈循环。

Actor 类型定义在 `src/models/ActorTypeUtils.h`，跨文件共享，避免重复定义。

蓝图系统需要区分以下概念：

| 概念 | 定位 | 载体 | 作用域 |
|---|---|---|---|
| 关卡蓝图 | 当前关卡的导演脚本 | `LevelDocument` | 全局调度场上对象 |
| Actor 蓝图（类） | 某类对象自己的行为脚本 | `BPClass` / `.bp` | 仅操作自身 |
| 组件 | 装在对象类上的功能模块 | `BPClass.components` | 当前仅精灵渲染器有真实行为，其余部分仍可能是占位 |
| 蓝图节点 | 逻辑语句或函数调用 | `BPNode` | 由运行时解释执行 |

蓝图节点、Actor 类型、UI 控件类型对外必须使用中文名。常用名称包括：

- `Event.BeginPlay`：开始运行
- `Event.KeyDown`：按键按下
- `Event.Tick`：每帧
- `Action.Print`：打印字符串
- `Action.MoveActor`：移动对象
- `Action.LoadLevel`：跳转关卡
- `Action.BackLevel`：返回上一关
- `Flow.Branch`：条件分支
- `Flow.Switch`：分支控制
- `Flow.ForEach`：遍历数组
- `Global.Get`：获取全局变量
- `Global.Set`：设置全局变量
- `Self.GetPosition`：获取自身位置
- `Self.SetPosition`：设置自身位置
- `Self.Sprite.SetImage`：设置精灵图片
- `Self.Sprite.SetColor`：设置精灵颜色
- `Self.Anim.Play`：播放动画
- `Self.Anim.Stop`：停止动画
- `Self.Anim.SetAsset`：设置动画素材
- `UI.Create`：创建UI
- `UI.Show`：显示UI
- `UI.Hide`：隐藏UI
- `UI.Destroy`：销毁UI
- `UI.SetText`：设置文本
- `UI.SetValue`：设置进度值
- `UI.SetPosition`：设置UI位置
- `UI.SetVisible`：设置控件可见
- `UI.Follow`：跟随对象
- `UI.Ref`：UI引用变量
- `UI.OnButtonClick`：按钮点击时
- `UI.OnDropdownChanged`：下拉选项改变时

Actor 类型中文名：

| 代码值 | 中文名 |
|---|---|
| `Camera` | 摄像机 |
| `Sprite` | 精灵 |
| `Light` | 光源 |
| `Trigger` | 触发器 |
| `Empty` | 空对象 |

UI 控件类型中文名：

`UI.面板` / `UI.文本` / `UI.图片` / `UI.按钮` / `UI.进度条` / `UI.下拉菜单` / `UI.竖向布局` / `UI.横向布局` / `UI.网格布局` / `UI.滚动视图`

## 文件修改边界

数据模型与持久化位置：

| 类 | 职责 | 持久化位置 |
|---|---|---|
| `ProjectManager` | 最近项目列表、模板分类 | `~/.config/2DYinqing/projects.json` |
| `LevelDocument` | Actor 列表、关卡蓝图、脏标记 | `{project}/Levels/*.level` |
| `BPClass` | Actor 蓝图类：组件、默认值、节点图 | `{project}/Blueprints/*.bp` |
| `BPMacro` | 可复用宏节点子图 | `{project}/Blueprints/Macros/*.bpmacro` |
| `UIDocument` | UI 控件树 | `{project}/UI/*.ui` |
| `GlobalVars` / `EnumDef` | 全局变量声明、枚举资产 | `project.json` / `*.enum` |
| `LayoutManager` | 命名停靠布局 | `{project}/layouts.json` |
| `ProjectSettingsDialog` | 默认启动关卡、PPU 设置 | `{project}/project.json` |

`LevelDocument` 和 `UIDocument` 不自动保存。修改保存逻辑时，要保留 Tab 关闭和窗口关闭时的 `isDirty()` 检查与提示。

`ActorData.type` 已迁移为 `bpClass` 路径。内置类如 `builtin/Sprite`，自定义类如 `Blueprints/Player.bp`。旧 level 文件需要保持兼容迁移。

新增源码文件时，必须维护 `launcher/CMakeLists.txt`，否则构建不会包含新文件。

## 验收标准

每次修改功能后，至少完成：

1. 使用指定命令重新编译。
2. 编译成功后启动 `launcher.app`。
3. 验证修改涉及的主要交互路径。
4. 如果修改了运行时、蓝图、UI 或视口行为，要在游戏预览或对应编辑器页面中实际验证。
5. 如果修改了 `.qss` 或 `.qrc`，要确认重新编译后的样式资源已生效。
6. 如果修复 bug，应优先使用复现录制文件辅助确认修复前后的行为。

## 注意事项

`GameViewport` 只负责渲染和运行期显示。非运行时显示 `LevelDocument::sortedActors()`，运行时显示 `BPRuntime` 的 actor 快照。

`BPRuntime` 是关卡蓝图运行时，维护独立 actor 快照，不直接修改文档。

`ActorBPRuntime` 是 Actor 蓝图运行时，每个有节点的 Actor 会有一个实例，共享 `BPRuntime::mutableActors()`。

运行生命周期由 `EditorWindow::startRuntime()` 编排：

1. 创建 `BPRuntime`，注入全局变量表与 `UIRuntime`。
2. 为有节点的 Actor 创建 `ActorBPRuntime`。
3. 切到游戏视图。
4. 触发 `triggerBeginPlay()`。
5. 每帧通过 `stateChanged` 刷新 `GameViewport`。
6. 停止运行时销毁运行时对象并恢复静态显示。

`UIRuntime` 负责按蓝图 `UI.*` 节点创建和操作 UI 实例。按钮点击和下拉改变通过信号回流到 `BPRuntime` 与 `ActorBPRuntime`。

复现录制输出在 `{project}/.recordings/`，其中 `latest.md` 是最新录制。修 bug 时优先读取该文件。
