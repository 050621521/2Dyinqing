#pragma once
#include "models/LevelDocument.h"
#include "models/UIDocument.h"
#include "models/AnimationAsset.h"
#include "UIRuntime.h"
#include <QWidget>
#include <QHash>
#include <QPixmap>

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

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void    drawPrintLog(QPainter& p) const;
    QRectF  computeCameraRect(float aspect) const;
    QPointF cameraWorldToScreen(QPointF world, const QRectF& camRect,
                                const ActorData& cam) const;
    void    drawScene(QPainter& p, const QList<ActorData>& actors,
                      const ActorData& cam, const QRectF& camRect);
    void    renderUI(QPainter& p, const QRectF& camRect, const ActorData* cam) const;
    QRectF  widgetScreenRect(const UIWidget& w, const QRectF& parentRect) const;
    void    renderWidget(QPainter& p, const UIWidget& w,
                         const QRectF& parentRect, const UIDocument& doc) const;
    void    renderChildren(QPainter& p, const QString& parentId,
                           const QRectF& parentRect, const UIWidget& parent,
                           const UIDocument& doc) const;
    bool    hitTestWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                          const UIDocument& doc, QString& outWidget) const;
    bool    hitTestChildren(QPointF pos, const QString& parentId, QRectF parentRect,
                            const UIWidget& parent, const UIDocument& doc, QString& outWidget) const;

    UIRuntime*       m_uiRuntime   = nullptr;
    float            m_ppu         = 100.0f;
    LevelDocument*   m_doc         = nullptr;
    QList<ActorData> m_runtimeActors;
    bool             m_runtimeMode = false;
    QStringList      m_printLog;
    mutable QHash<QString, QPixmap> m_pixmapCache;
    mutable QHash<QString, AnimationAsset> m_animCache;
};
