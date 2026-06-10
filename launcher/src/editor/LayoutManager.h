#pragma once
#include <QObject>
#include <QMap>
#include <QByteArray>
#include <QStringList>

namespace ads { class CDockManager; }

class LayoutManager : public QObject {
    Q_OBJECT
public:
    explicit LayoutManager(ads::CDockManager* dock,
                           const QString& projectPath,
                           QObject* parent = nullptr);

    void captureDefault();

    QStringList layoutNames() const;
    QString     currentLayout() const;

    void saveLayout(const QString& name);
    bool loadLayout(const QString& name);
    void deleteLayout(const QString& name);
    void resetDefault();

signals:
    void layoutListChanged();
    void currentLayoutChanged(const QString& name);

private:
    void readFile();
    void writeFile();

    ads::CDockManager*     m_dock;
    QString                m_filePath;
    QString                m_current;
    QMap<QString, QString> m_layouts;
    QByteArray             m_defaultState;
};
