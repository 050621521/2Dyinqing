#pragma once
#include "models/UIDocument.h"
#include <QObject>
#include <QList>

struct UIInstance {
    QString     instanceId;
    QString     uiName;      // 创建时记录来源 UI 名
    UIDocument  docCopy;
    float       screenX = 0;
    float       screenY = 0;
    bool        shown   = false;
    QString followActorId;          // 空 = 不跟随
    float   followOffsetX = 0.0f;   // 画布像素：跟随时相对单位中心的水平偏移
    float   followOffsetY = 0.0f;   // 画布像素：负值=单位上方
};

class UIRuntime : public QObject {
    Q_OBJECT
public:
    explicit UIRuntime(const QString& projectRoot, QObject* parent = nullptr);
    ~UIRuntime() override;

    QString createInstance (const QString& uiName);
    void    showInstance   (const QString& instanceId);
    void    hideInstance   (const QString& instanceId);
    void    destroyInstance(const QString& instanceId);
    void    setText        (const QString& instanceId, const QString& widgetName, const QString& text);
    void    setValue       (const QString& instanceId, const QString& widgetName, float value);
    void    setPosition    (const QString& instanceId, float x, float y);
    void    setFollowActor   (const QString& instanceId, const QString& actorId, float offsetX, float offsetY);
    void    setFollowActorRef(const QString& ref,        const QString& actorId, float offsetX, float offsetY);
    void    clearFollow      (const QString& instanceId);
    void    setWidgetVisible(const QString& instanceId, const QString& widgetName, bool visible);

    QString showWidgetByName   (const QString& uiName, const QString& widgetName);
    void hideWidgetByName      (const QString& uiName, const QString& widgetName);
    void showByName            (const QString& uiName);
    void hideByName            (const QString& uiName);
    void destroyByName         (const QString& uiName);
    void setTextByName         (const QString& uiName, const QString& widgetName, const QString& text);
    void setValueByName        (const QString& uiName, const QString& widgetName, float value);
    void setPositionByName     (const QString& uiName, float x, float y);
    void setWidgetVisibleByName(const QString& uiName, const QString& widgetName, bool visible);

    const QList<UIInstance*>& shownInstances() const { return m_shown; }

    void notifyButtonClicked(const QString& instanceId, const QString& widgetName);
    void notifyDropdownChanged(const QString& instanceId, const QString& widgetName, int index);

signals:
    void uiStateChanged();
    void buttonClicked      (const QString& instanceId, const QString& widgetName);
    void dropdownChanged    (const QString& instanceId, const QString& widgetName, int index);

private:
    UIInstance* findInstance(const QString& instanceId);
    UIInstance* findByName  (const QString& uiName);

    QString            m_projectRoot;
    QList<UIInstance*> m_all;
    QList<UIInstance*> m_shown;
};
