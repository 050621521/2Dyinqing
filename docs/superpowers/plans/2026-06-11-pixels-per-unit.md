# Pixels Per Unit 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在项目级别引入 Pixels Per Unit（PPU）机制，让精灵渲染大小 = 像素尺寸 / PPU × scale，而不是直接用原始像素尺寸。

**Architecture:** PPU 值存在 `project.json`，由 `ProjectSettingsDialog` 读写；`EditorWindow` 在启动时和项目设置保存后将 PPU 下发给 `Viewport2D` 和 `GameViewport`；两个视口在各自的渲染和命中检测公式中除以 `m_ppu`。

**Tech Stack:** C++17, Qt6 Widgets, QSpinBox, QJsonObject

---

## 文件改动清单

| 文件 | 改动类型 | 内容 |
|---|---|---|
| `launcher/src/editor/ProjectSettingsDialog.h` | 修改 | 加 `readPixelsPerUnit()` 静态方法声明 + `m_ppuSpinBox` 成员 |
| `launcher/src/editor/ProjectSettingsDialog.cpp` | 修改 | 实现 `readPixelsPerUnit()`、加 SpinBox UI、修复 `saveSettings()` 保留已有字段 |
| `launcher/src/editor/Viewport2D.h` | 修改 | 加 `m_ppu` 成员 + `setPixelsPerUnit()` 声明 |
| `launcher/src/editor/Viewport2D.cpp` | 修改 | 实现 setter；更新 `drawActors()` 1 处、`mousePressEvent()` 2 处的精灵尺寸公式 |
| `launcher/src/editor/GameViewport.h` | 修改 | 加 `m_ppu` 成员 + `setPixelsPerUnit()` 声明 |
| `launcher/src/editor/GameViewport.cpp` | 修改 | 实现 setter；更新 `drawScene()` 1 处精灵尺寸公式 |
| `launcher/src/editor/EditorWindow.cpp` | 修改 | 构造函数加 PPU 初始化；`onProjectSettings()` 保存后刷新 |

---

## Task 1: ProjectSettingsDialog — 读写 PPU

**Files:**
- Modify: `launcher/src/editor/ProjectSettingsDialog.h`
- Modify: `launcher/src/editor/ProjectSettingsDialog.cpp`

- [ ] **Step 1: 修改头文件，声明新成员**

将 `ProjectSettingsDialog.h` 中 `public:` 区的静态方法声明后面加上新静态方法，`private:` 区加上 SpinBox 成员：

```cpp
// ProjectSettingsDialog.h — 完整替换内容
#pragma once
#include "models/ProjectInfo.h"
#include <QDialog>

class QLineEdit;
class QComboBox;
class QSpinBox;

class ProjectSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectSettingsDialog(const ProjectInfo& project, QWidget* parent = nullptr);

    static QString readDefaultLevel(const QString& projectPath);
    static float   readPixelsPerUnit(const QString& projectPath);

private:
    void saveSettings();

    ProjectInfo  m_project;
    QLineEdit*   m_nameEdit          = nullptr;
    QComboBox*   m_defaultLevelCombo = nullptr;
    QSpinBox*    m_ppuSpinBox        = nullptr;
};
```

- [ ] **Step 2: 实现 `readPixelsPerUnit()` 静态方法**

在 `ProjectSettingsDialog.cpp` 中，紧跟 `readDefaultLevel()` 函数之后加入：

```cpp
float ProjectSettingsDialog::readPixelsPerUnit(const QString& projectPath) {
    QFile f(projectJsonPath(projectPath));
    if (!f.open(QIODevice::ReadOnly)) return 100.0f;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return (float)obj.value("pixelsPerUnit").toDouble(100.0);
}
```

- [ ] **Step 3: 在构造函数的 grid 里加 SpinBox 一行**

在 `.cpp` 顶部 include 区加入：
```cpp
#include <QSpinBox>
```

在构造函数中，`grid->addWidget(makeLabel("默认关卡"), 3, 0);` 之后（`root->addLayout(grid);` 之前）加入：

```cpp
// 像素/单位
m_ppuSpinBox = new QSpinBox(this);
m_ppuSpinBox->setObjectName("psInput");
m_ppuSpinBox->setRange(1, 10000);
m_ppuSpinBox->setSuffix(" px/unit");
m_ppuSpinBox->setValue((int)readPixelsPerUnit(m_project.path));
grid->addWidget(makeLabel("像素/单位"), 4, 0);
grid->addWidget(m_ppuSpinBox, 4, 1);
```

同时将对话框高度从 `setFixedSize(520, 260)` 改为 `setFixedSize(520, 300)`。

- [ ] **Step 4: 修复 `saveSettings()`，保留 project.json 中已有字段**

当前 `saveSettings()` 会覆写整个 json，导致 `defaultLevel` 以外的字段丢失。改为先读取再合并写入：

```cpp
void ProjectSettingsDialog::saveSettings() {
    QFile rf(projectJsonPath(m_project.path));
    QJsonObject obj;
    if (rf.open(QIODevice::ReadOnly))
        obj = QJsonDocument::fromJson(rf.readAll()).object();

    obj["defaultLevel"] = m_defaultLevelCombo->currentData().toString();
    obj["pixelsPerUnit"] = m_ppuSpinBox->value();

    QFile wf(projectJsonPath(m_project.path));
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        wf.write(QJsonDocument(obj).toJson());
}
```

- [ ] **Step 5: 编译验证**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

预期：编译通过，打开项目后进入「文件 → 项目设置」，看到「像素/单位」一行，默认值 100。

- [ ] **Step 6: Commit**

```bash
git add launcher/src/editor/ProjectSettingsDialog.h launcher/src/editor/ProjectSettingsDialog.cpp
git commit -m "feat: ProjectSettingsDialog 加入 pixelsPerUnit 读写"
```

---

## Task 2: Viewport2D — 加 PPU 成员并更新渲染公式

**Files:**
- Modify: `launcher/src/editor/Viewport2D.h`
- Modify: `launcher/src/editor/Viewport2D.cpp`

- [ ] **Step 1: 头文件加成员和 setter 声明**

在 `Viewport2D.h` 的 `public:` 区的 `setRuntimeMode` 声明后面加：

```cpp
void setPixelsPerUnit(float ppu);
```

在 `private:` 区的 `float m_zoom = 1.0f;` 之前加：

```cpp
float m_ppu = 100.0f;
```

- [ ] **Step 2: 实现 setter**

在 `Viewport2D.cpp` 的 `setToolMode()` 函数之后加：

```cpp
void Viewport2D::setPixelsPerUnit(float ppu) {
    m_ppu = qMax(1.0f, ppu);
    update();
}
```

- [ ] **Step 3: 更新 `drawActors()` 中的精灵尺寸计算**

定位 `drawActors()` 中计算 `szW` / `szH` 的两行（当前约第 167–172 行），将：

```cpp
const float szW = hasPx
    ? m_pixmapCache[a.spritePath].width()  * m_zoom * qMax(0.05f, qAbs(a.scaleX))
    : szBase * qMax(0.05f, qAbs(a.scaleX));
const float szH = hasPx
    ? m_pixmapCache[a.spritePath].height() * m_zoom * qMax(0.05f, qAbs(a.scaleY))
    : szBase * qMax(0.05f, qAbs(a.scaleY));
```

改为：

```cpp
const float szW = hasPx
    ? m_pixmapCache[a.spritePath].width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
    : szBase * qMax(0.05f, qAbs(a.scaleX));
const float szH = hasPx
    ? m_pixmapCache[a.spritePath].height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
    : szBase * qMax(0.05f, qAbs(a.scaleY));
```

- [ ] **Step 4: 更新 `mousePressEvent()` 左键命中检测的精灵尺寸计算**

定位左键点选 Actor 的命中检测块（变量名为 `hasPxH` / `szBaseH` / `szW` / `szH`，当前约第 505–511 行），将：

```cpp
const float szW = hasPxH
    ? m_pixmapCache[a.spritePath].width()  * m_zoom * qMax(0.05f, qAbs(a.scaleX))
    : szBaseH * qMax(0.05f, qAbs(a.scaleX));
const float szH = hasPxH
    ? m_pixmapCache[a.spritePath].height() * m_zoom * qMax(0.05f, qAbs(a.scaleY))
    : szBaseH * qMax(0.05f, qAbs(a.scaleY));
```

改为：

```cpp
const float szW = hasPxH
    ? m_pixmapCache[a.spritePath].width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
    : szBaseH * qMax(0.05f, qAbs(a.scaleX));
const float szH = hasPxH
    ? m_pixmapCache[a.spritePath].height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
    : szBaseH * qMax(0.05f, qAbs(a.scaleY));
```

- [ ] **Step 5: 更新 `mousePressEvent()` 右键命中检测的精灵尺寸计算**

定位右键点选 Actor 的命中检测块（变量名为 `hasPxR` / `szBaseR` / `szW2` / `szH2`，当前约第 547–554 行），将：

```cpp
const float szW2 = hasPxR
    ? m_pixmapCache[a.spritePath].width()  * m_zoom * qMax(0.05f, qAbs(a.scaleX))
    : szBaseR * qMax(0.05f, qAbs(a.scaleX));
const float szH2 = hasPxR
    ? m_pixmapCache[a.spritePath].height() * m_zoom * qMax(0.05f, qAbs(a.scaleY))
    : szBaseR * qMax(0.05f, qAbs(a.scaleY));
```

改为：

```cpp
const float szW2 = hasPxR
    ? m_pixmapCache[a.spritePath].width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
    : szBaseR * qMax(0.05f, qAbs(a.scaleX));
const float szH2 = hasPxR
    ? m_pixmapCache[a.spritePath].height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
    : szBaseR * qMax(0.05f, qAbs(a.scaleY));
```

- [ ] **Step 6: 编译验证**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

预期：编译通过（PPU 目前为默认 100，精灵大小视觉上会缩小为原来的 1/100，此为预期 — Task 4 接入 EditorWindow 后将从 project.json 读取并恢复正常）。

- [ ] **Step 7: Commit**

```bash
git add launcher/src/editor/Viewport2D.h launcher/src/editor/Viewport2D.cpp
git commit -m "feat: Viewport2D 加入 setPixelsPerUnit，精灵尺寸渲染/命中检测接入 PPU"
```

---

## Task 3: GameViewport — 加 PPU 成员并更新渲染公式

**Files:**
- Modify: `launcher/src/editor/GameViewport.h`
- Modify: `launcher/src/editor/GameViewport.cpp`

- [ ] **Step 1: 头文件加成员和 setter 声明**

打开 `GameViewport.h`，在 `public:` 区加：

```cpp
void setPixelsPerUnit(float ppu);
```

在 `private:` 区加：

```cpp
float m_ppu = 100.0f;
```

- [ ] **Step 2: 实现 setter**

在 `GameViewport.cpp` 的 `setRuntimeMode()` 函数之后加：

```cpp
void GameViewport::setPixelsPerUnit(float ppu) {
    m_ppu = qMax(1.0f, ppu);
    update();
}
```

- [ ] **Step 3: 更新 `drawScene()` 中的精灵尺寸计算**

定位 `drawScene()` 中计算 `szW` / `szH` 的两行（当前约第 126–127 行，注释写着 `1 world unit = 1 pixel`），将：

```cpp
const float szW = px.width()  * scale * qMax(0.05f, qAbs(a.scaleX));
const float szH = px.height() * scale * qMax(0.05f, qAbs(a.scaleY));
```

改为：

```cpp
const float szW = px.width()  / m_ppu * scale * qMax(0.05f, qAbs(a.scaleX));
const float szH = px.height() / m_ppu * scale * qMax(0.05f, qAbs(a.scaleY));
```

同时删除上方注释 `// 以精灵的自然像素尺寸乘以摄像机缩放因子，1 world unit = 1 pixel`（已过时）。

- [ ] **Step 4: 编译验证**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

预期：编译通过。

- [ ] **Step 5: Commit**

```bash
git add launcher/src/editor/GameViewport.h launcher/src/editor/GameViewport.cpp
git commit -m "feat: GameViewport 加入 setPixelsPerUnit，精灵尺寸渲染接入 PPU"
```

---

## Task 4: EditorWindow — 启动和保存后下发 PPU

**Files:**
- Modify: `launcher/src/editor/EditorWindow.cpp`

- [ ] **Step 1: 构造函数中，`setupWindowMenu()` 之后加 PPU 初始化**

在 `EditorWindow.cpp` 构造函数中，`setupWindowMenu();` 这一行之后、`QString defaultLevel = ...` 这一行之前，加入：

```cpp
{
    float ppu = ProjectSettingsDialog::readPixelsPerUnit(m_project.path);
    m_viewport->setPixelsPerUnit(ppu);
    m_gameViewport->setPixelsPerUnit(ppu);
}
```

- [ ] **Step 2: 修改 `onProjectSettings()`，保存后刷新视口的 PPU**

将当前的：

```cpp
void EditorWindow::onProjectSettings() {
    auto* dlg = new ProjectSettingsDialog(m_project, this);
    dlg->exec();
    dlg->deleteLater();
}
```

改为：

```cpp
void EditorWindow::onProjectSettings() {
    auto* dlg = new ProjectSettingsDialog(m_project, this);
    if (dlg->exec() == QDialog::Accepted) {
        float ppu = ProjectSettingsDialog::readPixelsPerUnit(m_project.path);
        m_viewport->setPixelsPerUnit(ppu);
        m_gameViewport->setPixelsPerUnit(ppu);
    }
    dlg->deleteLater();
}
```

- [ ] **Step 3: 编译并完整验证**

```bash
pkill -x launcher 2>/dev/null; sleep 0.3; cd /Users/kwy/Documents/2Dyinqing/launcher/build && cmake --build . -j$(sysctl -n hw.logicalcpu) && open launcher.app
```

验证清单：
1. 打开一个有精灵的项目，确认精灵正常显示（PPU=100，100px 精灵 = 1 世界单位宽）
2. 打开「文件 → 项目设置」，将 PPU 改为 50，点确定
3. 视口中精灵变大（相同 scale 下，显示尺寸变为原来的 2 倍）
4. 游戏视图中精灵也变大
5. 点选精灵，命中框与视觉框对齐
6. 将 PPU 改回 100，精灵恢复原来大小

- [ ] **Step 4: Commit**

```bash
git add launcher/src/editor/EditorWindow.cpp
git commit -m "feat: EditorWindow 启动和项目设置保存后下发 PPU 到视口"
```

---

## 完成标志

- `project.json` 中可见 `"pixelsPerUnit": 100`
- 项目设置对话框有「像素/单位」字段，修改后立即反映到两个视口
- 精灵显示大小公式：`像素尺寸 / PPU × scale × zoom`
- 点选命中框与视觉框始终对齐
- 无精灵的占位 Actor 显示不受影响
