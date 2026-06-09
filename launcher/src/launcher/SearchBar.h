#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QToolButton>

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent = nullptr);

signals:
    void filterChanged(const QString& text);
    void refreshRequested();

private:
    QLineEdit*   m_edit;
    QToolButton* m_sortBtn;
    QToolButton* m_refreshBtn;
};
