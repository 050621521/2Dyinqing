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
    void showMultiSelection(const QList<ActorData>& actors);
    void assignSpritePath(const QString& path);
    void setProjectRoot(const QString& root);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

signals:
    void actorModified(const ActorData& actor);
    void actorsModified(const QList<ActorData>& actors);   // 多选批量编辑
    void editBpClassRequested(const QString& bpClass);
    // 请求把指定字段重置为默认（类实例→类默认值；内置/类本身→引擎硬默认）
    void actorFieldsReset(const QString& actorId, const QStringList& fields);

private slots:
    void onAnyFieldChanged();
    void applyMultiEdit();

private:
    void buildHeader(QVBoxLayout* root);
    void buildComponents(QVBoxLayout* root);
    void buildTransform(QVBoxLayout* root);
    void buildSpriteRenderer(QVBoxLayout* root);
    void buildAnimator(QVBoxLayout* root);
    void buildCameraComponent(QVBoxLayout* root);
    void buildFollowControl(QVBoxLayout* root);
    void buildConfiner(QVBoxLayout* root);
    void buildCollider(QVBoxLayout* root);
    void refreshComponentList();
    void refreshTagCombo();
    void manageTagsDialog();              // 标签管理（删除）
    void editColliderTargets();           // 弹多选菜单选目标标签
    void updateColliderTargetsLabel();
    void refreshSpriteSection();
    void refreshAnimSection();
    void reloadAnimClips(const QString& selectedClip);
    void refreshCameraSections();
    void onAddComponent(const QString& compName);

    static QColor typeColor(const QString& type);
    // 精灵渲染器组件涉及的全部字段（用于「重置组件」）
    static QStringList spriteRendererFields();
    // 当前对象的精灵颜色是否相对默认被改过（决定「↩」是否亮起）
    bool spriteColorIsOverridden() const;

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
    QPushButton* m_spriteColorResetBtn = nullptr;   // 颜色「↩」重置箭头
    QCheckBox*   m_flipXCheck      = nullptr;
    QCheckBox*   m_flipYCheck      = nullptr;
    QComboBox*   m_sortLayerCombo  = nullptr;
    QSpinBox*    m_orderSpin       = nullptr;
    QComboBox*   m_drawModeCombo   = nullptr;

    // 动画器区块
    QWidget*     m_animBox          = nullptr;
    QLabel*      m_animAssetLabel   = nullptr;
    QComboBox*   m_animClipCombo    = nullptr;
    QCheckBox*   m_animAutoPlayChk  = nullptr;

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

    // 碰撞盒组件
    QWidget*        m_colliderBox        = nullptr;
    QCheckBox*      m_colliderEnabledChk = nullptr;
    QDoubleSpinBox* m_colliderWSpin      = nullptr;
    QDoubleSpinBox* m_colliderHSpin      = nullptr;
    QDoubleSpinBox* m_colliderOffXSpin   = nullptr;
    QDoubleSpinBox* m_colliderOffYSpin   = nullptr;
    QComboBox*      m_colliderRespCombo  = nullptr;
    QPushButton*    m_colliderTargetsBtn = nullptr;   // 多选目标标签
    QString         m_colliderTargetsValue;           // 当前选中标签（逗号分隔）

    ActorData       m_currentActor;
    QStackedWidget* m_stack     = nullptr;
    QLabel*         m_emptyHint = nullptr;
    QString         m_projectRoot;

    // 多选批量编辑页（stack index 2）：仅编辑共有的变换属性
    void buildMultiPage();
    QList<ActorData> m_multiActors;
    QLabel*    m_multiHint = nullptr;
    QLineEdit* m_mPosX     = nullptr;
    QLineEdit* m_mPosY     = nullptr;
    QLineEdit* m_mRot      = nullptr;
    QLineEdit* m_mScaleX   = nullptr;
    QLineEdit* m_mScaleY   = nullptr;
};
