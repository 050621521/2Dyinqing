# Pixels Per Unit 机制设计文档

**日期**：2026-06-11  
**状态**：待实现

---

## 背景

当前 `Viewport2D` 和 `GameViewport` 直接以精灵的原始像素尺寸决定 Actor 的显示大小（1 像素 = 1 世界单位）。这导致不同分辨率的精灵在同一场景中比例混乱，也与 Unity/Unreal 等主流引擎的处理方式不同。

引入 **Pixels Per Unit（PPU）** 机制，将精灵的像素尺寸与世界空间尺寸解耦：

```
显示大小 = 像素尺寸 / PPU × scale × zoom（或 cameraScale）
```

---

## 设计决策

| 决策点 | 选择 | 原因 |
|---|---|---|
| PPU 层级 | 项目级 | 统一、简单，与 Unity Project Settings 对应 |
| 坐标语义 | 不变（仍为像素） | 兼容已有数据，无需迁移 |
| 默认值 | 100 | 与 Unity 默认值一致 |
| 设置入口 | 项目设置对话框 | 与其他项目级配置集中管理 |

---

## 数据层

### project.json 新增字段

```json
{
  "pixelsPerUnit": 100
}
```

### ProjectSettingsDialog

- 新增静态方法 `readPixelsPerUnit(const QString& projectPath) → float`
- 对话框内加一行：标签"像素/单位"+ `QSpinBox`（范围 1–10000，默认 100）
- 保存时写入 `project.json`

---

## 视口层

### Viewport2D

新增成员：
```cpp
float m_ppu = 100.0f;
```

新增 setter：
```cpp
void setPixelsPerUnit(float ppu) { m_ppu = ppu; update(); }
```

**`drawActors()` 中的精灵尺寸计算**（当前 → 修改后）：

```cpp
// 当前
const float szW = px.width()  * m_zoom * qMax(0.05f, qAbs(a.scaleX));
const float szH = px.height() * m_zoom * qMax(0.05f, qAbs(a.scaleY));

// 修改后
const float szW = px.width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX));
const float szH = px.height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY));
```

**`mousePressEvent()` 中命中检测的尺寸计算**（同样修改，共 3 处重复计算）：

```cpp
// 修改前
const float szW = hasPx ? px.width()  * m_zoom * scale : szBase * scale;
// 修改后
const float szW = hasPx ? px.width()  / m_ppu * m_zoom * scale : szBase * scale;
```

### GameViewport

新增成员和 setter（同上）。

**`drawScene()` 中的精灵尺寸计算**（当前 → 修改后）：

```cpp
// 当前（注释写明 "1 world unit = 1 pixel"）
const float szW = px.width()  * scale * qMax(0.05f, qAbs(a.scaleX));
const float szH = px.height() * scale * qMax(0.05f, qAbs(a.scaleY));

// 修改后
const float szW = px.width()  / m_ppu * scale * qMax(0.05f, qAbs(a.scaleX));
const float szH = px.height() / m_ppu * scale * qMax(0.05f, qAbs(a.scaleY));
```

---

## 胶水层（EditorWindow）

`EditorWindow` 已有 `m_projectPath`，在两处读取并下发 PPU：

1. **启动时**（`EditorWindow` 构造完成后，`loadDefaultLevel()` 之前）：
   ```cpp
   float ppu = ProjectSettingsDialog::readPixelsPerUnit(m_projectPath);
   m_viewport->setPixelsPerUnit(ppu);
   m_gameViewport->setPixelsPerUnit(ppu);
   ```

2. **项目设置保存后**（`ProjectSettingsDialog::accepted` 信号触发）：
   ```cpp
   connect(dlg, &QDialog::accepted, this, [this, dlg]() {
       float ppu = ProjectSettingsDialog::readPixelsPerUnit(m_projectPath);
       m_viewport->setPixelsPerUnit(ppu);
       m_gameViewport->setPixelsPerUnit(ppu);
   });
   ```

---

## 不受影响的部分

- `ActorData` 结构体无变化，`.level` 文件格式无变化
- 无精灵（占位图形）的 Actor 渲染逻辑不变
- 摄像机视口矩形、边界限制框、Gizmo 绘制不变
- `BPRuntime` 运行时逻辑不变
- `DetailsPanel` 无需修改

---

## 文件改动清单

| 文件 | 改动 |
|---|---|
| `ProjectSettingsDialog.h/.cpp` | 加 `readPixelsPerUnit()` 静态方法 + SpinBox UI |
| `Viewport2D.h/.cpp` | 加 `m_ppu` 成员 + `setPixelsPerUnit()` + 渲染/命中公式 |
| `GameViewport.h/.cpp` | 加 `m_ppu` 成员 + `setPixelsPerUnit()` + 渲染公式 |
| `EditorWindow.cpp` | 启动 + 设置保存后下发 PPU |
