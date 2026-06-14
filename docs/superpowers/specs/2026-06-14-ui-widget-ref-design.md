# UI 控件引用系统设计

**日期**：2026-06-14  
**状态**：已批准，待实现

---

## 目标

将蓝图中的 UI 控件访问方式从"字符串名查找"改为"强类型连线引用"，对齐虚幻引擎 UMG 的设计哲学：控件本身是可连线的引用，不需要手填名字。

---

## 现状

`UI.Ref` 只有一个固定输出引脚 `uiRef`，所有操作节点（设置文本、按钮点击时等）用两个引脚定位控件：

```
UI.Ref → uiRef ──▶ 设置文本.uiRef
                    设置文本.widgetName = "文本"  ← 手填字符串
```

问题：widgetName 手填，容易拼错，无提示，无法连线。

---

## 新设计

### UI.Ref 节点变为动态多引脚

选好 UI 文件后，根据该文件的控件树动态生成输出引脚：

```
┌─────────────────────┐
│  UI引用             │
│  [游戏HUD ▾]        │   ← 选择器按钮（保持不变）
│                     ├──── UI引用 ──▶ UI.Show / UI.Hide / UI.Destroy
│                     ├──── 按钮 ────▶ 按钮点击时.widgetRef
│                     ├──── 文本 ────▶ 设置文本.widgetRef
│                     └──── 面板 ────▶ 设置控件可见.widgetRef
└─────────────────────┘
```

- **`UI引用` 引脚**（固定）：供 UI.Show / UI.Hide / UI.Destroy 使用，返回 UI 文件名
- **控件引脚**（动态）：每个控件一个引脚，引脚 key = 控件名，返回 `"uiName::widgetName"` 格式的字符串

### 5 个操作节点简化引脚

| 节点 | 改前 | 改后 |
|------|------|------|
| 设置文本 | uiRef + widgetName + text | **widgetRef** + text |
| 设置进度值 | uiRef + widgetName + value | **widgetRef** + value |
| 设置控件可见 | uiRef + widgetName + visible | **widgetRef** + visible |
| 按钮点击时 | uiRef + widgetName + exec_out | **widgetRef** + exec_out |
| 下拉选项改变时 | uiRef + widgetName + exec_out + index | **widgetRef** + exec_out + index |

UI.Show / UI.Hide / UI.Destroy / UI.Create 的引脚**不变**（操作整个 UI，不涉及具体控件）。

---

## 数据格式

### widgetRef 编码

控件引脚的输出值格式：`"<uiName>::<widgetName>"`

例：`"游戏HUD::按钮"`

运行时按 `::` 分割得到 uiName 和 widgetName，分别用于查找 UI 实例和控件。

### BPConnection 兼容性

连线存储的 fromPin 直接使用控件名（如 `"按钮"`）。若控件被重命名，连线断开——与虚幻行为一致，属于预期行为。

---

## 实现变更范围

### 1. BlueprintEditor.cpp — 渲染层

**节点高度**（`nodeHeight`）：
- UI.Ref：`kHeaderH + kRowH * (1 + 1 + widgetCount)`
  - 1 行选择器按钮
  - 1 行固定 `UI引用` 引脚
  - N 行动态控件引脚

**引脚位置**（`pinCenter`）：
- UI.Ref 特殊分支：先处理固定 `uiRef` 引脚（row 0），再处理动态引脚（row 1..N）
- 所有引脚位置相对选择器按钮行下移

**绘制**（`drawNode`）：
- 选择器按钮行不变
- 之后按序绘制 `UI引用` 固定引脚 + 动态控件引脚（方形数据引脚，右侧，标签 = 控件名）

**命中检测**（`hitTest`）：
- UI.Ref 特殊分支：逐行匹配，固定行返回 `pinKey="uiRef"`，动态行返回 `pinKey=widgetName`

**控件名加载**：
- BlueprintEditor 新增 `QMap<QString, QStringList> m_uiWidgetCache`
- 新增 `QStringList loadWidgetNames(const QString& uiName)`：从 `m_projectRoot/UI/<uiName>.ui` 读取 UIDocument 并返回所有控件名
- 选择 UI 文件时清除该 UI 对应的缓存（`m_uiWidgetCache.remove(uiName)`），触发重新加载

### 2. BlueprintEditor.cpp — 节点定义（`nodeDefs()`）

`UI.Ref` 静态定义只保留固定引脚：
```cpp
{"UI.Ref", "UI引用", QColor("#4a1a6a"),
    {{"uiRef", "UI引用", false, true}}  // 仅固定引脚
}
```
动态控件引脚在渲染/命中检测时实时读取，不存入 NodeDef。

5 个操作节点的 `widgetName` 引脚替换为 `widgetRef`：
```cpp
{"widgetRef", "控件引用", false, false}  // 输入，数据引脚
```

移除这 5 个节点原有的 `uiRef` 和 `widgetName` 引脚。

### 3. BlueprintEditor.cpp — 线拖放弹窗（`showWireDropPopup`）

从 `widgetRef` 引脚拖出连线时，弹窗过滤仅显示上述 5 个操作节点，方便连接。

### 4. BPRuntime.cpp — resolveOutputPin

```cpp
if (node->type == "UI.Ref") {
    const QString uiName = node->params.value("uiName");
    if (pinKey == "uiRef") return uiName;          // 固定引脚：返回 UI 名
    return uiName + "::" + pinKey;                 // 控件引脚：返回 "uiName::widgetName"
}
```

### 5. BPRuntime.cpp — executeNode

5 个操作节点改为解析 `widgetRef`：

```cpp
// 工具函数（内部）
auto splitWidgetRef = [](const QString& ref) -> std::pair<QString,QString> {
    int sep = ref.indexOf("::");
    if (sep < 0) return {ref, {}};
    return {ref.left(sep), ref.mid(sep + 2)};
};

// 设置文本示例：
if (node->type == "UI.SetText") {
    auto [uiName, widgetName] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
    if (m_uiRuntime)
        m_uiRuntime->setTextByName(uiName, widgetName, resolveDataPin(nodeId, "text"));
    return "exec_out";
}
```

### 6. BPRuntime.cpp — triggerButtonClick / triggerDropdownChanged

匹配逻辑改为解析 `widgetRef`：

```cpp
void BPRuntime::triggerButtonClick(const QString& instanceId, const QString& widgetName) {
    QString uiName = /* 从 UIRuntime 查 instanceId 对应的 uiName */;
    for (const BPNode& node : m_nodes) {
        if (node.type != "UI.OnButtonClick") continue;
        auto [refUiName, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUiName == instanceId || refUiName == uiName) && refWidget == widgetName) {
            executeChain(node.id, "exec_out");
        }
    }
}
```

### 7. ActorBPRuntime.cpp — 同步修改

与 BPRuntime 相同的 5 个节点引脚变更 + widgetRef 解析逻辑。

---

## 不在本次范围内

- `UI.Create` 动态输出控件引脚（动态创建的 UI 实例的控件访问留待后续）
- 控件重命名后自动更新连线
- 连线类型校验（widgetRef 引脚只接受控件引脚输出）

---

## 成功标准

1. UI.Ref 选好 UI 文件后，出现每个控件对应的输出引脚
2. 将控件引脚连到「设置文本」节点，运行时文本正确更新
3. 将控件引脚连到「按钮点击时」，运行时点击按钮触发执行链
4. 没有任何地方需要手填控件名字符串
