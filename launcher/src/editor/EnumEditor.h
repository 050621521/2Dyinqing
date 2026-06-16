#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;

// 枚举编辑器（中央页签，仿虚幻 User Defined Enum）：
// 每个枚举值一行（显示命名 + 描述 + 删除），顶部"添加枚举器"；改动即自动写回。
class EnumEditor : public QWidget {
    Q_OBJECT
public:
    explicit EnumEditor(QWidget* parent = nullptr);
    void load(const QString& enumPath);

signals:
    void changed();

private:
    void appendRow(const QString& name, const QString& desc);
    void addValue();
    void save();

    QString       m_path;
    QLabel*       m_title = nullptr;
    QTableWidget* m_table = nullptr;
    bool          m_loading = false;
};
