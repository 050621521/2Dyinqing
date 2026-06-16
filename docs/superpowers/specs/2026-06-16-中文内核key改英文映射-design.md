# 中文内核 key 改英文 key + 中文显示映射 — 设计规格

日期：2026-06-16

## 背景与目标

引擎中存在「中文字符串被当作内核逻辑 key / 类型 ID」的情况：这些值既参与代码里的 `==` 比较，又被序列化进 `.level` / `.bpmacro` / `editor_layouts.json`。隐患：

1. **跨编译器**：中文字面量依赖源文件 UTF-8，MSVC 不加 `/utf-8` 会乱码失配（当前 clang/macOS 默认 UTF-8，暂未暴露）。
2. **Unicode 规范化（NFC/NFD）**：中文 key 经过不同系统/进程规范化后与源码字面量 `==` 会静默失配。
3. **显示名与持久化 key 绑死**：改一个界面中文名就会破坏存量数据和比较逻辑。
4. **`LayoutManager` 的 `"默认布局"`**：既是 JSON map key、又是哨兵特判，用户保存同名布局会覆盖系统默认布局且无法删除（可复现 bug）。

**目标**：内核存英文 key，界面查表显示中文；读取旧存档时把中文自动映射成英文（兼容层）；保存时一律写英文。**用户界面保持全中文不变。**

## 范围

| 字段 | 处理 |
|---|---|
| UI 控件类型（`UI.面板` 等） | ✅ 改英文 key + 中文显示表 |
| drawMode（`简单/平铺/切片`） | ✅ 改英文 key + 中文显示表 |
| 对齐方式 anchor（`左上/居中` 等） | ✅ 改英文 key + 中文显示表 |
| 布局默认名（`默认布局`） | ✅ 改英文哨兵 `__default__` + 中文显示 |
| **layer / sortingLayer（`背景/前景` 等）** | ❌ **排除**：图层将来允许用户自定义，属自由用户数据，保持中文字符串 |

### 不在本次范围

- 蓝图节点 typeId：本来就是英文，不动。
- 宏（BPMacro）：`id` 已是生成的 UUID，方向正确。仅记录一条原则约束（见下），不在本次改动。

## 设计

### ① 映射工具（单一数据源）

新建 `launcher/src/models/DisplayNames.h`（与 `ActorTypeUtils.h` 同层，models 与 editor 均可 include），集中三类封闭枚举的双向查询 + 兼容归一化：

```cpp
namespace DisplayNames {
  // 英文 key -> 中文显示名
  QString uiTypeDisplay(const QString& key);    // "UI.Panel" -> "面板"
  QString drawModeDisplay(const QString& key);  // "Tile"     -> "平铺"
  QString anchorDisplay(const QString& key);    // "TopLeft"  -> "左上"

  // 兼容归一化：旧中文 或 新英文 -> 新英文（读盘时调用）；未知值原样返回
  QString normUiType(const QString& v);   // "UI.面板"|"UI.Panel" -> "UI.Panel"
  QString normDrawMode(const QString& v);
  QString normAnchor(const QString& v);
}
```

英文 key 方案：

| 域 | 旧中文 → 新英文 key |
|---|---|
| UI 控件 | 面板→`UI.Panel`、文本→`UI.Text`、图片→`UI.Image`、按钮→`UI.Button`、进度条→`UI.Progress`、下拉菜单→`UI.Dropdown`、竖向布局→`UI.VBox`、横向布局→`UI.HBox`、网格布局→`UI.Grid`、滚动视图→`UI.ScrollView` |
| drawMode | 简单→`Simple`、平铺→`Tile`、切片→`Slice` |
| 对齐 anchor | 左上→`TopLeft`、正上→`TopCenter`、右上→`TopRight`、左中→`MiddleLeft`、居中→`Center`、右中→`MiddleRight`、左下→`BottomLeft`、正下→`BottomCenter`、右下→`BottomRight` |

### ② 读盘兼容层（只在读取入口归一化一次）

```cpp
// LevelDocument::actorFromJson
a.drawMode = DisplayNames::normDrawMode(obj["drawMode"].toString("Simple"));
// 旧档 "简单"→"Simple"，新档 "Simple"→"Simple"

// UIDocument 读取
w.type   = DisplayNames::normUiType(obj["type"].toString());
w.anchor = DisplayNames::normAnchor(obj["anchor"].toString("TopLeft"));
```

- 结构体默认值同步改英文：`LevelDocument.h` `drawMode = "简单"` → `"Simple"`；UIDocument 锚点默认改英文。
- `layer / sortingLayer` 读取行不动。
- 未知值兜底：`norm*` 查不到原样返回，避免吃掉用户老数据。

### ③ 下拉框 / 创建入口改用 itemData（界面照旧全中文）

文本显示中文、`data` 存英文，读取读 `currentData()` 而非 `currentText()`，回填用 `findData()`：

```cpp
m_drawModeCombo->addItem("简单", "Simple");
m_drawModeCombo->addItem("平铺", "Tile");
m_drawModeCombo->addItem("切片", "Slice");
QString v = m_drawModeCombo->currentData().toString();
```

| 位置 | 改动 |
|---|---|
| `DetailsPanel` 绘制模式下拉 | itemData 化 |
| `DetailsPanel` 图层 / 排序图层下拉 | **不动**（中文保留） |
| `UIEditor` 控件类型创建菜单 / 按钮 | 菜单显示中文，传英文 key 给 `addWidget` |
| `UIEditor` 控件树节点名 | 显示 `uiTypeDisplay(w.type)` |
| `UIEditor` 对齐下拉 | itemData 化 |
| `GameViewport` / `Viewport2D` 渲染分支 | `== "平铺"` → `== "Tile"`，`== "UI.面板"` → `== "UI.Panel"` 等 |

### ④ 布局默认名

- 内核哨兵 `__default__`，菜单仍显示「默认布局」（`layoutNames` 填充时把 `__default__` 映射成中文）。
- `captureDefault / deleteLayout / resetDefault` 全用 `__default__`，修掉用户同名覆盖 bug。
- 兼容：`readFile` 时把旧 key `"默认布局"` 改名为 `__default__`，`m_current` 同步迁移。

## 数据迁移策略

**兼容映射（读时归一化 + 写时英文）**，不写一次性迁移脚本：

- 旧 `.level` / `.bpmacro` / `editor_layouts.json` 打开后经 `norm*` 自动识别中文值；
- 任一次保存后即落盘为英文 key；
- 零数据丢失，旧档无缝打开。

## 受影响文件（预估）

- 新增：`launcher/src/models/DisplayNames.h`（更新 `CMakeLists.txt` HEADERS）
- `models/LevelDocument.{h,cpp}`：drawMode 默认值 + 读盘归一化
- `models/UIDocument.{h,cpp}`：type / anchor 默认值 + 读盘归一化
- `editor/DetailsPanel.cpp`：drawMode 下拉 itemData 化
- `editor/UIEditor.{h,cpp}`：控件类型菜单 / 树显示 / 对齐下拉
- `editor/GameViewport.cpp`、`editor/Viewport2D.cpp`：渲染分支比较值改英文
- `editor/ContentBrowser.cpp`：默认 actor 写入值改英文（`drawMode = "Simple"` 等）
- `editor/LayoutManager.cpp`：哨兵 `__default__` + 迁移

## 关联原则（记录，非本次改动）

宏库（跨蓝图引用宏）落地时：调用节点必须按宏 `id`（UUID）引用与解析，`macroName`（中文）仅用于显示。否则会重新引入「同名撞车 / 改名断引用」隐患。

## 成功标准

1. 全新建项目，UI 控件 / drawMode / 对齐 / 布局存盘均为英文 key。
2. 打开改动前创建的旧 `.level` / 旧 `editor_layouts.json`，显示与行为与改动前一致，再保存后落盘为英文。
3. 界面所有可见文本仍为中文。
4. 用户保存名为「默认布局」的自定义布局不再覆盖系统默认布局。
5. 编译通过，应用启动正常。
