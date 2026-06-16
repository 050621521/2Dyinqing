#pragma once
#include <QDialog>

class QListWidget;

// 枚举编辑器：编辑某 .enum 资产的选项列表（增/删/改/上下移），确定即写回。
class EnumEditor : public QDialog {
    Q_OBJECT
public:
    explicit EnumEditor(const QString& enumPath, QWidget* parent = nullptr);

private:
    void addValue();
    void removeSelected();
    void move(int delta);
    void saveAndClose();

    QString      m_path;
    QListWidget* m_list = nullptr;
};
