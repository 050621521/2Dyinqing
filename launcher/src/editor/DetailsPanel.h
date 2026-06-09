#pragma once
#include "models/LevelDocument.h"
#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>

class QVBoxLayout;

class DetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailsPanel(QWidget* parent = nullptr);
    void showActor(const ActorData& actor);
    void clearActor();
    void assignSpritePath(const QString& path);
    void setProjectRoot(const QString& root);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

signals:
    void actorModified(const ActorData& actor);

private slots:
    void onAnyFieldChanged();

private:
    void buildHeader(QVBoxLayout* root);
    void buildComponents(QVBoxLayout* root);
    void buildTransform(QVBoxLayout* root);
    void buildSpriteRenderer(QVBoxLayout* root);
    void refreshComponentList();
    void refreshSpriteSection();
    void onAddComponent(const QString& compName);

    static QColor typeColor(const QString& type);

    QLabel*         m_iconLabel   = nullptr;
    QCheckBox*      m_activeCheck = nullptr;
    QLineEdit*      m_nameEdit    = nullptr;
    QCheckBox*      m_staticCheck = nullptr;
    QComboBox*      m_tagCombo    = nullptr;
    QComboBox*      m_layerCombo  = nullptr;

    QDoubleSpinBox* m_posX     = nullptr;
    QDoubleSpinBox* m_posY     = nullptr;
    QDoubleSpinBox* m_rotation = nullptr;
    QDoubleSpinBox* m_scaleX   = nullptr;
    QDoubleSpinBox* m_scaleY   = nullptr;

    QTreeWidget* m_componentTree   = nullptr;
    QPushButton* m_addComponentBtn = nullptr;

    // 精灵渲染器区块
    QWidget*     m_spriteBox       = nullptr;
    QLabel*      m_spritePathLabel = nullptr;
    QPushButton* m_spriteColorBtn  = nullptr;
    QCheckBox*   m_flipXCheck      = nullptr;
    QCheckBox*   m_flipYCheck      = nullptr;
    QComboBox*   m_sortLayerCombo  = nullptr;
    QSpinBox*    m_orderSpin       = nullptr;
    QComboBox*   m_drawModeCombo   = nullptr;

    ActorData       m_currentActor;
    QStackedWidget* m_stack = nullptr;
    QString         m_projectRoot;
};
