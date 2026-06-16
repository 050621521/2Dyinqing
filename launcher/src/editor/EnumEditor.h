#pragma once
#include <QWidget>

class QListWidget;
class QLabel;

// 枚举编辑器（中央页签）：编辑某 .enum 资产的选项，改动即自动写回并发信号。
class EnumEditor : public QWidget {
    Q_OBJECT
public:
    explicit EnumEditor(QWidget* parent = nullptr);
    void load(const QString& enumPath);   // 切到该枚举资产

signals:
    void changed();   // 选项变化 → EditorWindow 重扫枚举

private:
    void addValue();
    void removeSelected();
    void move(int delta);
    void save();

    QString      m_path;
    QLabel*      m_title = nullptr;
    QListWidget* m_list  = nullptr;
    bool         m_loading = false;
};
