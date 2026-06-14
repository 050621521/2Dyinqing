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
    void editBpClassRequested(const QString& bpClass);

private slots:
    void onAnyFieldChanged();

private:
    void buildHeader(QVBoxLayout* root);
    void buildComponents(QVBoxLayout* root);
    void buildTransform(QVBoxLayout* root);
    void buildSpriteRenderer(QVBoxLayout* root);
    void buildCameraComponent(QVBoxLayout* root);
    void buildFollowControl(QVBoxLayout* root);
    void buildConfiner(QVBoxLayout* root);
    void refreshComponentList();
    void refreshSpriteSection();
    void refreshCameraSections();
    void onAddComponent(const QString& compName);

    static QColor typeColor(const QString& type);

    QPushButton*    m_editBpBtn   = nullptr;
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

    // 摄像机组件区块
    QWidget*        m_cameraBox          = nullptr;
    QCheckBox*      m_cameraOrthoCheck   = nullptr;
    QDoubleSpinBox* m_cameraSizeSpin     = nullptr;
    QCheckBox*      m_cameraIsMainCheck  = nullptr;
    QSpinBox*       m_cameraResWSpin     = nullptr;
    QSpinBox*       m_cameraResHSpin     = nullptr;
    QPushButton*    m_cameraBgBtn        = nullptr;

    // 跟随控制组件区块
    QWidget*        m_followBox          = nullptr;
    QLineEdit*      m_followTargetEdit   = nullptr;
    QDoubleSpinBox* m_followLerpSpin     = nullptr;
    QDoubleSpinBox* m_followOffsetXSpin  = nullptr;
    QDoubleSpinBox* m_followOffsetYSpin  = nullptr;

    // 边界限制组件区块
    QWidget*        m_confinerBox        = nullptr;
    QCheckBox*      m_confinerEnabledChk = nullptr;
    QLineEdit*      m_confinerActorEdit  = nullptr;
    QDoubleSpinBox* m_confinerMinXSpin   = nullptr;
    QDoubleSpinBox* m_confinerMaxXSpin   = nullptr;
    QDoubleSpinBox* m_confinerMinYSpin   = nullptr;
    QDoubleSpinBox* m_confinerMaxYSpin   = nullptr;

    ActorData       m_currentActor;
    QStackedWidget* m_stack = nullptr;
    QString         m_projectRoot;
};
