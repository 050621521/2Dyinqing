# 运行时输入与编辑器快捷键隔离 — 设计

日期: 2026-06-20

## 背景与问题

运行游戏时,按键蓝图节点(如「按键 W 按下 → 设置进度值」)完全不触发。

经真实集成测试(构造真实 `EditorWindow`、加载用户工程、程序化点运行并投递 W 键、抓帧数像素)定位到根因:

`EditorWindow` 构造时把场景工具快捷键注册为**窗口级** `QShortcut`:

```cpp
auto* wSc = new QShortcut(QKeySequence("W"), this);  // 移动工具
```

Qt 中快捷键优先级高于控件 `keyPressEvent`。即便这些快捷键的 lambda 内有 `if (m_centralStack->currentIndex() == 0)` 守卫(只在场景视口 tab 才切工具),**QShortcut 一旦匹配就会消费按键**。于是运行时(游戏视图 tab)按 W 被「W=移动工具」吃掉,传不到 `GameViewport`,`triggerKeyDown` 永不触发。

集成测试对照证据:
- 现状:`keyPressed` 信号未发出,进度条像素 4664 → 4664(不变)。
- 禁用窗口级 "W" 快捷键后:`keyPressed` 发出 "W",进度条 4664 → 2332(正好一半,即 value 0.5 生效)。

影响范围不止进度条:任何用 `Q/W/E/R/F/Esc/Delete/退格` 当游戏键的逻辑,在游戏视图里全部失灵。

## 目标

运行时游戏视图能收到全部键盘按键;编辑时所有编辑器快捷键行为不变。

参照虚幻 PIE 的思路:编辑器命令绑定到编辑器视口的焦点上下文,运行中的游戏视口独占输入,二者天然隔离。

## 涉及的快捷键

只有**无修饰单键**会与游戏键冲突。带修饰键的(Ctrl+S 保存、撤销/重做、Ctrl+A/C/V/D)不冲突,保持不动。

冲突单键分两类,采用不同隔离手段:

### 第一类:纯场景视口工具键 Q / W / E / R —— 作用域绑定(方案 B)
这些键语义上只属于场景编辑视口(切换 选择/移动/旋转/缩放 工具)。
改为:
```cpp
auto* wSc = new QShortcut(QKeySequence("W"), m_viewport);
wSc->setContext(Qt::WidgetWithChildrenShortcut);
```
仅当 `m_viewport`(场景视口)或其子控件获得键盘焦点时才触发。游戏视图、蓝图视图聚焦时这些键不再被消费,直达游戏。

前提已确认:`setupCentralArea()`(创建 `m_viewport`)在快捷键注册之前执行,`m_viewport` 此时已存在。

### 第二类:跨上下文单键 F / Esc / Delete / 退格 —— 运行时挂起(方案 A)
这些键在蓝图编辑器/浮动蓝图窗口/UI 编辑器等多个上下文都要用(F 居中节点、Esc 关弹窗、Delete/退格 删节点),不能死绑到场景视口。
改为:把这 4 个 `QShortcut*` 存入成员列表 `m_editorSingleKeyShortcuts`,在运行生命周期开关:
- `startRuntime()`:遍历列表 `setEnabled(false)`。
- `stopRuntime()`:遍历列表 `setEnabled(true)`。

运行时这些键交给游戏;停止后编辑器恢复。

## 实现要点

1. 在 `EditorWindow` 头文件新增成员:`QList<QShortcut*> m_editorSingleKeyShortcuts;`
2. 构造函数中:
   - Q/W/E/R:`new QShortcut(key, m_viewport)` + `setContext(Qt::WidgetWithChildrenShortcut)`。
   - F/Esc/Delete/Backspace:创建后 `m_editorSingleKeyShortcuts << sc;`(F 仍保留其现有 `ApplicationShortcut` 上下文与路由逻辑不变,只是加入挂起列表)。
3. `startRuntime()` 末尾:禁用列表中全部快捷键。
4. `stopRuntime()`:恢复列表中全部快捷键。

不改动任何蓝图运行时、UIRuntime、渲染逻辑——它们已验证正确。

## 验证

复用集成测试 `launcher/uitest/integration_test.cpp`(真实 EditorWindow + 用户工程):
- 运行 → 投递 W 键(不再手动禁用快捷键)→ 进度条像素应从满变半(value 0.5 生效)。
- 验证 Q/W/E/R 在场景视口聚焦时仍能切工具。
- 验证停止运行后,F/Esc/Delete 在编辑器里恢复可用。

## 影响面与风险

- 风险低:仅改快捷键的注册上下文与运行期开关,不触碰核心逻辑。
- 行为变化:Q/W/E/R 现在需要场景视口获得焦点才生效(此前为全窗口)。这与虚幻一致,且符合直觉(在场景里操作时才切工具)。

## 不做(YAGNI)

- 不改带修饰键的快捷键。
- 不引入全局输入处理器/事件过滤器(挂起+作用域已足够)。
- 不做可配置的按键绑定系统。
