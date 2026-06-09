#pragma once
#include "models/LevelDocument.h"
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

class SceneOutliner : public QWidget {
    Q_OBJECT
public:
    explicit SceneOutliner(QWidget* parent = nullptr);
    void loadLevel(LevelDocument* doc);
    void clear();

signals:
    void actorSelected(const ActorData& actor);
    void actorRemoved(const QString& id);
    void levelChanged();

private slots:
    void onItemClicked(QTreeWidgetItem* item, int col);
    void showContextMenu(const QPoint& pos);
    void onSearchChanged(const QString& text);

private:
    void rebuild();
    QTreeWidgetItem* groupItem(const QString& type) const;

    LevelDocument* m_doc = nullptr;
    QTreeWidget*   m_tree  = nullptr;
    QLineEdit*     m_search = nullptr;
};
