# 蓝图引脚类型驱动内联编辑器 设计文档

日期：2026-06-15
状态：已确认，待实现

## 背景与动机

当前蓝图节点上的参数栏（引脚内联编辑器）大多是「打字编辑」的 `QLineEdit`。例如「跳转关卡」节点的**关卡名**需要手输关卡名字，容易打错；布尔参数（激活/翻转/可见）也是打字。

分发逻辑（`BlueprintEditor.cpp` 约 1048 行）目前是硬编码：

```cpp
if (hit.pinName == "actorId")      showParamEditPopup(...);
else if (hit.pinName == "uiName")  showUIAssetPicker(...);
else                               showInlineEdit(...);   // 打字
```

新旧两套并存、靠 key 名硬编码，难以扩展。

## 目标

参考**虚幻引擎蓝图**的做法：**内联编辑器由引脚的「值类型」决定，而非给每个参数单独写死**。让「能从当前项目枚举出选项」的参数自动变成下拉/勾选，只有真正自由的数值/字符串才保留打字。

虚幻的对应关系：bool → 勾选框；enum/资产引用 → 下拉；int/float/string → 输入框。

## 设计

### 1. 数据结构：给 PinDef 增加值类型

`BlueprintEditor.h` 中：

```cpp
enum class ValueKind { Text, Number, Bool, LevelRef, ActorRef, UIRef, WidgetRef };

struct PinDef {
    QString   key;
    QString   label;
    bool      isExec;
    bool      isOutput;
    ValueKind kind = ValueKind::Text;   // 默认 Text，不破坏现有 4 字段初始化列表
};
```

利用 C++ 聚合初始化「尾部成员可省略并取默认值」特性，现有 `{key,label,isExec,isOutput}` 写法保持有效；只在需要的引脚补第 5 个字段。

需标注的引脚（在 `nodeDefs()` 静态列表中）：

| 引脚 key | kind | 出现节点 |
|---|---|---|
| `levelName` | `LevelRef` | 跳转关卡 |
| `active` | `Bool` | 设置激活、设置激活状态 |
| `flip`（含 SetFlipX/SetFlipY） | `Bool` | 水平翻转、垂直翻转 |
| `visible` | `Bool` | 设置精灵可见、设置控件可见 |
| `actorId` | `ActorRef` | 设置激活、获取位置、Actor引用、移动对象 等 |
| `target` | `ActorRef` | 设置跟随目标 |
| `uiName` | `UIRef` | 显示UI / UI引用 等 |
| `widgetRef` | `WidgetRef` | 各 UI 节点 |

其余（X/Y/角度/尺寸/RGBA/路径/文本/数值/条件等）保持 `Text`/`Number`，仍打字。

> 注：`target` 存的是 Actor **名称**，`actorId` 存的是 Actor **id**，两者都归类 `ActorRef`，但写回 `params` 的值不同（名称 vs id）。下拉列表项需各自携带正确的写回值。

### 2. 分发逻辑统一

把硬编码 `if/else` 改为按引脚 `kind` 分发。命中 `Hit::PinValue` 后，查 `PinDef` 取 `kind`：

| kind | 编辑器 | 选项来源 |
|---|---|---|
| `Bool` | 勾选框：点一下直接在 `true`/`false` 间切换，不弹窗 | — |
| `LevelRef` | 浮动列表选择器 | 枚举 `{project}/Levels/*.level`（去扩展名） |
| `ActorRef` | 浮动列表选择器（复用现有 actorId 逻辑） | 场景 Actor 列表 |
| `UIRef` | 浮动列表选择器（复用现有 uiName 逻辑） | 项目 UI 文件 |
| `WidgetRef` | 浮动列表选择器 | 枚举项目所有 UI 的控件，项显示 `UI名/控件名` |
| `Text` / `Number` | 原地 `QLineEdit`（保持现状） | — |

### 3. 通用列表选择器

现有 `showParamEditPopup`（actorId）与 `showUIAssetPicker`（uiName）是两套近乎重复的浮动 `QFrame`+`QListWidget` 弹窗。合并为一个通用方法：

```cpp
void showListPicker(const QPoint& screenPos,
                    const QString& nodeId, const QString& pinKey,
                    const QList<QPair<QString /*显示文本*/, QString /*写回值*/>>& items);
```

- 复用现有弹窗样式（`#paramEditPopup` / `#uiAssetPopup` 的 QSS）。
- 选中后写回 `node.params[pinKey] = 写回值`，走现有 `commitParamValue` / Undo 命令路径，标脏并刷新。
- 点击弹窗外区域关闭（沿用现有 `mousePress`/`eventFilter` 中弹窗关闭逻辑）。
- 选项来源各 kind 提供一个 `buildItems(kind, node)` 辅助函数：
  - `LevelRef`：列 `Levels/*.level`
  - `ActorRef`：列场景 Actor（区分写回 id 还是 name）
  - `UIRef`：列 UI 文件
  - `WidgetRef`：遍历所有 UI 文件，调用现有 `loadWidgetNames(uiName)` 拼 `UI名/控件名`

旧的 `showParamEditPopup` / `showUIAssetPicker` 内部改为构造 items 后调用 `showListPicker`，或直接删除并由分发逻辑统一调用。

### 4. 绘制

`drawNode` 中按引脚 `kind` 决定参数行外观：

- `Bool`：画一个小方框，`true` 显示 ✔、`false` 显示空框（替代文本值）。
- `LevelRef`/`ActorRef`/`UIRef`/`WidgetRef`：画当前值文本 + 末尾 `▼` 提示「可下拉」。
- `Text`/`Number`：维持现状。

命中检测（`hitTest`）对 `Bool` 勾选框区域仍归为 `Hit::PinValue`，点击直接切换而非弹窗。

## 不在本次范围

- 引脚连线时的类型校验（虚幻有，本项目暂不做）。
- `widgetRef` 不追溯连线上下文确定所属 UI；下拉直接列全项目控件即可。
- 数值引脚不引入数字微调器（spinbox），保持 `QLineEdit`。

## 影响文件

- `launcher/src/editor/BlueprintEditor.h`：新增 `ValueKind` 枚举、`PinDef::kind` 字段、`showListPicker` 等方法声明。
- `launcher/src/editor/BlueprintEditor.cpp`：标注引脚 kind、改分发逻辑、合并弹窗、改绘制与命中检测、Bool 勾选切换。
- 可能涉及 `resources/styles/launcher.qss`：若通用弹窗 objectName 变化需同步选择器。

## 验证

编译后运行，在「单人模式 蓝图」中：
1. 「跳转关卡」的关卡名点开是下拉，列出项目所有关卡，选中后写回。
2. 布尔参数（激活/翻转/可见）显示勾选框，点击切换真假。
3. 跟随目标 / actorId 下拉列出场景 Actor。
4. UI/控件引用下拉可选。
5. 数值/文本参数仍可正常打字。
