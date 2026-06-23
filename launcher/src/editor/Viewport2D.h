#pragma once
#include "models/LevelDocument.h"
#include "models/AnimationAsset.h"
#include <QWidget>
#include <QHash>
#include <QSet>
#include <QPixmap>
#include <QUndoStack>
#include <functional>

class Viewport2D : public QWidget {
    Q_OBJECT
public:
    enum class ToolMode { Select, Move, Rotate, Scale };

    explicit Viewport2D(QWidget* parent = nullptr);

    void loadLevel(LevelDocument* doc);
    void setSelectedId(const QString& id);
    void setToolMode(ToolMode mode);
    void setGridSnap(bool enabled, float size);
    void setRotSnap(bool enabled, float angle);
    void alignSelected(int type); // 0=左 1=右 2=水平居中 3=上 4=下 5=垂直居中

    int  selectionCount() const { return m_selectedIds.size(); }

    void setUndoStack(QUndoStack* stack, std::function<void()> refresh);
    void frameSelected();
    void selectAll();
    void duplicateSelected();
    void deleteSelected();
    void clearSelection();

    void setRuntimeMode(bool on, const QList<ActorData>& actors = {});
    void updateRuntimeActors(const QList<ActorData>& actors);
    void syncPrintLog(const QStringList& log);
    void clearPrintLog();
    void setPixelsPerUnit(float ppu);

signals:
    void actorSelected(const ActorData& actor);
    void selectionChanged(QStringList ids);
    void actorsAligned(QList<ActorData> actors);
    void actorTransformed(const ActorData& actor);
    void actorDragging(const ActorData& actor);
    void actorCreated(const ActorData& actor);
    void actorRemoved(const QString& id);
    void bpClassDropped(const QString& bpPath, const QPointF& worldPos);
    void keyPressed(const QString& key);
    void keyReleased(const QString& key);
    void toolModeChanged(ToolMode mode);

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    float   m_ppu    = 100.0f;
    float   m_zoom   = 1.0f;
    QPointF m_offset = {0, 0};
    QPoint  m_lastMouse;
    bool    m_panning  = false;
    bool    m_dragging = false;
    QPointF m_dragStart;
    QString m_dragActorId;

    enum class ScaleHandle { None, Center, AxisX, AxisY };

    ToolMode    m_toolMode        = ToolMode::Select;
    QPointF     m_dragAnchor;
    float       m_dragRotStart    = 0.0f;
    ScaleHandle m_scaleHandle     = ScaleHandle::None;
    float       m_dragScaleStartX = 1.0f;
    float       m_dragScaleStartY = 1.0f;

    LevelDocument* m_doc = nullptr;
    QString        m_selectedId;    // 主选（Gizmo 绘制目标）
    QSet<QString>  m_selectedIds;   // 完整选区
    mutable QHash<QString, QPixmap> m_pixmapCache;
    mutable QHash<QString, AnimationAsset> m_animCache;

    bool             m_runtimeMode = false;
    QList<ActorData> m_runtimeActors;
    QStringList      m_printLog;

    // 吸附
    bool  m_gridSnapEnabled = true;
    float m_gridSnapSize    = 50.0f;
    bool  m_rotSnapEnabled  = true;
    float m_rotSnapAngle    = 15.0f;

    // 框选
    bool   m_rubberBanding = false;
    QPoint m_rubberStart;
    QRect  m_rubberRect;

    // 多 Actor 拖拽起始位置
    QHash<QString, QPointF> m_dragStartPositions;
    QPointF                 m_dragMouseStartWorld;

    // 拖拽前的 Actor 快照（用于 undo）
    QList<ActorData>      m_dragBeforeActors;
    QUndoStack*           m_undoStack = nullptr;
    std::function<void()> m_onRefresh;

    void applyToolCursor();
    void drawGrid(QPainter& p);
    void drawAxes(QPainter& p);
    void drawOriginMark(QPainter& p);
    void drawActors(QPainter& p);
    void drawGizmo(QPainter& p, const ActorData& a, const QRectF& rect, const QPointF& pos);
    void drawPrintLog(QPainter& p);
    void drawSelectionOverlay(QPainter& p);

    QRectF  actorScreenRect(const ActorData& a) const;
    QPointF worldToScreen(QPointF world) const;
    QPointF screenToWorld(QPointF screen) const;
};
