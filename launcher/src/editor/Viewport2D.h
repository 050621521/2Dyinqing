#pragma once
#include "models/LevelDocument.h"
#include <QWidget>
#include <QHash>
#include <QPixmap>

class Viewport2D : public QWidget {
    Q_OBJECT
public:
    enum class ToolMode { Select, Move, Rotate, Scale };

    explicit Viewport2D(QWidget* parent = nullptr);

    void loadLevel(LevelDocument* doc);
    void setSelectedId(const QString& id);
    void setToolMode(ToolMode mode);

    void setRuntimeMode(bool on, const QList<ActorData>& actors = {});
    void updateRuntimeActors(const QList<ActorData>& actors);
    void syncPrintLog(const QStringList& log);
    void clearPrintLog();

signals:
    void actorSelected(const ActorData& actor);
    void actorTransformed(const ActorData& actor);
    void actorDragging(const ActorData& actor);
    void actorCreated(const ActorData& actor);
    void keyPressed(const QString& key);

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
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
    QString        m_selectedId;
    mutable QHash<QString, QPixmap> m_pixmapCache;

    bool             m_runtimeMode = false;
    QList<ActorData> m_runtimeActors;
    QStringList      m_printLog;

    void applyToolCursor();
    void drawGrid(QPainter& p);
    void drawAxes(QPainter& p);
    void drawOriginMark(QPainter& p);
    void drawActors(QPainter& p);
    void drawGizmo(QPainter& p, const ActorData& a, const QRectF& rect, const QPointF& pos);
    void drawPrintLog(QPainter& p);

    QPointF worldToScreen(QPointF world) const;
    QPointF screenToWorld(QPointF screen) const;
};
