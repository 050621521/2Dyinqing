#pragma once
#include "GlobalVars.h"
#include <QWidget>

class QTableWidget;

// 常驻「全局变量」面板：声明的增/删/改名/改类型，变更即写回 project.json 并发信号。
class GlobalVarPanel : public QWidget {
    Q_OBJECT
public:
    explicit GlobalVarPanel(QWidget* parent = nullptr);
    void setProjectRoot(const QString& root);

signals:
    void changed();                                  // 声明变化（增删/改名/改类型）
    void varRenamed(const QString& oldName, const QString& newName);  // 改名（同步引用节点）

private:
    void reload();    // 磁盘 → 表
    void commit();    // 表 → 磁盘 + emit changed
    void addRow();
    void removeSelected();

    QString       m_projectRoot;
    QTableWidget* m_table   = nullptr;
    bool          m_loading = false;
};
