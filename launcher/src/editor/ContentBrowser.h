#pragma once
#include <QWidget>
#include <QHash>
#include <QIcon>

class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QLabel;

class ContentBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ContentBrowser(const QString& projectRoot, QWidget* parent = nullptr);

signals:
    void levelOpenRequested(const QString& levelPath);
    void levelFileDeleted(const QString& levelPath);
    void saveAllRequested();
    void imageAssignRequested(const QString& imagePath);

private slots:
    void onFolderSelected(QTreeWidgetItem* item, int col);
    void onGridItemDoubleClicked(QListWidgetItem* item);
    void onSearchChanged(const QString& text);
    void onNewLevel();
    void onNewFolder();
    void showGridContextMenu(const QPoint& pos);

private:
    void buildFolderTree();
    void addDirToTree(QTreeWidgetItem* parent, const QString& absPath, int depth);
    void populateFolder(const QString& absPath);
    void updateBreadcrumb();
    bool createLevelFile(const QString& filePath);
    static QIcon makeFolderIcon();
    static QIcon makeLevelIcon();
    QIcon makeImageIcon(const QString& path);

    QString      m_projectRoot;
    QString      m_currentPath;
    QStringList  m_currentEntries;
    QStringList  m_currentTypes;

    QTreeWidget* m_folderTree  = nullptr;
    QListWidget* m_assetGrid   = nullptr;
    QLineEdit*   m_searchEdit  = nullptr;
    QLabel*      m_bcLabel     = nullptr;
    QLabel*      m_countLabel  = nullptr;

    QHash<QString, QIcon> m_imageIconCache;
};
