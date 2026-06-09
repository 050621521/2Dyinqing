#pragma once
#include "models/ProjectInfo.h"
#include <QWidget>
#include <QListWidget>

class CategoryList : public QWidget {
    Q_OBJECT
public:
    explicit CategoryList(QWidget* parent = nullptr);
    void setCategories(const QList<TemplateCategory>& cats);

signals:
    void categorySelected(const QString& categoryId);

private:
    QListWidget* m_list;
    QList<TemplateCategory> m_categories;
};
