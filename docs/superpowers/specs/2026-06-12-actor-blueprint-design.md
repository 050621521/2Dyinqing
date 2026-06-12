# Actor 级蓝图系统设计文档

**日期：2026-06-12**
**状态：设计完成，待实施**

---

## 设计目标

让每个 Actor 实例拥有独立的蓝图逻辑（类似 Unreal Actor Blueprint），蓝图类作为 Actor 的类定义，同时保留现有关卡蓝图不变。

此功能是 UI 系统的前置依赖。

---

## 核心决策

- **蓝图类作为 Actor 的类定义**（Unreal 风格），不采用 Unity 式"结构与行为分离"
- 现有固定 Actor 类型（Camera / Sprite / Light / Trigger / Empty）变为内置蓝图类
- 关卡蓝图保留，与 Actor 蓝图共存，边界靠约定（与 Unreal 一致，不强制）
- 实例存储：`bpClass` 路径 + 所有属性完整快照（不只存 overrides，避免合并逻辑）

---

## 文件结构

```
{project}/
  Levels/
    Level1.level       ← Actor 实例列表（bpClass + 属性全快照）
  Blueprints/
    Player.bp          ← 用户创建的蓝图类
    Enemy.bp

engine/builtin/        ← 内置蓝图类（只读，嵌入引擎资源）
  Sprite.bp
  Camera.bp
  Light.bp
  Trigger.bp
  Empty.bp
```

---

## .bp 文件格式（JSON）

```json
{
  "name": "Player",
  "components": ["精灵渲染器", "碰撞盒"],
  "defaults": {
    "spritePath": "",
    "spriteColor": "#ffffff"
  },
  "nodes": [ /* BPNode 列表，格式与关卡蓝图相同 */ ],
  "connections": [ /* BPConnection 列表 */ ]
}
```

内置蓝图类格式相同，`components` 字段对应原 `ActorTypeUtils.h` 中的默认组件列表。

---

## 关卡文件迁移

`ActorData` 中 `type` 字段改为 `bpClass`，其余属性字段全部保留：

```json
// 迁移前
{ "type": "Sprite", "id": "abc", "name": "Player", "x": 0, "y": 0, "spritePath": "hero.png" }

// 迁移后
{ "bpClass": "builtin/Sprite", "id": "abc", "name": "Player", "x": 0, "y": 0, "spritePath": "hero.png" }
```

映射关系：

| 旧 type | 新 bpClass |
|---|---|
| `"Sprite"` | `"builtin/Sprite"` |
| `"Camera"` | `"builtin/Camera"` |
| `"Light"` | `"builtin/Light"` |
| `"Trigger"` | `"builtin/Trigger"` |
| `"Empty"` | `"builtin/Empty"` |

`LevelDocument::load()` 兼容两种格式（读到 `type` 字段自动转换）。

---

## 新增数据类

### BPClass（src/models/BPClass.h）

```cpp
struct BPClass {
    QString                  name;
    QString                  filePath;   // "" = 内置类
    QStringList              components;
    QMap<QString, QVariant>  defaults;
    QList<BPNode>            nodes;
    QList<BPConnection>      connections;

    bool isBuiltin() const { return filePath.isEmpty(); }
    bool hasNodes()  const { return !nodes.isEmpty(); }

    QJsonObject toJson() const;
    static BPClass fromJson(const QJsonObject& obj, const QString& filePath = {});
};
```

### ActorBPRuntime（src/editor/ActorBPRuntime.h）

```cpp
class ActorBPRuntime : public QObject {
    Q_OBJECT
public:
    explicit ActorBPRuntime(const BPClass* bpClass, ActorData* self,
                            const QList<ActorData>* allActors, QObject* parent = nullptr);

    void triggerBeginPlay();
    void triggerKeyDown(const QString& key);
    // tick() 由外部 QTimer 驱动（与 BPRuntime 共用同一 timer）

signals:
    void selfModified();   // self 属性变化时通知 GameViewport

private:
    QString executeNode(const QString& nodeId);
    QString resolveDataPin(const QString& nodeId, const QString& pinKey);

    const BPClass*           m_bpClass;
    ActorData*               m_self;       // 不拥有，指向共享列表中的元素
    const QList<ActorData>*  m_allActors;  // 用于 Actor 间通信节点
};
```

---

## Runtime 架构

### 执行层次

```
EditorWindow::startRuntime()
  ├── 创建 LevelBPRuntime（关卡蓝图，不变，内部有自己的 QTimer）
  ├── 遍历所有 Actor：
  │     if bpClass.hasNodes() → 创建 ActorBPRuntime(bpClass, &actor, &allActors)
  └── 创建共享 QTimer（16ms），连接到 EditorWindow::onRuntimeTick()

EditorWindow::onRuntimeTick()：
  1. LevelBPRuntime::triggerTick(dt)
  2. for each ActorBPRuntime: triggerTick(dt)
  3. tickComponents(dt)          ← 跟随控制/边界限制等（不变）
  4. emit stateChanged()
```

**指针安全**：`ActorBPRuntime::m_self` 指向运行时 Actor 列表中的元素。运行期间不允许增删 Actor，列表只读，指针始终有效（与现有 `BPRuntime::m_actors` 约束一致）。

### 没有节点的 Actor

`bpClass` 节点图为空时不创建 `ActorBPRuntime`，无额外运行时开销。

---

## Self 节点（第一批）

### 变换节点（所有 Actor 通用）

| typeId | 描述 | 引脚 |
|---|---|---|
| `Self.GetPosition` | 获取自身位置 | 输出：x, y |
| `Self.SetPosition` | 设置自身位置 | 执行，输入：x, y |
| `Self.GetRotation` | 获取自身旋转 | 输出：angle |
| `Self.SetRotation` | 设置自身旋转 | 执行，输入：angle |
| `Self.IsActive` | 获取激活状态 | 输出：bool |
| `Self.SetActive` | 设置激活状态 | 执行，输入：bool |
| `Self.GetName` | 获取 Actor 名称 | 输出：string |

### 精灵渲染器组件节点

| typeId | 描述 | 引脚 |
|---|---|---|
| `Self.Sprite.SetImage` | 替换精灵图片 | 执行，输入：path |
| `Self.Sprite.SetColor` | 设置颜色 | 执行，输入：r, g, b, a |
| `Self.Sprite.SetFlipX` | 水平翻转 | 执行，输入：bool |
| `Self.Sprite.SetFlipY` | 垂直翻转 | 执行，输入：bool |
| `Self.Sprite.SetVisible` | 设置可见性 | 执行，输入：bool |

### 摄像机组件节点

| typeId | 描述 | 引脚 |
|---|---|---|
| `Self.Camera.SetSize` | 设置摄像机尺寸 | 执行，输入：size |
| `Self.Camera.SetBackground` | 设置背景色 | 执行，输入：r, g, b |

**节点过滤规则**：`BlueprintEditor` 打开 `.bp` 文件时读取 `components` 列表，右键菜单只显示该 bp 所含组件对应的 Self 节点。无组件的 Actor 只显示变换节点。

---

## 编辑器改动

### ContentBrowser
- 扫描 `{project}/Blueprints/` 目录，展示 `.bp` 文件
- 双击打开蓝图编辑器（在 DocTabBar 新建 Tab）
- 右键菜单：新建蓝图类（创建空白 `.bp` 文件）
- 内置类在 ContentBrowser 中以只读形式展示（可查看但不可编辑）

### DocTabBar
- 新增 `.bp` 文件 Tab 类型，`tabData()` 存储 `.bp` 文件绝对路径
- 关闭时检查 `isDirty()` 提示保存（与关卡 Tab 逻辑相同）

### BlueprintEditor
- 新增 `loadBpClass(BPClass* bpClass)` 入口（现有 `loadLevel` 保留用于关卡蓝图）
- 页眉显示蓝图类名
- 右键节点菜单：在 Self 分类下按 `components` 列表过滤可用节点

### DetailsPanel
- `type` 字段改为 `bpClass` 只读显示（格式：`builtin/Sprite` 或 `Blueprints/Player.bp`）
- 新增"编辑蓝图"按钮，点击在 ContentBrowser 定位并打开对应 `.bp` 文件

### SceneOutliner
- 无改动

### EditorWindow
- `startRuntime()`：创建 `LevelBPRuntime` 后，遍历 Actor 列表为有节点的 Actor 创建 `ActorBPRuntime`
- `stopRuntime()`：销毁所有 `ActorBPRuntime`

---

## Actor 间通信（第二批，不在本次实施范围内）

设计预留：`ActorBPRuntime` 持有 `const QList<ActorData>* m_allActors` 指针，后续可添加：
- `GetActorByName` 节点 → 返回 Actor 引用
- `CallEvent` 节点 → 触发目标 Actor 蓝图上的自定义事件

---

## 不在本次范围内

- 蓝图类继承
- 自定义函数/宏
- `Blueprints/` 子目录支持
- 内置蓝图类的可视化编辑（只读展示）
