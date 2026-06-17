# UI 编辑器辅助功能 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 UI 编辑器加入虚幻 UMG 风格的四类编辑辅助：智能对齐参考线、标尺+拖拽辅助线、锚点/边距可视化、间距/尺寸测量提示。

**Architecture:** 新建纯逻辑模块 `UISnapGuides` 收集吸附候选线（UI 控件/画布/场景/摄像机/Guide）并计算吸附修正；`UIEditorCanvas` 增加 `drawSnapGuides`/`drawRulers`/`drawAnchorBadge`/`drawMeasureHints` 等私有绘制方法与状态；`UIEditor` 工具栏加"辅助"下拉开关。四功能复用同一份候选线收集。

**Tech Stack:** C++17、Qt6 Widgets、CMake（`CMAKE_AUTOMOC ON`）。

## Global Constraints

- 与用户沟通用中文术语；UI 控件类型名形如 `UI.面板/UI.文本/UI.按钮…`。
- 修改 `.cpp/.h/.qrc/.qss` 后必须重新编译再运行，验证命令见下方"构建与验证"。
- 新增 `.cpp/.h` 必须同步更新 `launcher/CMakeLists.txt` 的 `SOURCES`（第 23 行起）与 `HEADERS`（第 56 行起）。
- 画布为固定世界坐标系，视口恒为 `(0,0,m_canonicalW,m_canonicalH)`；坐标转换用现有 `screenToCanvas`/`worldRectToScreen`/`screenOrigin`/`cameraWorldToScreen`。
- 吸附阈值用屏幕像素恒定（默认 8px），世界阈值 = `8.0 / m_zoom`。
- Guide 与辅助开关状态仅存内存，不持久化。
- 无单元测试框架：每个任务的验证 = 编译成功 + 运行 App + 肉眼观察指定行为。

### 构建与验证（每个任务的"运行验证"都用这条）

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

进入方式：打开任一项目 → 切到 UI 编辑器页（中央 Tab）。

---

### Task 1: `UISnapGuides` 模块骨架 + 接入编译

**Files:**
- Create: `launcher/src/editor/UISnapGuides.h`
- Create: `launcher/src/editor/UISnapGuides.cpp`
- Modify: `launcher/CMakeLists.txt:53`（SOURCES 加一行）、`launcher/CMakeLists.txt:87`（HEADERS 加一行）

**Interfaces:**
- Produces: `struct SnapLine { enum Kind{Widget,Canvas,Scene,Camera,Guide}; bool vertical; double pos; Kind kind; double spanLo, spanHi; };`
- Produces: `struct SnapResult { double dx=0, dy=0; bool snappedX=false, snappedY=false; QVector<SnapLine> activeLines; };`
- Produces: `class UISnapGuides { void clear(); void addLine(const SnapLine&); SnapResult snap(const QRectF& movingRect, double worldThreshold) const; const QVector<SnapLine>& candidates() const; };`

- [ ] **Step 1: 写 `UISnapGuides.h`**

```cpp
#pragma once
#include <QVector>
#include <QRectF>

struct SnapLine {
    enum Kind { Widget, Canvas, Scene, Camera, Guide };
    bool   vertical = true;   // true=竖线(吸附x)  false=横线(吸附y)
    double pos = 0;           // 世界坐标值
    Kind   kind = Widget;
    double spanLo = 0, spanHi = 0; // 另一轴覆盖范围（只画相关段）
};

struct SnapResult {
    double dx = 0, dy = 0;
    bool   snappedX = false, snappedY = false;
    QVector<SnapLine> activeLines;
};

// 纯逻辑吸附引擎：不依赖 QPainter，只产出几何数据。
class UISnapGuides {
public:
    void clear();
    void addLine(const SnapLine& line);
    // movingRect 为拖动后的世界矩形；worldThreshold = 屏幕阈值 / zoom。
    SnapResult snap(const QRectF& movingRect, double worldThreshold) const;
    const QVector<SnapLine>& candidates() const { return m_lines; }

private:
    QVector<SnapLine> m_lines;
};
```

- [ ] **Step 2: 写 `UISnapGuides.cpp`**

```cpp
#include "editor/UISnapGuides.h"
#include <cmath>

void UISnapGuides::clear() { m_lines.clear(); }
void UISnapGuides::addLine(const SnapLine& line) { m_lines.append(line); }

SnapResult UISnapGuides::snap(const QRectF& r, double thr) const {
    SnapResult res;
    // 候选锚点：矩形左/中/右(竖)、上/中/下(横)
    const double vx[3] = { r.left(), r.center().x(), r.right() };
    const double hy[3] = { r.top(),  r.center().y(), r.bottom() };

    double bestVX = thr; double bestHY = thr;
    for (const SnapLine& ln : m_lines) {
        if (ln.vertical) {
            for (double x : vx) {
                double d = std::abs(ln.pos - x);
                if (d < bestVX) {
                    bestVX = d; res.dx = ln.pos - x; res.snappedX = true;
                }
            }
        } else {
            for (double y : hy) {
                double d = std::abs(ln.pos - y);
                if (d < bestHY) {
                    bestHY = d; res.dy = ln.pos - y; res.snappedY = true;
                }
            }
        }
    }
    // 收集所有命中阈值的线用于高亮（用吸附后的最终坐标判定）
    if (res.snappedX || res.snappedY) {
        const QRectF snapped = r.translated(res.dx, res.dy);
        const double sx[3] = { snapped.left(), snapped.center().x(), snapped.right() };
        const double sy[3] = { snapped.top(),  snapped.center().y(), snapped.bottom() };
        for (const SnapLine& ln : m_lines) {
            bool hit = false;
            if (ln.vertical && res.snappedX)
                for (double x : sx) if (std::abs(ln.pos - x) < 0.5) hit = true;
            if (!ln.vertical && res.snappedY)
                for (double y : sy) if (std::abs(ln.pos - y) < 0.5) hit = true;
            if (hit) res.activeLines.append(ln);
        }
    }
    return res;
}
```

- [ ] **Step 3: 接入 CMakeLists**

在 `launcher/CMakeLists.txt` SOURCES 列表内 `src/editor/UIEditor.cpp` 后加一行 `src/editor/UISnapGuides.cpp`；HEADERS 列表内 `src/editor/UIEditor.h` 后加一行 `src/editor/UISnapGuides.h`。

- [ ] **Step 4: 编译验证**

Run（CMakeLists 改了会自动 reconfigure）：
```bash
cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu)
```
Expected: 编译成功，无报错（此时还没人调用，行为不变）。

- [ ] **Step 5: 提交**

```bash
git add launcher/src/editor/UISnapGuides.h launcher/src/editor/UISnapGuides.cpp launcher/CMakeLists.txt
git commit -m "feat(ui-aids): 新增 UISnapGuides 吸附引擎骨架"
```

---

### Task 2: 智能对齐参考线（UI 控件 + 画布）

**Files:**
- Modify: `launcher/src/editor/UIEditor.h`（UIEditorCanvas 私有成员/方法）
- Modify: `launcher/src/editor/UIEditor.cpp`（include、rebuild 候选、mouseMove 接入、paint 绘制）

**Interfaces:**
- Consumes: `UISnapGuides::snap`、`SnapLine`、`SnapResult`（Task 1）
- Produces: `void UIEditorCanvas::rebuildSnapCandidates();`（成员）、`void drawSnapGuides(QPainter&) const;`、成员 `UISnapGuides m_snap; QVector<SnapLine> m_activeGuides; bool m_aidSnap = true;`

- [ ] **Step 1: 头文件加成员与方法**

在 `UIEditor.h` 顶部 include 区加 `#include "editor/UISnapGuides.h"`。在 `UIEditorCanvas` private 区（`m_pixmapCache` 附近）加：
```cpp
    // ── 智能对齐参考线 ──
    UISnapGuides       m_snap;
    QVector<SnapLine>  m_activeGuides;
    bool               m_aidSnap = true;
    void rebuildSnapCandidates();           // 拖动/缩放开始时收集候选
    void drawSnapGuides(QPainter& p) const; // 世界变换内绘制高亮线
public:
    void setAidSnap(bool on) { m_aidSnap = on; update(); }
```

- [ ] **Step 2: 实现 `rebuildSnapCandidates`（先做 Widget + Canvas）**

在 `UIEditor.cpp` 适当位置（如 `worldRectOf` 附近）实现。`getViewportRect()` 给画布矩形；遍历控件用 `resolveRect(w.id)` 取世界矩形，排除当前选区。
```cpp
void UIEditorCanvas::rebuildSnapCandidates() {
    m_snap.clear();
    if (!m_doc) return;
    const QRectF vp = getViewportRect();
    auto pushRect = [&](const QRectF& r, SnapLine::Kind k) {
        const double vs[3] = { r.left(), r.center().x(), r.right() };
        const double hs[3] = { r.top(),  r.center().y(), r.bottom() };
        for (double x : vs) m_snap.addLine({true,  x, k, r.top(),  r.bottom()});
        for (double y : hs) m_snap.addLine({false, y, k, r.left(), r.right()});
    };
    // 画布边界 + 中心
    pushRect(vp, SnapLine::Canvas);
    // 其它控件（排除选区）
    for (const UIWidget& w : m_doc->widgets()) {
        if (m_selectedIds.contains(w.id)) continue;
        pushRect(resolveRect(w.id), SnapLine::Widget);
    }
}
```

- [ ] **Step 3: 拖动开始处调用 rebuild**

在 `mousePressEvent` 命中控件、设置 `m_dragging = true` 的那一处之后，加 `if (m_aidSnap) rebuildSnapCandidates();`。

- [ ] **Step 4: mouseMove 接入吸附（769–794 行那段）**

单选分支：算出 `newX/newY` 后、调用 `onWidgetMoved` 前，插入：
```cpp
m_activeGuides.clear();
if (m_aidSnap) {
    // 候选线是世界矩形坐标(resolveRect)，所以 movingRect 也要用世界矩形。
    // 当前世界矩形 sel；本帧位移量 = (newX-startX, newY-startY)（同 UI 单位=世界单位）。
    QRectF sel = resolveRect(m_selectedId);
    QPointF start = m_dragStartPositions.value(m_selectedId, {});
    QRectF mr(sel.left() + (newX - start.x()), sel.top() + (newY - start.y()),
              sel.width(), sel.height());
    SnapResult sr = m_snap.snap(mr, 8.0 / m_zoom);
    newX += (float)sr.dx; newY += (float)sr.dy;
    m_activeGuides = sr.activeLines;
} else if (m_pixelSnapEnabled) {
    newX = std::round(newX / m_snapGrid) * m_snapGrid;
    newY = std::round(newY / m_snapGrid) * m_snapGrid;
}
```
> 说明：吸附开启时优先吸附（吃掉网格 round）；关闭时退回原像素网格逻辑。movingRect 必须用 `resolveRect`（世界矩形）系，与候选线同坐标系——`newX/newY` 是控件相对父锚点的偏移，本帧位移量 `newX-start.x()` 才是世界位移。多选分支用选区 bounding box 作 movingRect，吸附一次得 `dx/dy` 统一加到每个控件。

- [ ] **Step 5: paintEvent 调用 drawSnapGuides + 实现**

在 `paintEvent` 世界变换 `p.save()`…`p.restore()` 区间内、控件绘制之后加 `drawSnapGuides(p);`。实现：
```cpp
void UIEditorCanvas::drawSnapGuides(QPainter& p) const {
    if (m_activeGuides.isEmpty()) return;
    auto colorOf = [](SnapLine::Kind k) -> QColor {
        switch (k) {
            case SnapLine::Widget: return QColor(0, 220, 220);
            case SnapLine::Canvas: return QColor(230, 0, 200);
            case SnapLine::Scene:  return QColor(255, 150, 0);
            case SnapLine::Camera: return QColor(255, 220, 0);
            case SnapLine::Guide:  return QColor(0, 220, 0);
        }
        return Qt::white;
    };
    for (const SnapLine& ln : m_activeGuides) {
        p.setPen(QPen(colorOf(ln.kind), 1.0 / m_zoom));
        if (ln.vertical) p.drawLine(QPointF(ln.pos, ln.spanLo), QPointF(ln.pos, ln.spanHi));
        else             p.drawLine(QPointF(ln.spanLo, ln.pos), QPointF(ln.spanHi, ln.pos));
    }
}
```

- [ ] **Step 6: mouseRelease 清理**

在 `mouseReleaseEvent` 结束拖动分支（`m_dragging = false` 处）加 `m_activeGuides.clear(); update();`。

- [ ] **Step 7: 编译运行验证**

运行"构建与验证"命令。观察：拖动一个控件靠近另一个控件或画布中心时，出现青色/品红高亮对齐线并吸附；松手后线消失。

- [ ] **Step 8: 提交**

```bash
git add launcher/src/editor/UIEditor.h launcher/src/editor/UIEditor.cpp
git commit -m "feat(ui-aids): 智能对齐参考线（控件+画布吸附）"
```

---

### Task 3: 吸附扩展到背景场景物体 + 摄像机区域

**Files:**
- Modify: `launcher/src/editor/UIEditor.cpp`（`rebuildSnapCandidates` 增加 Scene/Camera 候选）

**Interfaces:**
- Consumes: `rebuildSnapCandidates`（Task 2）、`cameraWorldToScreen`、`m_level`、`ActorData`

- [ ] **Step 1: 在 `rebuildSnapCandidates` 末尾追加场景/摄像机候选**

复用 `drawScenePreview` 的投影方式：找主摄像机，画布矩形 `camRect=(0,0,canonicalW,canonicalH)` 即摄像机可视区域；每个场景 Actor 投影中心点 ± 半尺寸成矩形。
```cpp
    if (m_level) {
        const QList<ActorData>& actors = m_level->sortedActors();
        const ActorData* cam = nullptr;
        for (const ActorData& a : actors)
            if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"))) { cam = &a; break; }
        if (cam) {
            const QRectF camRect(0, 0, m_canonicalW, m_canonicalH);
            pushRect(camRect, SnapLine::Camera);  // pushRect 需提升为成员或在此重写
            const float aspect = cam->cameraResH > 0 ? (float)cam->cameraResW / cam->cameraResH : 1.7778f;
            const float scale  = qMin((float)camRect.width()/(cam->cameraSize*aspect*2.0f),
                                      (float)camRect.height()/(cam->cameraSize*2.0f));
            for (const ActorData& a : actors) {
                if (!a.active) continue;
                if (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件")) continue;
                const QPointF c = cameraWorldToScreen({a.x, a.y}, camRect, *cam);
                float half = qMax(24.0f, 40.0f * scale) * 0.5f;
                pushRect(QRectF(c.x()-half, c.y()-half, half*2, half*2), SnapLine::Scene);
            }
        }
    }
```
> 实现注意：`pushRect` 在 Task 2 是局部 lambda，本 Task 把它提取为文件内静态 helper 或 lambda 复用，避免重复。摄像机区域当前等于画布矩形，仍单独标 `Camera` 以便上色区分（黄）。

- [ ] **Step 2: 编译运行验证**

运行"构建与验证"。先在底部"背景预览"下拉选一个含摄像机+精灵的关卡，再拖动 UI 控件靠近某个背景精灵中心或摄像机边框，观察橙色（场景）/黄色（摄像机）对齐线吸附。

- [ ] **Step 3: 提交**

```bash
git add launcher/src/editor/UIEditor.cpp
git commit -m "feat(ui-aids): 吸附扩展到背景场景物体与摄像机区域"
```

---

### Task 4: 间距 / 尺寸测量提示

**Files:**
- Modify: `launcher/src/editor/UIEditor.h`（成员 `bool m_aidMeasure`、方法 `drawMeasureHints`、hover 状态）
- Modify: `launcher/src/editor/UIEditor.cpp`（mouseMove 记录 hover、paint 绘制）

**Interfaces:**
- Produces: `void drawMeasureHints(QPainter& p) const;`、成员 `bool m_aidMeasure = true; QString m_hoverId;`、`void setAidMeasure(bool on)`

- [ ] **Step 1: 头文件加成员**

```cpp
    bool    m_aidMeasure = true;
    QString m_hoverId;                       // 选中态下悬停的另一控件
    void drawMeasureHints(QPainter& p) const;
public:
    void setAidMeasure(bool on) { m_aidMeasure = on; update(); }
```

- [ ] **Step 2: mouseMove 记录 hover（非拖动、有选中时）**

在 `mouseMoveEvent` 顶部"悬停时更新缩放光标"那段附近，加：选中单个控件且未拖动时，用 `hitTest` 找鼠标下控件，若不是选中控件则记入 `m_hoverId` 否则清空，`update()`。

- [ ] **Step 3: 实现 drawMeasureHints（屏幕空间药丸标签）**

绘制内容：
1. 拖动/缩放中：被选控件旁画 `宽×高`（取整世界像素）+ 移动时 `x, y`。
2. 选中 + `m_hoverId` 非空：两控件世界矩形间画水平/垂直双向箭头 + 间距数值。
标签用 `worldRectToScreen` 换到屏幕坐标，画圆角矩形底 + 文字。绘制放在 paintEvent 的 `p.restore()` 之后（屏幕空间）。

- [ ] **Step 4: paintEvent 末尾调用**

在缩放百分比提示之前加 `if (m_aidMeasure) drawMeasureHints(p);`。

- [ ] **Step 5: 编译运行验证**

运行"构建与验证"。拖动控件时旁边显示 `宽×高` 与 `x,y`；选中一个控件后把鼠标悬停到另一个控件上，两者间出现带数值的间距箭头。

- [ ] **Step 6: 提交**

```bash
git add launcher/src/editor/UIEditor.h launcher/src/editor/UIEditor.cpp
git commit -m "feat(ui-aids): 间距/尺寸测量提示"
```

---

### Task 5: 锚点 / 边距可视化

**Files:**
- Modify: `launcher/src/editor/UIEditor.h`（成员 `bool m_aidAnchor`、方法 `drawAnchorBadge`）
- Modify: `launcher/src/editor/UIEditor.cpp`（paint 绘制）

**Interfaces:**
- Produces: `void drawAnchorBadge(QPainter& p) const;`、成员 `bool m_aidAnchor = true;`、`void setAidAnchor(bool on)`
- Consumes: `resolveRect`、`parentWorldRect`、`UIWidget::anchor`（现有锚点字段，值如"左上/居中/右下"）

- [ ] **Step 1: 头文件加成员**

```cpp
    bool m_aidAnchor = true;
    void drawAnchorBadge(QPainter& p) const;
public:
    void setAidAnchor(bool on) { m_aidAnchor = on; update(); }
```

- [ ] **Step 2: 实现 drawAnchorBadge（仅单选）**

`m_selectedIds.size() == 1` 时：取选中控件世界矩形 `r = resolveRect(id)` 与父矩形 `pr = parentWorldRect(id)`。
1. 按控件锚点字符串在父矩形对应位置画十字/花瓣标记（世界变换内，线宽 `1/zoom`）。
2. 从控件四边到父矩形对应边画虚线，并在中点标注边距像素值（`r.left()-pr.left()` 等，取整）。
数值标签可走屏幕空间（同 Task 4 药丸风格）。

- [ ] **Step 3: paintEvent 调用**

世界变换区间内、`drawSnapGuides` 之前加 `if (m_aidAnchor) drawAnchorBadge(p);`（虚线在世界空间，数值标签在屏幕空间则放 restore 后）。

- [ ] **Step 4: 编译运行验证**

运行"构建与验证"。单选一个控件，画布上出现锚点标记与到父容器四边的边距虚线+数值；多选时不显示。

- [ ] **Step 5: 提交**

```bash
git add launcher/src/editor/UIEditor.h launcher/src/editor/UIEditor.cpp
git commit -m "feat(ui-aids): 锚点/边距可视化"
```

---

### Task 6: 标尺 + 拖拽辅助线（Guide）

**Files:**
- Modify: `launcher/src/editor/UIEditor.h`（成员 `bool m_aidRuler`、Guide 容器、方法 `drawRulers`、hit 测试）
- Modify: `launcher/src/editor/UIEditor.cpp`（paint 绘制、mousePress/Move/Release 处理标尺拖出/拖动/删除、Guide 并入候选）

**Interfaces:**
- Produces: `void drawRulers(QPainter& p) const;`、成员 `bool m_aidRuler = true; QVector<double> m_guidesX, m_guidesY; int m_draggingGuide = -1; bool m_dragGuideVertical = false;`、`void setAidRuler(bool on)`
- Consumes: `screenToCanvas`、`worldRectToScreen`、`rebuildSnapCandidates`（Task 2/3，追加 Guide 候选）

- [ ] **Step 1: 头文件加成员**

```cpp
    bool m_aidRuler = true;
    QVector<double> m_guidesX, m_guidesY;     // 世界坐标（仅内存）
    int  m_draggingGuide = -1;                // 正在拖动的 guide 索引
    bool m_dragGuideVertical = false;
    static constexpr int kRulerSize = 20;     // 标尺厚度(屏幕像素)
    void drawRulers(QPainter& p) const;
public:
    void setAidRuler(bool on) { m_aidRuler = on; update(); }
```

- [ ] **Step 2: 实现 drawRulers（屏幕空间）**

上/左各画 `kRulerSize` 厚刻度条；刻度按世界坐标，间隔随 `m_zoom` 在 {50,100,200,500} 取一档（让屏幕间距落在 ~50–120px）；用 `worldRectToScreen` 或 `screenToCanvas` 反算刻度屏幕位置；画跟随鼠标的指示刻线；左上角 `kRulerSize×kRulerSize` 方块作为标尺显隐按钮。已有的 Guide（`m_guidesX/Y`）在画布上画成绿色虚线。

- [ ] **Step 3: paintEvent 末尾调用**

在屏幕空间段（`p.restore()` 后）加 `if (m_aidRuler) drawRulers(p);`。Guide 虚线在世界变换内画（restore 前）。

- [ ] **Step 4: 鼠标交互**

- `mousePressEvent`：若在上标尺区按下 → 开始拖出水平 Guide（`m_draggingGuide = 新建, m_dragGuideVertical=false`）；左标尺区 → 垂直 Guide；命中已有 Guide → 拖动它；点左上角方块 → `m_aidRuler = !m_aidRuler`。
- `mouseMoveEvent`：拖动 Guide 时更新其世界坐标 `screenToCanvas(pos)`，`update()`。
- `mouseReleaseEvent`：松手时若 Guide 落在标尺区内 → 删除该 Guide，否则提交。

- [ ] **Step 5: Guide 并入吸附候选**

在 `rebuildSnapCandidates` 末尾追加：`for (double x : m_guidesX) m_snap.addLine({true, x, SnapLine::Guide, vp.top(), vp.bottom()}); for (double y : m_guidesY) m_snap.addLine({false, y, SnapLine::Guide, vp.left(), vp.right()});`

- [ ] **Step 6: 编译运行验证**

运行"构建与验证"。画布上下左出现标尺并随缩放更新刻度；从标尺拖出绿色 Guide，拖动控件可吸附到 Guide；把 Guide 拖回标尺删除；点左上角方块切换标尺显隐。

- [ ] **Step 7: 提交**

```bash
git add launcher/src/editor/UIEditor.h launcher/src/editor/UIEditor.cpp
git commit -m "feat(ui-aids): 标尺 + 可拖拽辅助线"
```

---

### Task 7: "辅助"下拉开关

**Files:**
- Modify: `launcher/src/editor/UIEditor.cpp`（工具栏 alignBar 旁加下拉菜单，连接到 canvas 的 setAidXxx）

**Interfaces:**
- Consumes: `setAidSnap`/`setAidMeasure`/`setAidAnchor`/`setAidRuler`（Task 2/4/5/6）

- [ ] **Step 1: 在工具栏 snap 按钮附近加"辅助" QToolButton + QMenu**

四个 checkable QAction：智能吸附、标尺、锚点边距、测量提示，默认全 checked。`toggled` 分别连 `m_canvas->setAidSnap/...`。放在 `centerLay->addWidget(alignBar);`（约 1024 行）之前的工具栏区域。

```cpp
auto* aidBtn = new QToolButton; aidBtn->setText("辅助");
aidBtn->setPopupMode(QToolButton::InstantPopup);
auto* aidMenu = new QMenu(aidBtn);
auto addAid = [&](const QString& t, std::function<void(bool)> f){
    auto* a = aidMenu->addAction(t); a->setCheckable(true); a->setChecked(true);
    connect(a, &QAction::toggled, this, [f](bool on){ f(on); });
};
addAid("智能吸附",   [this](bool on){ m_canvas->setAidSnap(on); });
addAid("标尺",       [this](bool on){ m_canvas->setAidRuler(on); });
addAid("锚点边距",   [this](bool on){ m_canvas->setAidAnchor(on); });
addAid("测量提示",   [this](bool on){ m_canvas->setAidMeasure(on); });
aidBtn->setMenu(aidMenu);
// 加入 alignBar 的布局
```

- [ ] **Step 2: 编译运行验证**

运行"构建与验证"。工具栏出现"辅助"下拉，取消勾选某项后对应辅助绘制/吸附立即关闭。

- [ ] **Step 3: 提交**

```bash
git add launcher/src/editor/UIEditor.cpp
git commit -m "feat(ui-aids): 辅助功能下拉开关"
```

---

## Self-Review

- **Spec coverage：** 第1节→Task1-3；第2节→Task6；第3节→Task5；第4节→Task4；第5节→Task7。背景吸附（画布/场景/摄像机/Guide）分别在 Task2/3/6 落地。全覆盖。
- **Placeholder：** 无 TBD/TODO；代码步骤均给出实际代码或精确插入位置。
- **Type 一致性：** `SnapLine`/`SnapResult`/`setAidSnap`/`setAidRuler`/`setAidAnchor`/`setAidMeasure`/`rebuildSnapCandidates`/`drawSnapGuides`/`drawRulers`/`drawAnchorBadge`/`drawMeasureHints` 跨任务命名一致；`pushRect` 在 Task3 提示提取复用。
- **字段名核对（已对照 `UIDocument.h`）：** 宽高为 `UIWidget::width` / `height`（非 `w`/`h`）；坐标 `x`/`y`；锚点 `anchor`，取值 `左上/正上/右上/左中/居中/右中/左下/正下/右下`。Task5 锚点标记按这 9 值映射父矩形位置。
- **坐标系一致性：** 吸附候选线与 movingRect 统一用 `resolveRect` 世界矩形系；拖动位移量取 `newX/Y - start`，见 Task2 Step4 说明。
