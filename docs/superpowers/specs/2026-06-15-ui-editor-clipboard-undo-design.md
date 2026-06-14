# UI 编辑器剪贴板与撤销重做设计规格

**日期**：2026-06-15  
**状态**：已批准

---

## 背景

UI 编辑器画布目前缺少：
- 复制 / 粘贴 / 原位复制快捷键
- 任何形式的撤销 / 重做支持

本规格补全上述功能，使 UI 编辑器的交互体验与视口、蓝图编辑器对齐。

---

## 一、快捷键

| 快捷键 | 行为 |
|---|---|
| Cmd+C | 复制主选中控件（含完整子树）到内存剪贴板 |
| Cmd+V | 粘贴剪贴板内容（见粘贴目标逻辑） |
| Cmd+D | 原位复制：在同一父节点下直接创建副本，不经过剪贴板 |
| Ctrl+Z | 撤销（已有全局 QShortcut，补接 UIDocument undo stack 即生效） |
| Ctrl+Shift+Z | 重做（同上） |

---

## 二、剪贴板

### 数据存储

`UIEditor` 持有：

```cpp
QList<UIWidget> m_clipboard; // 存储被复制控件的完整子树，root 在 index 0
```

### 复制（Cmd+C）

- 仅对主选中控件（`m_selectedId`）生效，多选暂不处理
- 收集该控件及其所有后代，按 BFS/DFS 顺序存入 `m_clipboard`
- **保留原始 ID**，不在复制时生成新 ID

### 粘贴（Cmd+V）

1. `m_clipboard` 为空则返回
2. 克隆整棵子树：为每个 UIWidget 生成新 UUID，建立旧→新 ID 映射表，重映射所有 `parentId`
3. 确定粘贴目标父节点（UE UMG 规则）：
   - 当前 `m_selectedId` 对应的控件是**容器型**（`UI.面板` / `UI.竖向布局` / `UI.横向布局` / `UI.网格布局` / `UI.滚动视图`）→ `parentId = m_selectedId`
   - 否则（叶子控件或无选中）→ `parentId = ""`（根层级）
4. 将克隆后子树的根节点位置 +10/+10，子孙节点位置保持相对不变
5. 以 `UIWidgetAddCmd` 推入 undo stack（整棵子树作为一条命令），同时在文档中 `addWidget` 每个节点
6. 选中新粘贴的根节点

### 原位复制（Cmd+D）

- 不使用剪贴板，直接克隆当前选中控件的子树
- 粘贴目标固定为**原控件的同一父节点**（`parentId` 不变）
- 位置偏移 +10/+10
- 同样推入 `UIWidgetAddCmd` 到 undo stack

---

## 三、撤销 / 重做系统

### UIDocument 增加 QUndoStack

与 `LevelDocument` 完全平行：

```cpp
// UIDocument.h
QUndoStack* undoStack();
~UIDocument();

// UIDocument.cpp
QUndoStack* UIDocument::undoStack() {
    if (!m_undoStack) m_undoStack = new QUndoStack();
    return m_undoStack;
}
```

### UIEditor 新增 setUndoStack 接口

```cpp
void UIEditor::setUndoStack(QUndoStack* stack, std::function<void()> refresh);
```

内部保存 `m_undoStack` 和 `m_onRefresh`，所有操作改为 push 命令。

### EditorWindow 接入

`onTabChanged` 处理 `.ui` tab 时（当前设为 `nullptr`）：

```cpp
m_activeUndoStack = doc->undoStack();
m_uiEditor->setUndoStack(doc->undoStack(), uiRefresh);
```

`uiRefresh` lambda：重建控件树、刷新属性面板、更新 tab 标题和保存标签。

---

## 四、Undo 命令类（新增到 UndoCommands.h）

### UIWidgetAddCmd

覆盖：添加控件、粘贴、原位复制。

```
redo: 按顺序 doc->addWidget(w) for each w in subtree
undo: doc->removeWidget(subtree[0].id)  // removeWidget 已递归删子孙
```

```cpp
class UIWidgetAddCmd : public QUndoCommand {
    UIDocument*           m_doc;
    QList<UIWidget>       m_subtree;   // root 在 index 0
    std::function<void()> m_refresh;
};
```

### UIWidgetRemoveCmd

覆盖：删除控件（含子孙还原）。

```
redo: doc->removeWidget(m_subtree[0].id)
undo: 按顺序 doc->addWidget(w) for each w in m_subtree
```

```cpp
class UIWidgetRemoveCmd : public QUndoCommand {
    UIDocument*           m_doc;
    QList<UIWidget>       m_subtree;   // 删除前保存，root 在 index 0
    std::function<void()> m_refresh;
};
```

### UIWidgetModifyCmd

覆盖：属性面板所有字段修改（名称、位置、尺寸、颜色、文字、字号等）、图片拖入、锚点更改。

```
redo: doc->updateWidget(m_after)
undo: doc->updateWidget(m_before)
```

```cpp
class UIWidgetModifyCmd : public QUndoCommand {
    UIDocument*           m_doc;
    UIWidget              m_before, m_after;
    std::function<void()> m_refresh;
};
```

### UIWidgetMoveCmd

覆盖：画布拖动位置、画布拖动缩放（支持多选批量移动）。

**批量提交策略**（与 Viewport2D 的 ActorTransformCmd 相同）：
- `UIEditorCanvas::mousePressEvent` 开始拖动/缩放时，快照当前所有选中控件的状态到 `m_dragBeforeWidgets`
- `UIEditorCanvas::mouseReleaseEvent` 结束时，比较前后，若有变化则 push 一条 `UIWidgetMoveCmd`
- 拖动过程中的中间状态直接 `doc->updateWidget()` 不经 undo stack（视觉流畅，只有最终结果进历史）

```cpp
class UIWidgetMoveCmd : public QUndoCommand {
    UIDocument*           m_doc;
    QList<UIWidget>       m_before;   // 拖动前快照
    QList<UIWidget>       m_after;    // 拖动后快照
    std::function<void()> m_refresh;
};
```

### 对齐 / 分布操作

使用 `beginMacro("对齐") / endMacro()` 包裹多个 `UIWidgetModifyCmd`，保持原子性。

---

## 五、需要修改的文件

| 文件 | 修改内容 |
|---|---|
| `UndoCommands.h` | 新增 4 个 UI 命令类 |
| `UIDocument.h / .cpp` | 添加 `QUndoStack`、析构、`undoStack()` 方法 |
| `UIEditor.h` | 新增 `setUndoStack()`、`copySelected()`、`paste()`、`duplicateSelected()` 公有方法；新增 `m_undoStack`、`m_onRefresh`、`m_clipboard`、`m_dragBeforeWidgets` 成员 |
| `UIEditor.cpp` | 所有 `doc->addWidget/removeWidget/updateWidget` 改为 push 命令；画布回调加拖动快照批量提交；实现 copy/paste/duplicate |
| `EditorWindow.cpp` | `.ui` tab 的 `m_activeUndoStack = nullptr` 改为接入 `doc->undoStack()`；Ctrl+D 快捷键 index 3 分支补上 `m_uiEditor->duplicateSelected()`；新增 Cmd+C/Cmd+V 快捷键 |

---

## 六、不在本次范围内

- 多选复制（Cmd+C 复制多个控件）
- 跨 UIDocument 粘贴（不同 .ui 文件间）
- 系统剪贴板（跨进程粘贴）
