# 快捷键 & 撤销/重做系统设计规格

**日期**：2026-06-15  
**状态**：已批准，待实现

---

## 目标

参考虚幻引擎，为编辑器补全常用快捷键，并引入基于 `QUndoStack` 的撤销/重做系统。

---

## 一、快捷键清单

### 视口（`Viewport2D` 获焦时，编辑器模式下）

| 按键 | 功能 |
|---|---|
| Q | 切换到选择工具 |
| W | 切换到移动工具 |
| E | 切换到旋转工具 |
| R | 切换到缩放工具 |
| F | 聚焦选中对象（视口居中并缩放至选中 Actor） |
| Delete / Backspace | 删除选中 Actor |
| Ctrl+D | 原位复制选中 Actor（偏移 20px） |
| Ctrl+A | 全选所有 Actor |
| Escape | 取消选中 |

> **注意**：Q/W/E/R 只在 `Viewport2D` 有焦点且当前 Tab 为视口页时响应，避免在蓝图文本输入框中触发。

### 蓝图编辑器（`BlueprintEditor` 获焦时）

| 按键 | 功能 |
|---|---|
| Delete / Backspace | 删除选中节点（及其所有关联连线） |
| Ctrl+D | 复制选中节点（偏移 20px，新节点获得新 id） |
| Ctrl+A | 全选所有节点 |
| F | 适配视图（所有节点缩放到可见区域） |

### 全局（`EditorWindow` 级别）

| 按键 | 功能 |
|---|---|
| Ctrl+S | 保存当前关卡（已有，保留） |
| Ctrl+Z | 撤销 |
| Ctrl+Shift+Z / Ctrl+Y | 重做 |

---

## 二、撤销/重做架构

### 2.1 撤销栈分布

```
EditorWindow
  ├── QUndoStack* m_activeUndoStack   ← 指向当前活跃栈，随 Tab 切换更新
  │
  ├── m_openLevels[path] → LevelDocument
  │                            └── QUndoStack* undoStack()   ← Actor 操作历史
  │
  └── BlueprintEditor
           └── QUndoStack* m_undoStack   ← 蓝图节点/连线操作历史
```

- 每个 `LevelDocument` 持有独立 `QUndoStack`，切换关卡 Tab 不丢历史。
- `BlueprintEditor` 持有独立 `QUndoStack`，切换蓝图目标时 `clear()`。
- `Ctrl+Z/Y` 统一路由到 `m_activeUndoStack`，无需各子组件各自处理。

### 2.2 命令类（新建 `src/editor/UndoCommands.h`）

#### Actor 操作命令

```cpp
// 创建 Actor
class ActorAddCmd : public QUndoCommand {
    LevelDocument* doc;
    ActorData      data;
    // undo: doc->removeActor(data.id)
    // redo: doc->addActor(data)
};

// 删除 Actor（支持多选批量删除，用 QUndoCommand 的 child commands 机制）
class ActorRemoveCmd : public QUndoCommand {
    LevelDocument* doc;
    ActorData      data;   // 保存完整字段，undo 时可完整还原
    // undo: doc->addActor(data)
    // redo: doc->removeActor(data.id)
};

// 变换（移动/旋转/缩放）
class ActorTransformCmd : public QUndoCommand {
    LevelDocument*    doc;
    QList<QString>    ids;
    QList<ActorData>  before;   // 拖拽开始时快照
    QList<ActorData>  after;    // 鼠标抬起时快照
    // undo: 逐一 doc->updateActor(before[i])
    // redo: 逐一 doc->updateActor(after[i])
};

// 属性修改（细节面板）
class ActorModifyCmd : public QUndoCommand {
    LevelDocument* doc;
    ActorData      before;
    ActorData      after;
    // undo/redo: doc->updateActor(before/after)
};
```

#### 蓝图操作命令

```cpp
class BPNodeAddCmd      : public QUndoCommand { BPNode node; ... };
class BPNodeRemoveCmd   : public QUndoCommand {
    BPNode                  node;
    QList<BPConnection>     relatedConns;  // 同步删除/恢复相关连线
};
class BPConnectionAddCmd    : public QUndoCommand { BPConnection conn; ... };
class BPConnectionRemoveCmd : public QUndoCommand { BPConnection conn; ... };
```

所有命令类执行 `undo()`/`redo()` 后，通过 `doc` 指针直接操作数据层，视口/蓝图画布通过已有信号槽自动刷新。

### 2.3 拖拽变换的批量合并

拖拽过程中 `updateActor()` 高频调用，策略：

1. **`mousePressEvent`**：若工具为 Move/Rotate/Scale 且命中 Actor，快照选中 Actor 的变换到 `m_dragStartActors`。
2. **拖拽中**：只调用 `doc->updateActor()`，不 push 命令。
3. **`mouseReleaseEvent`**：比较最终状态与 `m_dragStartActors`，若有变化则 push 一条 `ActorTransformCmd`。

### 2.4 Tab 切换时的栈切换

```
切换到关卡 Tab  → m_activeUndoStack = currentLevelDoc()->undoStack()
切换到蓝图 Tab  → m_activeUndoStack = m_blueprintEditor->undoStack()
切换到游戏视图  → m_activeUndoStack = nullptr（Ctrl+Z 无效）
切换到 UI 编辑器 → m_activeUndoStack = nullptr（本期暂不支持）
```

---

## 三、复制/粘贴缓冲区

### 视口 Actor

- `EditorWindow` 持有 `QList<ActorData> m_actorClipboard`。
- `Ctrl+C`（本期不加，视口无此需求）暂不实现；`Ctrl+D` 直接原位复制，不经剪贴板。
- `Ctrl+D` 实现：复制选中 Actor 的 `ActorData`，生成新 id，位置偏移 `(20, 20)` 世界坐标，push `ActorAddCmd`。

### 蓝图节点

- `BlueprintEditor` 持有 `QList<BPNode> m_nodeClipboard`。
- `Ctrl+D`：复制选中节点，生成新 id，画布坐标偏移 `(20, 20)`，push `BPNodeAddCmd`。

---

## 四、影响文件

| 文件 | 改动类型 |
|---|---|
| `src/editor/UndoCommands.h`（新建） | 全部 8 个命令类声明与实现（header-only） |
| `src/models/LevelDocument.h/.cpp` | 加 `QUndoStack* m_undoStack`，暴露 `undoStack()` getter |
| `src/editor/EditorWindow.h/.cpp` | Ctrl+Z/Y 路由；Tab 切换更新 `m_activeUndoStack`；`m_actorClipboard` |
| `src/editor/Viewport2D.h/.cpp` | Q/W/E/R/F/Delete/Ctrl+A/D/Escape；拖拽前后快照；操作改走命令 |
| `src/editor/SceneOutliner.cpp` | Delete 改走 `ActorRemoveCmd` |
| `src/editor/BlueprintEditor.h/.cpp` | Delete/Ctrl+A/D/F；节点连线操作改走命令；`m_undoStack`；`m_nodeClipboard` |
| `launcher/CMakeLists.txt` | 加 `UndoCommands.h`（header-only，仅需确认 include 路径） |

---

## 五、本期不做

- UI 编辑器控件的撤销（UI 编辑器复杂度较高，单独排期）
- 快捷键自定义（.ini 映射）
- Ctrl+C / Ctrl+V 跨会话复制粘贴（Ctrl+D 已覆盖核心需求）
