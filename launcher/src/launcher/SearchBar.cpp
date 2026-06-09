#include "SearchBar.h"
#include <QHBoxLayout>

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText("过滤项目……");
    m_edit->setObjectName("searchEdit");

    m_sortBtn    = new QToolButton(this);
    m_sortBtn->setText("⇅");
    m_sortBtn->setObjectName("iconBtn");

    m_refreshBtn = new QToolButton(this);
    m_refreshBtn->setText("↺");
    m_refreshBtn->setObjectName("iconBtn");

    layout->addWidget(m_edit, 1);
    layout->addWidget(m_sortBtn);
    layout->addWidget(m_refreshBtn);

    connect(m_edit, &QLineEdit::textChanged, this, &SearchBar::filterChanged);
    connect(m_refreshBtn, &QToolButton::clicked, this, &SearchBar::refreshRequested);
}
