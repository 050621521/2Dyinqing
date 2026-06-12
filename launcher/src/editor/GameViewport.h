#pragma once
#include "models/LevelDocument.h"
#include <QWidget>
#include <QHash>
#include <QPixmap>

class GameViewport : public QWidget {
    Q_OBJECT
public:
    explicit GameViewport(QWidget* parent = nullptr);

    void loadLevel(LevelDocument* doc);
    void setRuntimeActors(const QList<ActorData>& actors);
    void setRuntimeMode(bool on);
    void setUIRuntime(class UIRuntime*) {}   // 临时空桩，Task 7 实现
    void setPixelsPerUnit(float ppu);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QRectF  computeCameraRect(float aspect) const;
    QPointF cameraWorldToScreen(QPointF world, const QRectF& camRect,
                                const ActorData& cam) const;
    void    drawScene(QPainter& p, const QList<ActorData>& actors,
                      const ActorData& cam, const QRectF& camRect);

    float            m_ppu         = 100.0f;
    LevelDocument*   m_doc         = nullptr;
    QList<ActorData> m_runtimeActors;
    bool             m_runtimeMode = false;
    mutable QHash<QString, QPixmap> m_pixmapCache;
};
