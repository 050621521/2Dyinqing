# UI 系统设计文档

**状态：设计完成，等待 Actor 级蓝图完成后实施**
**日期：2026-06-11**

---

## 前置依赖

**UI 系统的实现依赖 Actor 级蓝图系统先完成。**

当前引擎的蓝图是关卡级别（一个关卡一个蓝图），但 UI 系统需要每个 Actor 有独立的蓝图实例（等价于 Unity 的 Prefab + MonoBehaviour）。Actor 级蓝图设计在独立对话中进行，完成后再开始 UI 系统实施。

---

## 设计目标

在编辑器中直接看到 UI 与游戏背景的关系（解决 Unity Canvas 编辑时背景是灰色、难以判断 UI 和场景关系的问题）。

---

## 架构方案

**选定方案：独立 UIDocument 系统（Unreal Widget Blueprint 风格）**

- `UIDocument`（`.ui` 文件）与 `LevelDocument`（`.level` 文件）平级，独立存储
- `.ui` 文件是模板，蓝图运行时动态实例化（类似 Unity Prefab 实例化）
- 可被多个关卡复用
- 文件路径：`{project}/UI/*.ui`

排除方案：
- 方案一（扁平列表 + 排序层）：无法实现父子嵌套，布局系统无法实现
- 方案二（ActorData 加 parentId）：上限够用，但 UI 和游戏对象共用树状结构，长期维护成本高

---

## 新增组件

### UIDocument（新增）
```
src/models/UIDocument.h/.cpp
存储路径：{project}/UI/*.ui（JSON 格式）
```

### UIEditor（新增）
```
src/editor/UIEditor.h/.cpp
```
- 可视化控件画布 + 控件树面板
- 背景显示当前选定的关卡场景预览（底部下拉选择关卡）
- 双击 ContentBrowser 中的 .ui 文件打开
- 在 DocTabBar 中与关卡 Tab 并列显示

### UIRuntime（新增）
```
src/editor/UIRuntime.h/.cpp
```
- 管理所有激活的 UIDocument 实例
- 处理 UI 事件（点击、悬停）

### 修改现有组件
- `DetailsPanel`：选中 UIWidget 时显示 UI 属性
- `ContentBrowser`：识别并显示 `.ui` 文件，支持双击打开
- `GameViewport`：渲染完场景后，叠加渲染 UIRuntime 中所有激活的 UI 实例
- `BPRuntime`：新增 UI 蓝图节点的执行逻辑

---

## 数据模型

### UIWidget
```cpp
struct UIWidget {
    // 通用
    QString id;
    QString name;        // 中文名称，如"血量条"、"开始按钮"
    QString type;        // 见下方控件类型列表
    QString parentId;    // 空字符串 = 根节点
    bool    visible = true;
    float   alpha   = 1.0f;

    // 布局（屏幕空间）
    float   x, y;        // 相对父节点偏移（以锚点为基准）
    float   width, height;
    QString anchor;      // "左上"/"居中"/"右下" 等 9 个预设

    // 类型专属属性
    QString text;
    int     fontSize  = 16;
    QColor  color;           // 前景色
    QColor  bgColor;         // 背景色
    QString imagePath;
    bool    nineSlice = false;
    float   value     = 1.0f;  // 进度条 0~1
    QColor  fillColor;
    int     spacing   = 4;     // VBox/HBox 子间距
    int     columns   = 4;     // 网格列数
    int     cellW, cellH;
    int     selectedIndex = 0; // 下拉菜单选中项
};
```

### UIDocument
```cpp
class UIDocument {
    bool load(const QString& filePath);
    bool save();
    bool isDirty() const;

    const QList<UIWidget>& widgets() const;
    QList<UIWidget> rootWidgets() const;
    QList<UIWidget> childrenOf(const QString& parentId) const;

    void addWidget(const UIWidget&);
    void removeWidget(const QString& id);
    void updateWidget(const UIWidget&);
};
```

---

## 控件类型（type 字段，全部中文）

| 分类 | type 值 | 说明 |
|---|---|---|
| 布局容器 | `UI.面板` | 通用容器，可设背景色/图 |
| 布局容器 | `UI.竖向布局` | 子控件自动竖排 |
| 布局容器 | `UI.横向布局` | 子控件自动横排 |
| 布局容器 | `UI.网格布局` | 按列数排列子控件（背包格子） |
| 布局容器 | `UI.滚动视图` | 内容超出时可滚动 |
| 显示 | `UI.文本` | 文字标签 |
| 显示 | `UI.图片` | 图片/图标，支持九宫格模式 |
| 显示 | `UI.进度条` | 血量/经验值条 |
| 交互 | `UI.按钮` | 可点击，蓝图绑定事件 |
| 交互 | `UI.下拉菜单` | 选项列表 |

后续扩展：富文本、开关、滑块、输入框、CanvasGroup、Tooltip

---

## 锚点系统

9 个锚点预设（中文）：左上、正上、右上、左中、居中、右中、左下、正下、右下

控件的 x/y 是相对锚点的偏移，换分辨率后 UI 不乱跑。

---

## 蓝图节点（新增）

### 实例管理
| 节点名 | 类型 | 说明 |
|---|---|---|
| 创建UI | 动作 | 输入 UI文件名，实例化 .ui 模板，返回实例引用（类似 Unity Instantiate） |
| 显示UI | 动作 | 将实例叠加显示在 GameViewport 上方 |
| 隐藏UI | 动作 | 从屏幕移除，实例保留 |
| 销毁UI | 动作 | 释放实例（Actor 销毁时调用） |
| UI引用 | 变量 | 存储 UIDocument 实例引用 |

### 数据操作（创建后赋值，不在创建时传参）
| 节点名 | 类型 | 说明 |
|---|---|---|
| 设置文本 | 动作 | 输入：实例引用 + 控件名 + 文本内容 |
| 设置进度值 | 动作 | 输入：实例引用 + 控件名 + 0~1 数值 |
| 设置位置 | 动作 | 移动整个 UI 实例的屏幕坐标 |
| 设置可见 | 动作 | 显示/隐藏实例内某个具体控件 |

### 事件
| 节点名 | 类型 | 说明 |
|---|---|---|
| 按钮点击时 | 事件 | 监听指定按钮的点击，触发执行链 |
| 下拉选项改变时 | 事件 | 下拉菜单选中项变化，输出选中索引 |

### 典型用法（怪物血量条）
```
Actor 级蓝图 OnBeginPlay：
  创建UI("怪物血量条") → 存入 UI引用变量

Actor 级蓝图 OnTick：
  设置进度值(UI引用, "血量条", 当前HP / 最大HP)
  设置位置(UI引用, 怪物屏幕坐标.x, 怪物屏幕坐标.y - 30)

Actor 级蓝图 OnDestroy：
  销毁UI(UI引用)
```

---

## 运行时渲染顺序

```
GameViewport::paintEvent
  1. 渲染游戏场景（摄像机裁切，世界坐标）
  2. 遍历 UIRuntime 中所有激活实例，按屏幕坐标叠加渲染
```

UI 始终在游戏画面最上层，忽略摄像机变换。

---

## 编辑器体验要点

- UIEditor 底部有"背景预览"下拉，可选择任意关卡作为背景参考
- 控件树显示完整父子层级
- DetailsPanel 复用现有实现，新增 UIWidget 属性支持
- DocTabBar 新增 `.ui` 文件 Tab 类型（`tabData()` 为 `.ui` 文件绝对路径）

---

## 待实施前提

1. **Actor 级蓝图系统**：UIRuntime 和蓝图节点（创建UI、销毁UI、设置位置）依赖每个 Actor 有独立的蓝图实例才能正确工作
2. Actor 级蓝图完成后，本文档可直接用于编写实施计划
