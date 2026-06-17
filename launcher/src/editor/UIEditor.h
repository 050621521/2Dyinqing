#pragma once
#include <QUndoStack>
#include <functional>
#include "models/UIDocument.h"
#include "models/LevelDocument.h"
#include "editor/UISnapGuides.h"
#include <QWidget>
#include <QTreeWidget>
#include <QScrollArea>
#include <QComboBox>
#include <QHash>
#include <QSet>
#include <QPixmap>
#include <QSplitter>
#include <functional>

class QToolButton;
class QPainter;

// ── AnchorPicker（3×3 锚点选择器）────────────────────────────────────────
class AnchorPicker : public QWidget {
    Q_OBJECT
public:
    explicit AnchorPicker(QWidget* parent = nullptr);
    void    setAnchor(const QString& anchor);
    QString anchor() const { return m_anchor; }

signals:
    void anchorChanged(const QString& anchor);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QString m_anchor = "左上";
};

// ── UIEditorCanvas（独立类，避免 MOC 嵌套限制）────────────────────────────
class UIEditorCanvas : public QWidget {
    Q_OBJECT
public:
    explicit UIEditorCanvas(QWidget* parent = nullptr);
    void setDoc(UIDocument* doc);
    void setPreviewLevel(LevelDocument* level, float ppu);
    void setSelectedId(const QString& id);
    QString selectedId() const { return m_selectedId; }
    QStringList selectedIds() const { return m_selectedIds.values(); }

    void setPixelSnap(bool enabled, int grid);
    void alignSelected(int type);     // 0=左 1=右 2=水平居中 3=上 4=下 5=垂直居中
    void distributeSelected(bool horizontal);
    void makeSameSize(bool width);
    QRectF worldRectOf(const QString& widgetId) const;    // 控件在背景预览世界坐标中的矩形
    QRectF parentWorldRect(const QString& widgetId) const; // 控件父区域世界矩形

    // 回调，由 UIEditor 赋值
    std::function<void(const QString&)>                              onSelectionChanged;
    std::function<void(QStringList)>                                 onMultiSelectionChanged;
    std::function<void(const QString&, float, float)>                onWidgetMoved;
    std::function<void(QHash<QString,QPointF>)>                      onWidgetsMoved;
    std::function<void(const QString&, float, float, float, float)>  onWidgetResized; // id,x,y,w,h
    std::function<void(const QString&)>                              onAddWidget;
    std::function<void()>                                            onDeleteSelected;
    std::function<void(const QString&, const QString&)>              onImageDropped;
    // 拖拽/缩放开始与结束（用于批量 undo 提交）
    std::function<void(QStringList)> onDragBegan;
    std::function<void(QStringList)> onDragEnded;
    std::function<void(QString)>     onResizeBegan;
    std::function<void()>            onResizeEnded;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void    showCanvasMenu(const QPoint& globalPos);   // 右键单击弹出的画布菜单
    QRectF  widgetScreenRect(const UIWidget& w, const QRectF& parentRect) const;
    void    renderWidget(QPainter& p, const UIWidget& w, const QRectF& parentRect) const;
    void    renderChildren(QPainter& p, const QString& parentId,
                           const QRectF& parentRect, const UIWidget& parent) const;
    QString hitTest(QPointF pos, const QString& parentId, const QRectF& parentRect) const;

    UIDocument*    m_doc       = nullptr;
    LevelDocument* m_level     = nullptr;
    float          m_ppu       = 100.0f;
    QString        m_selectedId;             // 主选（单选兼容）
    QSet<QString>  m_selectedIds;            // 完整选区
    bool           m_dragging  = false;
    QPointF        m_dragStart;
    float          m_dragInitX = 0, m_dragInitY = 0;
    QHash<QString, QPointF> m_dragStartPositions; // 多选拖拽起始位置
    QHash<QString, QRectF>  m_dragStartWorldRects; // 拖拽起始世界矩形（吸附用，避免反馈震荡）
    mutable QHash<QString, QPixmap> m_pixmapCache;

    // ── 智能对齐参考线 ──
    UISnapGuides       m_snap;
    QVector<SnapLine>  m_activeGuides;
    bool               m_aidSnap = true;
    void rebuildSnapCandidates();           // 拖动/缩放开始时收集候选
    void drawSnapGuides(QPainter& p) const; // 世界变换内绘制高亮线
public:
    void setAidSnap(bool on) { m_aidSnap = on; update(); }
private:

    // ── 间距 / 尺寸测量提示 ──
    bool    m_aidMeasure = true;
    QString m_hoverId;                       // 选中态下悬停的另一控件
    void drawMeasureHints(QPainter& p) const;
public:
    void setAidMeasure(bool on) { m_aidMeasure = on; update(); }
private:

    // ── 锚点 / 边距可视化 ──
    bool m_aidAnchor = true;
    // screenPhase=false：世界空间段（锚点标记 + 边距虚线）；true：屏幕空间段（边距数值药丸）
    void drawAnchorBadge(QPainter& p, bool screenPhase) const;
public:
    void setAidAnchor(bool on) { m_aidAnchor = on; update(); }
private:

    // ── 标尺 + 拖拽辅助线（Guide）──
    bool m_aidRuler = true;
    QVector<double> m_guidesX, m_guidesY;     // 世界坐标（仅内存）
    int  m_draggingGuide = -1;                // 正在拖动的 guide 索引
    bool m_dragGuideVertical = false;
    static constexpr int kRulerSize = 20;     // 标尺厚度(屏幕像素)
    QPointF m_mouseScreenPos;                  // 最近鼠标屏幕位置(标尺指示线)
    void drawRulers(QPainter& p) const;
public:
    void setAidRuler(bool on) { m_aidRuler = on; update(); }
private:

    // 像素吸附
    bool m_pixelSnapEnabled = true;
    int  m_snapGrid         = 1;

    // 框选
    bool   m_rubberBanding = false;
    QPoint m_rubberStart;
    QRect  m_rubberRect;

    // 缩放手柄
    enum class ResizeHandle { None, TL, T, TR, R, BR, B, BL, L };
    bool         m_resizing      = false;
    ResizeHandle m_resizeHandle  = ResizeHandle::None;
    float        m_resizeInitX   = 0, m_resizeInitY = 0;
    float        m_resizeInitW   = 0, m_resizeInitH = 0;

    ResizeHandle hitResizeHandle(QPointF pos, const UIWidget& w) const;

    QRectF  getViewportRect() const;                 // 固定世界坐标 (0,0,cW,cH)
    QPointF cameraWorldToScreen(QPointF world, const QRectF& camRect, const ActorData& cam) const;
    void    drawScenePreview(QPainter& p) const;
    QRectF  resolveRect(const QString& widgetId) const;  // 递归计算世界坐标矩形

    // 视图变换
    QPointF screenOrigin() const;                    // 世界原点的屏幕坐标
    QPointF screenToCanvas(QPointF screenPos) const;
    QRectF  worldRectToScreen(const QRectF& worldRect) const;
    void    updateCanonicalSize();                   // 从摄像机同步分辨率

    float   m_zoom             = 1.0f;
    QPointF m_panOffset;
    bool    m_panning          = false;
    QPoint  m_panStart;
    QPoint  m_panPressPos;                     // 右键按下原点(区分单击/拖动)
    bool    m_panMoved         = false;        // 右键期间是否真的拖动过
    int     m_canonicalW       = 1920;
    int     m_canonicalH       = 1080;
    bool    m_zoomInitialized  = false;
};

// ── UIEditor ──────────────────────────────────────────────────────────────
class UIEditor : public QWidget {
    Q_OBJECT
public:
    explicit UIEditor(QWidget* parent = nullptr);

    void loadDocument(UIDocument* doc);
    void setPreviewLevel(LevelDocument* level, float ppu);
    void setAvailableLevels(const QStringList& levelNames, const QString& activeLevel = {});
    void setProjectRoot(const QString& root);

    UIDocument* document() const { return m_doc; }

signals:
    void documentModified();
    void previewLevelChanged(const QString& levelName);

public slots:
    void onAddWidget(const QString& type);
    void onDeleteSelected();

    void setUndoStack(QUndoStack* stack, std::function<void()> refresh);
    void copySelected();
    void paste();
    void duplicateSelected();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private slots:
    void onTreeSelectionChanged();

private:
    void rebuildTree();
    void rebuildPropsPanel(const QString& widgetId);
    void rebuildMultiPropsPanel(const QStringList& ids);

    UIDocument*      m_doc        = nullptr;
    UIEditorCanvas*  m_canvas     = nullptr;
    QTreeWidget*     m_tree       = nullptr;
    QWidget*         m_props      = nullptr;
    QScrollArea*     m_propScroll = nullptr;
    QComboBox*       m_bgCombo    = nullptr;
    QString          m_selectedId;
    QStringList      m_levelNames;
    QString          m_projectRoot;
    float            m_ppu = 100.0f;

    QList<QToolButton*> m_alignBtns; // 对齐/分布/等尺寸按钮（多选时启用）

    QUndoStack*           m_undoStack = nullptr;
    std::function<void()> m_onRefresh;
    QList<UIWidget>       m_clipboard;
    QList<UIWidget>       m_dragBeforeWidgets;

    QList<UIWidget> collectSubtree(const QString& rootId) const;
};
