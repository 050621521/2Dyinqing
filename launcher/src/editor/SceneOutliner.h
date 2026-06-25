#pragma once
#include "models/LevelDocument.h"
#include <QWidget>
#include <QUndoStack>
#include <functional>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

class SceneOutliner : public QWidget {
    Q_OBJECT
public:
    explicit SceneOutliner(QWidget* parent = nullptr);
    void loadLevel(LevelDocument* doc);
    void clear();
    void setUndoStack(QUndoStack* stack, std::function<void()> refresh);
    void setSelectedIds(const QStringList& ids);   // 视口选区同步到大纲高亮（不回环发信号）

signals:
    void actorSelected(const ActorData& actor);
    void selectionChanged(QStringList ids);        // 大纲多选变化
    void copyRequested();                          // 右键菜单「复制」
    void pasteRequested();                         // 右键菜单「粘贴」
    void actorRemoved(const QString& id);
    void levelChanged();

private slots:
    void onSelectionChanged();
    void showContextMenu(const QPoint& pos);
    void onSearchChanged(const QString& text);

private:
    void rebuild();

    LevelDocument*        m_doc     = nullptr;
    QTreeWidget*          m_tree    = nullptr;
    QLineEdit*            m_search  = nullptr;
    QUndoStack*           m_undoStack = nullptr;
    std::function<void()> m_onRefresh;
};
