#pragma once
#include "models/LevelDocument.h"
#include "models/UIDocument.h"
#include "models/AnimationAsset.h"
#include "UIRuntime.h"
#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <QPoint>

class GameViewport : public QWidget {
    Q_OBJECT
public:
    explicit GameViewport(QWidget* parent = nullptr);

    // 世界坐标 → 屏幕坐标（与内部相机投影一致，供跟随与测试复用）
    static QPointF worldToScreen(QPointF world, const QRectF& camRect, const ActorData& cam);

    void loadLevel(LevelDocument* doc);
    void setRuntimeActors(const QList<ActorData>& actors);
    void setRuntimeMode(bool on);
    void setUIRuntime(UIRuntime* ui);
    void setPixelsPerUnit(float ppu);
    void syncPrintLog(const QStringList& log);
    void clearPrintLog();

signals:
    void keyPressed(const QString& key);
    void keyReleased(const QString& key);
    void mousePressedInGame(float screenX, float screenY, float worldX, float worldY, const QString& button);
    void mouseReleasedInGame(float screenX, float screenY, float worldX, float worldY, const QString& button);
    void mouseMovedInGame(float screenX, float screenY, float worldX, float worldY);
    void mouseDraggedInGame(float screenX, float screenY, float worldX, float worldY, const QString& button);
    void mouseWheeledInGame(float screenX, float screenY, float worldX, float worldY, float deltaX, float deltaY);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void    drawPrintLog(QPainter& p) const;
    QRectF  computeCameraRect(float aspect) const;
    QPointF cameraWorldToScreen(QPointF world, const QRectF& camRect,
                                const ActorData& cam) const;
    QPointF cameraScreenToWorld(QPointF screen, const QRectF& camRect,
                                const ActorData& cam) const;
    bool    currentCameraContext(QRectF& outCamRect, ActorData& outCam) const;
    QPointF screenToWorldOrSelf(QPointF screen) const;
    void    drawScene(QPainter& p, const QList<ActorData>& actors,
                      const ActorData& cam, const QRectF& camRect);
    void    renderUI(QPainter& p, const QRectF& camRect, const ActorData* cam) const;
    QRectF  widgetScreenRect(const UIWidget& w, const QRectF& parentRect) const;
    void    renderWidget(QPainter& p, const UIWidget& w,
                         const QRectF& parentRect, const UIDocument& doc) const;
    void    renderDragGhost(QPainter& p, const QRectF& camRect, const ActorData* cam) const;
    void    renderChildren(QPainter& p, const QString& parentId,
                           const QRectF& parentRect, const UIWidget& parent,
                           const UIDocument& doc) const;
    bool    hitTestWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                          const UIDocument& doc, QString& outWidget) const;
    bool    hitTestChildren(QPointF pos, const QString& parentId, QRectF parentRect,
                            const UIWidget& parent, const UIDocument& doc, QString& outWidget) const;
    QRectF  childrenBounds(const QString& parentId, const UIDocument& doc) const;
    float   maxScrollX(const UIWidget& w, const UIDocument& doc) const;
    float   maxScrollY(const UIWidget& w, const UIDocument& doc) const;
    QRectF  horizontalScrollThumbRect(const UIWidget& w, const QRectF& r,
                                      const UIDocument& doc) const;
    QRectF  verticalScrollThumbRect(const UIWidget& w, const QRectF& r,
                                    const UIDocument& doc) const;
    bool    hitTestScrollThumb(QPointF pos, const UIWidget& w, QRectF parentRect,
                               const UIDocument& doc, QString& outWidget,
                               Qt::Orientation& outOrientation) const;
    bool    hitTestScrollWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                                const UIDocument& doc, QString& outWidget) const;
    bool    hitTestAnyWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                             const UIDocument& doc, QString& outWidget) const;
    const UIWidget* findWidgetByName(const UIDocument& doc, const QString& name) const;
    const UIWidget* findWidgetById(const UIDocument& doc, const QString& id) const;
    QString cardRootForWidget(const UIDocument& doc, const QString& widgetName) const;
    bool    widgetRectByName(const QString& widgetName, const UIWidget& w,
                             QRectF parentRect, const UIDocument& doc, QRectF& outRect) const;
    QPointF toCanonicalPos(const QPointF& pos, const QRectF& camRect,
                           float sx, float sy, const UIInstance* inst) const;

    UIRuntime*       m_uiRuntime   = nullptr;
    float            m_ppu         = 100.0f;
    LevelDocument*   m_doc         = nullptr;
    QList<ActorData> m_runtimeActors;
    bool             m_runtimeMode = false;
    QStringList      m_printLog;
    mutable QHash<QString, QPixmap> m_pixmapCache;
    mutable QHash<QString, AnimationAsset> m_animCache;
    QString m_scrollInstanceId;
    QString m_scrollWidgetName;
    QPointF m_scrollPressPos;
    float   m_scrollStartX = 0.0f;
    float   m_scrollStartY = 0.0f;
    bool    m_scrollDragging = false;
    bool    m_scrollThumbDragging = false;
    Qt::Orientation m_scrollThumbOrientation = Qt::Horizontal;
    QString m_dragInstanceId;
    QString m_dragWidgetName;
    QString m_dragVisualWidgetName;
    QPointF m_dragPressCanonical;
    QPointF m_dragVisualOffset;
    QPointF m_dragVisualCanonical;
    bool    m_uiDragActive = false;
};
