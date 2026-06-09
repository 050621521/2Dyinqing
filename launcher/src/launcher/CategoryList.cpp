#include "CategoryList.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidgetItem>

CategoryList::CategoryList(QWidget* parent) : QWidget(parent) {
    setFixedWidth(220);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_list = new QListWidget(this);
    m_list->setObjectName("categoryList");
    m_list->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_categories.size())
            emit categorySelected(m_categories[row].id);
    });
}

void CategoryList::setCategories(const QList<TemplateCategory>& cats) {
    m_categories = cats;
    m_list->clear();

    for (int i = 0; i < cats.size(); ++i) {
        const auto& cat = cats[i];
        auto* item = new QListWidgetItem(cat.displayName, m_list);
        item->setSizeHint(QSize(220, i == 0 ? 72 : 80));
        m_list->addItem(item);
    }

    if (!cats.isEmpty())
        m_list->setCurrentRow(0);
}
