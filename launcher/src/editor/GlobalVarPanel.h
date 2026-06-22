#pragma once
#include "GlobalVars.h"
#include <QWidget>
#include <functional>

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
    // 局部模式：注入数据源（绑定到某蓝图文档的局部变量）。设置后改用回调读写，
    // 并立即按新数据源重载。传空回调可解绑回全局(project.json)模式。
    void bindSource(std::function<QList<GlobalVarDef>()> loadFn,
                    std::function<void(const QList<GlobalVarDef>&)> saveFn);
    void reloadFromSource() { reload(); }   // 外部(如切换关卡)触发重载
    // 细节编辑控件（独立成一个"细节"停靠面板，不在变量列表面板里）
    QWidget* detailsWidget() const { return m_detailsWidget; }

signals:
    void changed();
    void varRenamed(const QString& oldName, const QString& newName);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;   // 行/名字点击 → 选中

private:
    void reload();
    void rebuildList();
    void onSelectionChanged();
    void onNameEdited();
    void onTypeChanged();
    void addVar();
    void removeSelected();
    void showContextMenu(const QPoint& pos);
    void commit();                 // m_vars → project.json + emit changed
    void fillTypeCombo(const QString& current);

    QString             m_projectRoot;
    QList<GlobalVarDef> m_vars;        // 内存模型
    // 局部模式数据源（为空=全局 project.json 模式）
    std::function<QList<GlobalVarDef>()>             m_loadFn;
    std::function<void(const QList<GlobalVarDef>&)>  m_saveFn;
    QListWidget*        m_list        = nullptr;
    QWidget*            m_detailsWidget = nullptr;  // 独立"细节"面板内容
    QLineEdit*          m_nameEdit    = nullptr;
    QComboBox*          m_typeCombo   = nullptr;
    bool                m_loading     = false;
};
