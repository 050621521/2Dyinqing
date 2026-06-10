# 停靠面板系统设计规格

**日期：** 2026-06-10  
**状态：** 已批准，待实现

---

## 目标

为 2DYinqing 编辑器引入类 Unreal Editor 风格的停靠面板系统，支持：

- 面板任意拖拽停靠（上/下/左/右/中央 Tab）
- 面板浮动为独立窗口
- 面板 Tab 合并与拆分
- 停靠目标高亮预览（蓝色吸附提示）
- 布局序列化保存/恢复
- 多套命名布局，通过菜单切换

---

## 实现方案

**采用 Qt Advanced Docking System（ADS）** — MIT 开源库，行为模型与 UE 一致，外观通过 QSS 完全可定制。

> 排除方案：  
> - 原生 QDockWidget：Tab 合并/分裂能力有限，样式难以定制至 UE 风格  
> - 完全自研：工作量极大，边角 bug 多

---

## 架构

```
EditorWindow (QMainWindow)
  └── ads::CDockManager                ← 接管整个中央区域
        ├── CDockWidget "视口"          ← Viewport2D，中央固定区
        ├── CDockWidget "大纲"          ← SceneOutliner，默认右上
        ├── CDockWidget "细节"          ← DetailsPanel，默认右下
        ├── CDockWidget "内容浏览器"    ← ContentBrowser，默认底部隐藏
        └── CDockWidget "关卡蓝图"      ← BlueprintEditor，默认中央 Tab 隐藏
```

---

## CMake 集成

`launcher/CMakeLists.txt` 新增：

```cmake
include(FetchContent)
FetchContent_Declare(
    QtADS
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        4.3.1
)
set(ADS_VERSION "4.3.1" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QtADS)

target_link_libraries(launcher PRIVATE qt6advanceddocking)
```

- 首次 configure 联网拉取，之后缓存在 `build/_deps/`
- ADS 样式变量直接追加到现有 `launcher.qss`，无需新增文件

---

## 布局管理

### 新增类：`LayoutManager`

**文件：** `launcher/src/editor/LayoutManager.h` / `LayoutManager.cpp`

```cpp
class LayoutManager : public QObject {
    Q_OBJECT
public:
    explicit LayoutManager(ads::CDockManager* dock, const QString& projectPath);

    QStringList layoutNames() const;
    QString     currentLayout() const;
    void        saveLayout(const QString& name);
    void        loadLayout(const QString& name);
    void        deleteLayout(const QString& name);
    void        resetDefault();

signals:
    void layoutListChanged();
    void currentLayoutChanged(const QString& name);
};
```

### 持久化格式

文件：`{project}/editor_layouts.json`

```json
{
  "current": "默认布局",
  "layouts": {
    "默认布局": "<base64 ADS state>",
    "调试布局": "<base64 ADS state>"
  }
}
```

`CDockManager::saveState()` → `QByteArray::toBase64()` → JSON 字符串  
`CDockManager::restoreState()` ← `QByteArray::fromBase64()` ← JSON 字符串

---

## 默认布局

```
┌─────────────────────────────┬──────────┐
│  视口工具栏                  │          │
├─────────────────────────────┤  大纲    │
│                             │  (右上)  │
│        Viewport2D           ├──────────┤
│        (中央固定)            │  细节    │
│                             │  (右下)  │
├─────────────────────────────┴──────────┤
│  内容浏览器（底部，默认收起）            │
└─────────────────────────────────────────┘
```

### 各面板行为

| 面板 | 默认位置 | 可关闭 | 初始状态 |
|------|----------|--------|----------|
| 视口 | 中央 | 否（`NoTabButton`） | 显示 |
| 大纲 | 右上 | 是 | 显示 |
| 细节 | 右下（与大纲同区 Tab） | 是 | 显示 |
| 内容浏览器 | 底部 | 是 | 隐藏 |
| 关卡蓝图 | 与视口同区 Tab | 是 | 隐藏 |

---

## 窗口菜单

"窗口"菜单（现有占位）扩展为：

```
窗口
 ├── 大纲
 ├── 细节
 ├── 内容浏览器
 ├── 关卡蓝图
 ├── ──────────────
 └── 布局
      ├── 保存当前布局…     （QInputDialog 输入名称）
      ├── ──────────────
      ├── ✓ 默认布局        （当前激活项打勾）
      ├──   调试布局
      ├── ──────────────
      └── 重置为默认布局
```

---

## 拆除的旧逻辑

实现时需要删除以下代码：

| 位置 | 描述 |
|------|------|
| `EditorWindow.cpp:281–295` | `QDockWidget` + `NoDockWidgetFeatures` 设置 |
| `EditorWindow.cpp:251–275` | 内容浏览器浮动覆盖层创建 |
| `EditorWindow.cpp:299–313` | `positionCBPanel` / `positionBPDockedBar` |
| `EditorWindow.cpp:316–429` | `eventFilter` 中的蓝图拖拽全部逻辑 |
| `EditorWindow.cpp:906–954` | `dockBlueprintAsTab` / `undockBlueprintFromTab` |
| `EditorWindow.h` | `m_bpPanel`、`m_bpTitleBar`、`m_bpDockedBar`、`m_bpDragging`、`m_bpDragOffset`、`m_bpDocked`、`m_bpTabIndex`、`m_bpGhostTabIndex`、`m_outlineDock`、`m_detailsDock` |

---

## 样式

在 `launcher.qss` 末尾追加 ADS 专属选择器：

- `ads--CDockAreaTitleBar` — 停靠区标题栏
- `ads--CDockWidgetTab` — Tab 样式
- `ads--CDockSplitter` — 分割线
- `ads--FloatingDockContainer` — 浮动窗口边框

颜色变量沿用现有 QSS 调色板，保持视觉一致。

---

## 按钮行为变更

| 位置 | 旧行为 | 新行为 |
|------|--------|--------|
| 状态栏"内容浏览器"toggle | 手动计算覆盖层位置并 show/hide | `m_cbDockWidget->toggleView()` |
| 视口工具栏"关卡蓝图"按钮 | 显示/隐藏浮动 `m_bpPanel` | `m_bpDockWidget->toggleView()` |
| 窗口菜单各面板项 | 无 | `CDockWidget::toggleView()` |

---

## 不在本次范围内

- 多显示器跨屏浮动
- 布局云同步
- 面板内容的撤销/重做
