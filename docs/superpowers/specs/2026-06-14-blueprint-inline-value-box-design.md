# 蓝图引脚值常驻输入框设计

**日期**：2026-06-14  
**状态**：已批准

## 背景

当前蓝图编辑器中，未连线的数据输入引脚值以纯文本显示，用户需要单击才能知道该区域可以编辑。虚幻引擎的做法是：未连线时始终显示一个可见的输入框外观，让用户一眼就能看出这里可以填值，并看到当前值是什么。

## 目标

将未连线数据输入引脚的值区域从「纯文本」改为「常驻输入框外观」，交互逻辑保持不变（单击激活真正的 QLineEdit 进行编辑）。

## 范围

- **仅影响** `BlueprintEditor::drawNode()` 中普通数据输入引脚（`!pd.isExec && !pd.isOutput && !connected`）的值绘制逻辑
- **不影响** `actorId`（Actor 选择器）、`uiName`（UI 资产选择器）这两个特殊引脚，它们用弹窗交互
- **不影响** 交互逻辑，单击 `Hit::PinValue` 激活 `showInlineEdit()` 的机制不变

## 方案：视觉常驻 + 单击激活

### 绘制逻辑（`drawNode()` 中）

对每个满足条件的数据输入引脚，在原来绘制纯文本的位置改为：

1. **计算值框矩形**，与 `showInlineEdit()` 中 QLineEdit 的 geometry 对齐：
   - `x0 = pinCenter.x() + 12.0 * zoom`（pin 圆心右侧 12px 屏幕空间）
   - `x1 = tl.x() + nodeWidth - 6.0`
   - `y = rowY + 2`，`height = rowH - 4`

2. **绘制背景矩形**：
   - 填充色：`#1c2d3e`
   - 边框色：`#2a5070`，线宽 1px
   - 圆角：2px

3. **绘制值文字**（在矩形内右对齐）：
   - 有值时：`#5a9fd4`，显示实际值
   - 无值时：`#555555`，显示 `···`

### 字号

与当前一致，跟随 zoom 缩放（`qMax(7.0, 8.5 * zoom)` pt）。

### QLineEdit 样式（不变）

`showInlineEdit()` 中 QLineEdit 的样式与上面保持一致：  
`background: #1c2d3e; color: #5a9fd4; border: 1px solid #2a5070; border-radius: 2px`

点击激活时 QLineEdit 精确覆盖在绘制的背景矩形上，实现无缝过渡。

## 实现文件

- `launcher/src/editor/BlueprintEditor.cpp`：修改 `drawNode()` 中值区域绘制段落（约第 858–868 行）

## 不在本次范围内

- 为不同 pin 类型（数字/布尔/颜色）适配不同输入控件（数字滑条、勾选框、颜色选择器）——保留为后续迭代
- 持久化真实 QLineEdit 控件（方案 B）
