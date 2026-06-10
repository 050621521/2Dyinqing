# Blueprint Tab Undock Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复将"关卡蓝图"标签页向下拖出时面板位置错误、不跟随鼠标、松手后状态泄漏三个 bug。

**Architecture:** 仅修改 `EditorWindow::eventFilter` 中与 docTabBar 拖拽相关的代码段：① 修正坐标系计算；② 新增 undock 后的面板拖动代理块；③ 补全 Release 时的状态清理。

**Tech Stack:** C++17, Qt6 Widgets, CMake

---

### Task 1：修正 undock 触发时的坐标计算

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp:422`

- [ ] **Step 1：定位目标行**

打开 `launcher/src/editor/EditorWindow.cpp`，找到第 422 行：

```cpp
QRect  cg = centralWidget() ? centralWidget()->geometry() : rect();
```

这一行在 `eventFilter` 的 docTabBar MouseMove 分支内，紧接在 `m_docTabBar->removeTab(oldTabIdx)` 的 `if` 块之后。

- [ ] **Step 2：替换坐标计算**

将第 422 行替换为：

```cpp
QRect cg = centralWidget()
    ? QRect(centralWidget()->mapTo(this, QPoint(0, 0)), centralWidget()->size())
    : rect();
```

`mapTo(this, QPoint(0,0))` 把 centralWidget 的左上角映射到 `this`（EditorWindow）坐标系，与 `mapFromGlobal` 使用相同的原点，消除坐标系不一致导致的 clamp 错误。

- [ ] **Step 3：确认上下文完整**

修改后该段代码应为：

```cpp
QPoint globalPos = me->globalPosition().toPoint();
QRect cg = centralWidget()
    ? QRect(centralWidget()->mapTo(this, QPoint(0, 0)), centralWidget()->size())
    : rect();
QPoint panelPos  = mapFromGlobal(globalPos) - QPoint(m_bpPanel->width() / 2, 15);
panelPos.setX(qBound(cg.left(),  panelPos.x(), cg.right()  - m_bpPanel->width()));
panelPos.setY(qBound(cg.top(),   panelPos.y(), cg.bottom() - m_bpPanel->height()));
m_bpPanel->move(panelPos);
m_bpDragging   = true;
m_bpDragOffset = globalPos - m_bpPanel->mapToGlobal(QPoint(0, 0));
```

- [ ] **Step 4：编译确认无错误**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

Expected: `[100%] Built target launcher`，无编译错误。

---

### Task 2：新增 undock 后的面板拖动代理块

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp:399`（在现有 docTabBar 拖拽块之前插入）

- [ ] **Step 1：定位插入位置**

找到第 400 行的注释：

```cpp
    // 蓝图标签页向下拖出（drag-to-undock）
    if (obj == m_docTabBar && m_bpDocked && m_bpTabIndex >= 0) {
```

在这两行**之前**（即第 399 行之后，`}` 闭合上一个 if 块之后）插入新代码块。

- [ ] **Step 2：插入代理拖动块**

在注释 `// 蓝图标签页向下拖出（drag-to-undock）` 之前插入：

```cpp
    // undock 后鼠标捕获仍在 docTabBar，此块代理面板拖动直到松手
    if (obj == m_docTabBar && m_bpDragging && !m_bpDocked && m_bpPanel) {
        if (e->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(e);
            QPoint globalPos = me->globalPosition().toPoint();
            QPoint newPos = mapFromGlobal(globalPos) - m_bpDragOffset;
            QRect cg = centralWidget()
                ? QRect(centralWidget()->mapTo(this, QPoint(0, 0)), centralWidget()->size())
                : rect();
            newPos.setX(qBound(cg.left(), newPos.x(), qMax(cg.left(), cg.right()  - m_bpPanel->width())));
            newPos.setY(qBound(cg.top(),  newPos.y(), qMax(cg.top(),  cg.bottom() - m_bpPanel->height())));
            m_bpPanel->move(newPos);
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_bpDragging = false;
        }
    }

```

条件 `!m_bpDocked` 保证只在 undock 后、标题栏拖动之前的这段"过渡期"生效，不干扰正常的标题栏拖动逻辑。

- [ ] **Step 3：确认插入位置正确**

插入后，该区域的代码顺序应为：

```
// ... （蓝图标题栏拖拽 if 块结束的 }）

    // undock 后鼠标捕获仍在 docTabBar，此块代理面板拖动直到松手
    if (obj == m_docTabBar && m_bpDragging && !m_bpDocked && m_bpPanel) {
        ...
    }

    // 蓝图标签页向下拖出（drag-to-undock）
    if (obj == m_docTabBar && m_bpDocked && m_bpTabIndex >= 0) {
        ...
    }
```

- [ ] **Step 4：编译确认**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

Expected: `[100%] Built target launcher`，无编译错误。

---

### Task 3：修复 Release 路径的状态泄漏

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp`（原 docTabBar undock 块内的 Release 处理）

- [ ] **Step 1：定位 Release 处理**

在 `if (obj == m_docTabBar && m_bpDocked && m_bpTabIndex >= 0)` 块的末尾找到：

```cpp
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_bpTabDragging = false;
        }
```

- [ ] **Step 2：补全状态清理**

将上述代码改为：

```cpp
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_bpTabDragging = false;
            m_bpDragging = false;
        }
```

这处理"按下 tab 但未拖出就松手"的情况，防止 `m_bpDragging` 因任何边缘情况留下 true 状态。

- [ ] **Step 3：编译**

```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) 2>&1 | tail -5
```

Expected: `[100%] Built target launcher`

- [ ] **Step 4：提交**

```bash
cd /Users/kwy/Documents/2Dyinqing
git add launcher/src/editor/EditorWindow.cpp
git commit -m "fix: blueprint tab undock panel position and drag follow"
```

---

### Task 4：验证行为

- [ ] **Step 1：启动应用**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; open /Users/kwy/Documents/2Dyinqing/launcher/build/launcher.app
```

- [ ] **Step 2：验证用例 1 — 面板出现位置正确**

操作：打开一个关卡 → 点击"关卡蓝图"按钮使其以浮动面板出现 → 将其拖入 Tab 栏停靠 → 再向下拖拽该 Tab 触发 undock

期望：面板出现在鼠标位置附近（中央区域内），而非视口底部。

- [ ] **Step 3：验证用例 2 — 松手前面板跟随鼠标**

操作：拖拽 Tab 超过阈值后，不松手，继续在屏幕上移动鼠标

期望：面板跟随鼠标移动，不在初始位置停滞。

- [ ] **Step 4：验证用例 3 — 松手后状态干净**

操作：undock 后松手 → 再次点击面板标题栏并拖动

期望：面板平滑跟随，不出现瞬间跳位。

- [ ] **Step 5：验证回归 — 原有停靠功能正常**

操作：将浮动面板拖回 Tab 栏顶部区域（ghost tab 出现后松手）

期望：面板正常停靠为标签页，逻辑与修复前一致。
