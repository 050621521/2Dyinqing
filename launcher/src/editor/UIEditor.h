#pragma once
#include "models/UIDocument.h"
#include "models/LevelDocument.h"
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

    // 回调，由 UIEditor 赋值
    std::function<void(const QString&)>                              onSelectionChanged;
    std::function<void(QStringList)>                                 onMultiSelectionChanged;
    std::function<void(const QString&, float, float)>                onWidgetMoved;
    std::function<void(QHash<QString,QPointF>)>                      onWidgetsMoved;
    std::function<void(const QString&, float, float, float, float)>  onWidgetResized; // id,x,y,w,h
    std::function<void(const QString&)>                              onAddWidget;
    std::function<void()>                                            onDeleteSelected;
    std::function<void(const QString&, const QString&)>              onImageDropped;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
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
    mutable QHash<QString, QPixmap> m_pixmapCache;

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

    QRectF  getViewportRect() const;
    QRectF  computeCameraRect(float aspect) const;
    QPointF cameraWorldToScreen(QPointF world, const QRectF& camRect, const ActorData& cam) const;
    void    drawScenePreview(QPainter& p) const;
};

// ── UIEditor ──────────────────────────────────────────────────────────────
class UIEditor : public QWidget {
    Q_OBJECT
public:
    explicit UIEditor(QWidget* parent = nullptr);

    void loadDocument(UIDocument* doc);
    void setPreviewLevel(LevelDocument* level, float ppu);
    void setAvailableLevels(const QStringList& levelNames);
    void setProjectRoot(const QString& root);

    UIDocument* document() const { return m_doc; }

signals:
    void documentModified();
    void previewLevelChanged(const QString& levelName);

public slots:
    void onAddWidget(const QString& type);
    void onDeleteSelected();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private slots:
    void onTreeSelectionChanged();

private:
    void rebuildTree();
    void rebuildPropsPanel(const QString& widgetId);

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
};
