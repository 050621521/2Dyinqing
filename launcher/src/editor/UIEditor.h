#pragma once
#include "models/UIDocument.h"
#include "models/LevelDocument.h"
#include <QWidget>
#include <QTreeWidget>
#include <QScrollArea>
#include <QComboBox>
#include <QHash>
#include <QPixmap>
#include <QSplitter>
#include <functional>

// ── UIEditorCanvas（独立类，避免 MOC 嵌套限制）────────────────────────────
class UIEditorCanvas : public QWidget {
    Q_OBJECT
public:
    explicit UIEditorCanvas(QWidget* parent = nullptr);
    void setDoc(UIDocument* doc);
    void setPreviewLevel(LevelDocument* level, float ppu);
    void setSelectedId(const QString& id);
    QString selectedId() const { return m_selectedId; }

    // 回调，由 UIEditor 赋值
    std::function<void(const QString&)>           onSelectionChanged;
    std::function<void(const QString&, float, float)> onWidgetMoved;
    std::function<void(const QString&)>           onAddWidget;
    std::function<void()>                         onDeleteSelected;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    QRectF  widgetScreenRect(const UIWidget& w, const QRectF& parentRect) const;
    void    renderWidget(QPainter& p, const UIWidget& w, const QRectF& parentRect) const;
    void    renderChildren(QPainter& p, const QString& parentId,
                           const QRectF& parentRect, const UIWidget& parent) const;
    QString hitTest(QPointF pos, const QString& parentId, const QRectF& parentRect) const;

    UIDocument*    m_doc       = nullptr;
    LevelDocument* m_level     = nullptr;
    float          m_ppu       = 100.0f;
    QString        m_selectedId;
    bool           m_dragging  = false;
    QPointF        m_dragOffset;
    mutable QHash<QString, QPixmap> m_pixmapCache;
};

// ── UIEditor ──────────────────────────────────────────────────────────────
class UIEditor : public QWidget {
    Q_OBJECT
public:
    explicit UIEditor(QWidget* parent = nullptr);

    void loadDocument(UIDocument* doc);
    void setPreviewLevel(LevelDocument* level, float ppu);

    UIDocument* document() const { return m_doc; }

signals:
    void documentModified();

public slots:
    void onAddWidget(const QString& type);
    void onDeleteSelected();

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
};
