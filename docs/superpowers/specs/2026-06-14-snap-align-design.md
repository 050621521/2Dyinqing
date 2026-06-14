# 视口 & UI编辑器辅助功能设计规格

**日期**：2026-06-14  
**范围**：Viewport2D、UIEditorCanvas、EditorWindow 工具栏、UIEditor 工具栏  
**参考**：虚幻引擎 Level Editor 工具栏 + UMG 对齐工具栏

---

## 一、多选（两个编辑器共用逻辑）

### 数据结构变更

| 位置 | 旧 | 新 |
|---|---|---|
| `Viewport2D` | `QString m_selectedId` | `QSet<QString> m_selectedIds` |
| `UIEditorCanvas` | `QString m_selectedId` | `QSet<QString> m_selectedIds` |

### 交互规则

| 操作 | 结果 |
|---|---|
| 点击空白 | 清空选区 |
| 点击对象 | 单选（清空其他） |
| Ctrl + 点击对象 | 切换该对象的选中状态 |
| 在空白区域拖拽 | 框选：拖拽时显示半透明蓝色矩形，松开时选中矩形内所有对象 |

### 信号变更（Viewport2D）

- **新增** `selectionChanged(QStringList ids)` — 选区变化时触发，供 SceneOutliner、DetailsPanel 响应
- **保留** `actorSelected(ActorData)` — 仅单选时触发，维持现有流程兼容性
- **新增** `actorsAligned(QList<ActorData>)` — 批量对齐后触发，EditorWindow 批量写入 doc

### 绘制

**Viewport2D**：
- 每个选中 Actor 显示黄色边框
- 多选（≥2）时，额外绘制整体 bounding box（白色虚线）
- 框选拖拽进行时，绘制半透明蓝色矩形覆盖层

**UIEditorCanvas**：
- 每个选中控件显示蓝色边框
- 多选时绘制整体 bounding box（白色虚线）

### DetailsPanel 多选状态

多选时显示「已选中 N 个对象」占位文字，不展示属性字段（同虚幻行为）。

---

## 二、吸附（Snap）

### Viewport2D 网格吸附

**新增成员变量**：
```cpp
bool  m_gridSnapEnabled = true;
float m_gridSnapSize    = 50.0f;   // 世界单位
bool  m_rotSnapEnabled  = true;
float m_rotSnapAngle    = 15.0f;   // 度
```

**Move 模式**：鼠标移动时对世界坐标取整：
```cpp
if (m_gridSnapEnabled) {
    pos.rx() = std::round(pos.x() / m_gridSnapSize) * m_gridSnapSize;
    pos.ry() = std::round(pos.y() / m_gridSnapSize) * m_gridSnapSize;
}
```

**Rotate 模式**：旋转角度取整：
```cpp
if (m_rotSnapEnabled)
    angle = std::round(angle / m_rotSnapAngle) * m_rotSnapAngle;
```

**EditorWindow 视口工具栏**（在现有选择/移动/旋转/缩放按钮之后新增）：

| 控件 | 类型 | 选项 |
|---|---|---|
| 网格吸附开关 | QToolButton（可选中） | 默认 ON |
| 网格大小 | QComboBox | 25 / 50 / 100 / 250（world units） |
| 旋转吸附开关 | QToolButton（可选中） | 默认 ON |
| 旋转角度 | QComboBox | 5° / 15° / 45° / 90° |

开关按下时对应 QComboBox disabled。

**公共接口**（供 EditorWindow 设置）：
```cpp
void setGridSnap(bool enabled, float size);
void setRotSnap(bool enabled, float angle);
```

### UIEditorCanvas 像素吸附

**新增成员变量**：
```cpp
bool m_pixelSnapEnabled = true;
int  m_snapGrid         = 1;   // 像素，可选 1/5/10
```

拖拽控件时，位置坐标取整：
```cpp
if (m_pixelSnapEnabled) {
    x = std::round(x / m_snapGrid) * m_snapGrid;
    y = std::round(y / m_snapGrid) * m_snapGrid;
}
```

**UIEditor 顶部工具栏**新增：
- 像素吸附开关按钮（默认 ON）
- 网格下拉：`1px / 5px / 10px`

---

## 三、对齐工具

### Viewport2D 对齐（EditorWindow 工具栏）

多选（≥2 个 Actor）时，视口工具栏右侧对齐按钮组 **enabled**，单选/无选时 **disabled**。

| 按钮 | 图标方向 | 逻辑 |
|---|---|---|
| 左对齐 | ⬛⬛→\| | 所有 Actor.x = 选区最小 X |
| 右对齐 | \|←⬛⬛ | 所有 Actor.x = 选区最大 X |
| 水平居中 | ⬛\|⬛ | 所有 Actor.x = bounding box 中心 X |
| 上对齐 | ⬛⬛↓— | 所有 Actor.y = 选区最小 Y |
| 下对齐 | —↑⬛⬛ | 所有 Actor.y = 选区最大 Y |
| 垂直居中 | ⬛—⬛ | 所有 Actor.y = bounding box 中心 Y |

对齐完成后发出 `actorsAligned(QList<ActorData>)` → EditorWindow 批量调用 `doc->updateActor()` 并标记脏。

### UIEditor 对齐工具栏

UIEditor 顶部独立工具栏（一直可见，无选或单选时 disabled）。

**对齐（6 个，≥2 个控件时 enabled）**：

| 按钮 | 逻辑 |
|---|---|
| 左对齐 | 所有控件 x = 选区最小 x |
| 水平居中 | 所有控件 x = bounding box 中心 x - width/2 |
| 右对齐 | 所有控件 x = 选区最大 (x + width) - 自身 width |
| 上对齐 | 所有控件 y = 选区最小 y |
| 垂直居中 | 所有控件 y = bounding box 中心 y - height/2 |
| 下对齐 | 所有控件 y = 选区最大 (y + height) - 自身 height |

**分布（2 个，≥3 个控件时 enabled）**：

| 按钮 | 逻辑 |
|---|---|
| 水平等间距 | 按 x 排序后，令相邻控件间距相等（总宽固定） |
| 垂直等间距 | 按 y 排序后，令相邻控件间距相等（总高固定） |

**统一尺寸（2 个，≥2 个控件时 enabled）**：

| 按钮 | 逻辑 |
|---|---|
| 等宽 | 所有控件 width = 第一个选中控件的 width |
| 等高 | 所有控件 height = 第一个选中控件的 height |

对齐/分布/统一尺寸完成后，UIEditorCanvas 调用 `onWidgetMoved` 回调更新 doc 并触发 `documentModified`。

---

## 四、文件改动清单

| 文件 | 改动 |
|---|---|
| `Viewport2D.h / .cpp` | 多选数据结构、框选绘制、snap 逻辑、align 信号、公共接口 |
| `UIEditorCanvas`（UIEditor.h/.cpp） | 多选数据结构、框选绘制、像素 snap、新增 align/distribute/resize 方法 |
| `UIEditor.h / .cpp` | 顶部对齐工具栏、吸附控件、多选状态联动 |
| `EditorWindow.h / .cpp` | 视口工具栏扩展（snap 控件 + 对齐按钮组）、`actorsAligned` 槽、`selectionChanged` 槽 |
| `DetailsPanel.h / .cpp` | 多选时显示「已选中 N 个对象」占位文字 |
| `SceneOutliner.h / .cpp` | 响应 `selectionChanged` 高亮多个条目 |
