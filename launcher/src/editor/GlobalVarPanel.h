#pragma once
#include "GlobalVars.h"
#include <QWidget>

class QListWidget;
class QLineEdit;
class QComboBox;

// 「我的蓝图」风格变量面板：上半变量列表（带类型色块）+ 下半细节（名字/类型/删除）。
// 只管变量；枚举改由内容浏览器管理，类型下拉从工程扫到的枚举追加。
class GlobalVarPanel : public QWidget {
    Q_OBJECT
public:
    explicit GlobalVarPanel(QWidget* parent = nullptr);
    void setProjectRoot(const QString& root);
    void refreshEnums();   // 枚举资产变化时刷新类型下拉

signals:
    void changed();
    void varRenamed(const QString& oldName, const QString& newName);

private:
    void reload();
    void rebuildList();
    void onSelectionChanged();
    void onNameEdited();
    void onTypeChanged();
    void addVar();
    void removeSelected();
    void commit();                 // m_vars → project.json + emit changed
    void fillTypeCombo(const QString& current);

    QString             m_projectRoot;
    QList<GlobalVarDef> m_vars;     // 内存模型
    QListWidget*        m_list      = nullptr;
    QLineEdit*          m_nameEdit  = nullptr;
    QComboBox*          m_typeCombo = nullptr;
    bool                m_loading   = false;
};
