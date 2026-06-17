# UI 编辑器辅助功能设计（参考虚幻 UMG）

日期：2026-06-17

## 目标

给 UI 编辑器（`UIEditor` / `UIEditorCanvas`）增加四类虚幻 UMG 风格的编辑辅助功能：

1. 智能对齐参考线（拖动/缩放时自动吸附并高亮对齐线）
2. 标尺 + 可拖拽辅助线（Guide）
3. 锚点 / 边距可视化（Anchor Medallion 风格）
4. 间距 / 尺寸测量提示

吸附目标包含：**其它 UI 控件**、**画布边界与中心线**、**背景场景物体**、**主摄像机可视区域边框**、**用户手动放置的 Guide**。

## 现状

- 画布为固定世界坐标系，视口恒为 `(0,0,canonicalW,canonicalH)`（设计分辨率，默认 1920×1080）。
- 已有：背景网格、缩放百分比显示、像素吸附（按固定网格 round）、框选、缩放手柄、缩放/平移、撤销重做、复制粘贴、对齐/分布/等尺寸。
- 坐标转换：`screenToCanvas` / `worldRectToScreen` / `screenOrigin`（含 zoom + pan）。
- 背景预览：`drawScenePreview` 用 `cameraWorldToScreen` 把场景 Actor 世界坐标投影到画布世界坐标后绘制。
- **尚无**：针对其它对象的智能对齐参考线、标尺、锚点可视化、测量提示。
- 拖动位置更新注入点：`UIEditorCanvas::mouseMoveEvent`（当前约 769–794 行的 `newX/newY` 计算 + 网格 round）。缩放有对应 resize 分支。

## 代码组织（方案 A）

- 新建模块 `launcher/src/editor/UISnapGuides.{h,cpp}`：纯逻辑吸附引擎，不依赖 QPainter，只产出几何数据。需同步加入 `launcher/CMakeLists.txt` 的 `SOURCES` / `HEADERS`。
- 标尺、锚点可视化、测量提示作为 `UIEditorCanvas` 私有绘制方法（`drawRulers` / `drawAnchorBadge` / `drawMeasureHints` / `drawSnapGuides`），沿用现有 `drawScenePreview` 写法。
- 四个功能复用同一份"候选线收集"逻辑（来自 `UISnapGuides`）。

理由：吸附数学独立、可测试；画布不会因此爆炸；避免引入透明 overlay 控件带来的 zoom/pan 同步复杂度。

## 第 1 节：`UISnapGuides` 吸附引擎

### 数据结构

```cpp
struct SnapLine {
    enum Kind { Widget, Canvas, Scene, Camera, Guide };  // 决定颜色/样式
    bool    vertical;       // true=竖线(对齐x)  false=横线(对齐y)
    double  pos;            // 世界坐标值(竖线是x, 横线是y)
    Kind    kind;
    double  spanLo, spanHi; // 这条线在另一轴上的覆盖范围(只画相关线段，不贯穿全屏)
};

struct SnapResult {
    double  dx = 0, dy = 0;          // 应施加到拖动矩形的吸附修正量
    bool    snappedX = false, snappedY = false;
    QVector<SnapLine> activeLines;   // 命中、需要高亮绘制的线
};
```

### 接口与逻辑

- `void rebuild(...)`：在**拖动/缩放开始时**调用一次，收集所有候选线（排除正在拖的控件本身）：
  - **Widget**：每个其它控件世界矩形的左/中/右（竖）、上/中/下（横），共 6 条候选值。
  - **Canvas**：`0, W/2, W`（竖）+ `0, H/2, H`（横），可选四分之一线 `W/4, 3W/4`、`H/4, 3H/4`。
  - **Scene**：每个场景 Actor 投影到画布世界后的矩形（用 `cameraWorldToScreen` 算 `pos ± 尺寸`）的边/中。
  - **Camera**：主摄像机可视区域边框矩形的边/中。
  - **Guide**：用户手动放置的辅助线（见第 2 节）。
- `SnapResult snap(QRectF movingRect, double zoom)`：每次 `mouseMove` 调用。
  - **吸附阈值用屏幕像素恒定**（默认 8px），换算成世界值 `= 8 / zoom`，保证任何缩放下手感一致。
  - 对矩形的左/中/右各自找最近候选竖线，取最近且在阈值内的一条得 `dx`；横向同理得 `dy`。

### 接入点

`UIEditorCanvas` 持有 `UISnapGuides m_snap` 与 `QVector<SnapLine> m_activeGuides`：

- `mousePressEvent`（拖动/缩放开始）→ `m_snap.rebuild(...)`。
- `mouseMoveEvent`（769–794 行）→ 算完 `newX/newY` 后调用 `snap()`，把 `dx/dy` 叠加进去，把 `activeLines` 存入 `m_activeGuides`。**吸附命中时用吸附值，否则才走原有像素网格 round**。
- `paintEvent` → 新增 `drawSnapGuides(p)`，在世界变换内画高亮线，按 kind 上色：控件=青、画布=品红、场景=橙、摄像机=黄、Guide=绿；线宽 `1/zoom`。
- `mouseReleaseEvent` → 清空 `m_activeGuides`。

### 视觉

吸附线只画 `spanLo→spanHi` 覆盖段（虚幻风格的局部对齐线），不贯穿全屏；命中时给轻微高亮。缩放（resize）时吸附被拖动的那条边，逻辑相同。

## 第 2 节：标尺 + 拖拽辅助线

### 标尺

- 画布上边缘、左边缘各画 ~20px 刻度条（屏幕空间，不进世界变换）。
- 刻度按世界坐标标注（0、100、200… 设计像素），间隔随 zoom 自适应（在 50/100/200/500 中取合适一档）。
- 鼠标移动时，标尺上画跟随的指示刻线，实时反映光标世界坐标。
- 左上角交点画小方块按钮，点击切换标尺显隐。

### 拖拽辅助线（Guide）

- 从横标尺往下拖 → 水平 Guide；从竖标尺往右拖 → 垂直 Guide。
- 存 `QVector<double> m_guidesX, m_guidesY`（世界坐标）。
- Guide 并入第 1 节吸附候选（`Kind::Guide`，绿色），控件可吸附到手动参考线。
- 把 Guide 拖回标尺区域 = 删除；hover 到 Guide 上光标变双向箭头可再拖动。
- Guide 只存内存（随编辑器会话），**不持久化进 `.ui` 文档**，避免污染数据层。

## 第 3 节：锚点 / 边距可视化

选中**单个**控件时，在画布上叠加：

- **锚点标记**：根据控件锚点（现有 `AnchorPicker` 的"左上/居中/右下…"）在父容器对应位置画小花瓣/十字标记。
- **边距虚线 + 数值**：从控件四边到父容器（或画布）对应边画虚线，标注 4 个边距像素值。
- 仅单选时显示；多选不画。

## 第 4 节：间距 / 尺寸测量提示

- **尺寸**：拖动或缩放时，在控件旁实时显示 `宽×高`（世界像素），移动时显示当前 `x, y`。
- **间距测量**：选中一个控件后，鼠标**悬停**到另一控件上，在两者间画双向箭头并标注水平/垂直间距数值。
- 数值标签画在屏幕空间小药丸背景上，保证任何 zoom 下清晰可读。

## 第 5 节：开关与默认值

- 工具栏（现有像素吸附按钮旁）加 **"辅助"下拉菜单**，勾选项：智能吸附✓、标尺✓、锚点边距✓、测量提示✓（默认全开）。
- 状态存内存，不持久化。

## 实现顺序建议

1. `UISnapGuides` 模块 + 智能对齐参考线（核心，其它功能复用候选线收集）。
2. 测量提示（复用拖动/选中状态）。
3. 锚点 / 边距可视化。
4. 标尺 + 拖拽辅助线（Guide 接入吸附候选）。
5. "辅助"下拉开关。

## 非目标（YAGNI）

- Guide 与辅助开关状态不持久化进文档或项目配置。
- 不引入透明 overlay 控件架构。
- 多选时不绘制锚点/边距。
