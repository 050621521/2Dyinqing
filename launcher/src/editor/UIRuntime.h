#pragma once
#include "models/UIDocument.h"
#include <QObject>
#include <QList>

struct UIInstance {
    QString     instanceId;
    UIDocument  docCopy;
    float       screenX = 0;
    float       screenY = 0;
    bool        shown   = false;
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
    void    setWidgetVisible(const QString& instanceId, const QString& widgetName, bool visible);

    const QList<UIInstance*>& shownInstances() const { return m_shown; }

signals:
    void uiStateChanged();
    void buttonClicked      (const QString& instanceId, const QString& widgetName);
    void dropdownChanged    (const QString& instanceId, const QString& widgetName, int index);

private:
    UIInstance* findInstance(const QString& instanceId);

    QString            m_projectRoot;
    QList<UIInstance*> m_all;
    QList<UIInstance*> m_shown;
};
