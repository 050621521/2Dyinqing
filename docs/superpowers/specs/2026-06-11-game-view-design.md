# 游戏视图（Game View）设计文档

## 概述

参照虚幻引擎的游戏视图，在编辑器中新增一个独立的「游戏视图」Tab，以主摄像机视角渲染场景画面，支持黑边适配、运行时自动跳转。

---

## 一、数据模型变更

### ActorData 字段变更

**新增字段：**
```cpp
bool cameraIsMain = false;   // 是否为主摄像机（同一关卡互斥）
int  cameraResW   = 1920;    // 目标分辨率宽（像素）
int  cameraResH   = 1080;    // 目标分辨率高（像素）
```

**移除字段：**
```cpp
float cameraAspect;  // 由 cameraResW / cameraResH 自动推导，不再存储
```

**序列化：** `toJson` / `fromJson` 同步更新，旧文件中 `cameraAspect` 字段自动忽略。

### 互斥逻辑

`cameraIsMain` 在同一关卡中最多一个为 true。互斥逻辑放在 `EditorWindow`：当 `DetailsPanel::actorModified` 触发，且该 actor 带 Camera 组件且 `cameraIsMain == true` 时，遍历关卡所有其他 Camera actor，将其 `cameraIsMain` 置 false 并调用 `doc->updateActor`。

### 新建关卡默认摄像机

`ContentBrowser` 创建新关卡文件后，自动写入一个默认 Actor：
- `type = "Camera"`，`name = "主摄像机"`
- `components = ["摄像机"]`
- `cameraIsMain = true`，`cameraSize = 5.0`，`cameraResW = 1920`，`cameraResH = 1080`
- `position = (0, 0)`，`rotation = 0`

---

## 二、DetailsPanel 摄像机组件 UI 变更

**移除：** `cameraAspect` 输入框（`m_cameraAspectSpin`）

**新增：**
- `m_cameraIsMainCheck`（QCheckBox）：「主摄像机」，互斥勾选
- `m_cameraResWSpin`（QSpinBox，范围 1–7680）：分辨率宽
- `m_cameraResHSpin`（QSpinBox，范围 1–4320）：分辨率高
- 显示格式：`分辨率  [1920] × [1080]`

---

## 三、GameViewport Widget

### 文件
- `launcher/src/editor/GameViewport.h`
- `launcher/src/editor/GameViewport.cpp`

### 类结构

```cpp
class GameViewport : public QWidget {
    Q_OBJECT
public:
    explicit GameViewport(QWidget* parent = nullptr);

    void loadLevel(LevelDocument* doc);
    void setRuntimeActors(const QList<ActorData>& actors);
    void setRuntimeMode(bool on);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRectF computeCameraRect() const;          // 计算黑边后的画面矩形
    QPointF cameraWorldToScreen(QPointF world, const QRectF& camRect,
                                const ActorData& cam) const;
    void drawScene(QPainter& p, const QList<ActorData>& actors,
                   const ActorData& cam);

    LevelDocument*   m_doc          = nullptr;
    QList<ActorData> m_runtimeActors;
    bool             m_runtimeMode  = false;
    QLabel*          m_camNameLabel = nullptr;  // 工具栏：摄像机名
    QLabel*          m_resLabel     = nullptr;  // 工具栏：分辨率
};
```

### 工具栏布局（28px 高）

```
[ 摄像机：主摄像机 ]  |  [ 1920×1080 ]  |  <stretch>  |  [ ⛶ 全屏 ]
```

- 摄像机名和分辨率在 `paintEvent` / `resizeEvent` 后更新
- 全屏按钮：`showFullScreen()` / `showNormal()` 切换

### 渲染逻辑（paintEvent）

1. 全区域填黑
2. 查找主摄像机（`cameraIsMain == true`）
3. **无主摄像机**：居中白字「场景中无主摄像机」，结束
4. **有主摄像机**：
   - 计算画面矩形（居中 + 黑边，保持 `cameraResW:cameraResH` 比例）
   - 画面区域填 `cameraBackground` 色
   - 在画面矩形内绘制所有 actors（sprite / 占位图形）
   - **不绘制**：网格、坐标轴、原点标记、Gizmo、摄像机视锥框

### 坐标变换

```
cameraWorldToScreen(world):
  camRect   = computeCameraRect()          // 屏幕上的画面矩形
  halfH_world = cam.cameraSize             // 摄像机世界半高
  halfW_world = halfH_world * (resW/resH)  // 世界半宽
  scaleX = camRect.width()  / (halfW_world * 2)
  scaleY = camRect.height() / (halfH_world * 2)
  return QPointF(
    camRect.center().x() + (world.x - cam.x) * scaleX,
    camRect.center().y() - (world.y - cam.y) * scaleY   // Y 轴翻转
  )
```

---

## 四、EditorWindow 集成

### 新增成员

```cpp
GameViewport*      m_gameViewport   = nullptr;
QWidget*           m_gameViewPage   = nullptr;   // wrapper（工具栏+GameViewport）
ads::CDockWidget*  m_gameViewDockW  = nullptr;   // centralStack index 2
```

`DocTabBar` 新增常量：
```cpp
static const QString kGameViewTabData;  // = "__gameview__"
```

### Tab 初始化

编辑器启动时，在 `setupDocTabBar` 或 `setupCentralArea` 后，自动在 DocTabBar 末尾添加「游戏视图」Tab（不可关闭），`m_centralStack` 新增第三个页面。

### onTabChanged 新增分支

```
游戏视图 Tab → centralStack 切到 gameViewPage
              m_gameViewport->loadLevel(当前关卡 doc)
```

### 运行时联动

```
startRuntime():
    创建 BPRuntime
    切换到游戏视图 Tab
    m_gameViewport->setRuntimeMode(true)
    m_gameViewport->setRuntimeActors(runtime->actors())

BPRuntime::stateChanged:
    m_viewport->updateRuntimeActors(...)        // 原有逻辑保留
    m_gameViewport->setRuntimeActors(...)       // 新增

stopRuntime():
    m_gameViewport->setRuntimeMode(false)
    // 不强制跳回其他 Tab
```

---

## 五、CMakeLists.txt 变更

在 `SOURCES` 和 `HEADERS` 列表新增：
```cmake
src/editor/GameViewport.cpp
src/editor/GameViewport.h
```

---

## 六、不在本次范围内

- 多摄像机切换下拉（游戏视图只认主摄像机）
- 运行时摄像机跟随动画（由蓝图运行时驱动 Actor 位置实现，GameViewport 无需额外处理）
- 音频、粒子等非 2D Sprite 渲染
