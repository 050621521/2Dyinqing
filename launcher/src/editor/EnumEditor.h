#pragma once
#include <QWidget>
#include <QList>

class QVBoxLayout;
class QLineEdit;
class QLabel;

// 枚举编辑器（中央页签，仿虚幻 User Defined Enum）：
// 顶部"添加枚举器"；「描述」区(列举描述) + 「枚举值」区(每值一行：显示命名 + 描述 + 删除)。
class EnumEditor : public QWidget {
    Q_OBJECT
public:
    explicit EnumEditor(QWidget* parent = nullptr);
    void load(const QString& enumPath);

signals:
    void changed();

private:
    struct Row { QWidget* w; QLineEdit* name; QLineEdit* desc; };
    void appendRow(const QString& name, const QString& desc);
    void removeRow(QWidget* rowW);
    void addValue();
    void save();

    QString       m_path;
    QLabel*       m_title    = nullptr;
    QLineEdit*    m_enumDesc = nullptr;   // 列举描述
    QVBoxLayout*  m_rowsLay  = nullptr;   // 枚举值行容器
    QList<Row>    m_rows;
    bool          m_loading  = false;
};
