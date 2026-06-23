#include "ProjectCard.h"
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPixmap>
#include <QStyle>

static const int CARD_W = 120;
static const int CARD_H = 140;
static const int THUMB_H = 90;

ProjectCard::ProjectCard(const ProjectInfo& info, QWidget* parent)
    : QFrame(parent), m_info(info)
{
    setFixedSize(CARD_W, CARD_H);
    setObjectName("projectCard");
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 4);
    layout->setSpacing(2);

    m_thumbnail = new QLabel(this);
    m_thumbnail->setFixedSize(CARD_W - 4, THUMB_H);
    m_thumbnail->setAlignment(Qt::AlignCenter);
    m_thumbnail->setObjectName("cardThumb");

    if (!info.thumbnailPath.isEmpty()) {
        QPixmap px(info.thumbnailPath);
        if (!px.isNull())
            m_thumbnail->setPixmap(px.scaled(m_thumbnail->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }

    m_name = new QLabel(info.name, this);
    m_name->setObjectName("cardName");
    m_name->setWordWrap(true);
    m_name->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_tag = new QLabel("当前", this);
    m_tag->setObjectName("cardTag");
    m_tag->setVisible(false);

    layout->addWidget(m_thumbnail);
    layout->addWidget(m_name);
    layout->addWidget(m_tag);

    updateStyle();
}

void ProjectCard::setSelected(bool selected) {
    m_selected = selected;
    m_tag->setVisible(selected);
    updateStyle();
}

void ProjectCard::updateStyle() {
    setProperty("selected", m_selected);
    style()->unpolish(this);
    style()->polish(this);
}

void ProjectCard::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        emit clicked(m_info);
    QFrame::mousePressEvent(e);
}

void ProjectCard::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        emit doubleClicked(m_info);
    QFrame::mouseDoubleClickEvent(e);
}

void ProjectCard::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    menu.setObjectName("cardContextMenu");
    QAction* moveAct = menu.addAction("移动到…");
    menu.addSeparator();
    QAction* removeAct = menu.addAction("从列表中移除");
    menu.addSeparator();
    QAction* deleteAct = menu.addAction("彻底删除项目文件夹…");

    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == moveAct)
        emit moveRequested(m_info);
    else if (chosen == removeAct)
        emit removeRequested(m_info);
    else if (chosen == deleteAct)
        emit deleteRequested(m_info);
}

void ProjectCard::enterEvent(QEnterEvent* e) {
    if (!m_selected) {
        setProperty("hovered", true);
        style()->unpolish(this);
        style()->polish(this);
    }
    QFrame::enterEvent(e);
}

void ProjectCard::leaveEvent(QEvent* e) {
    setProperty("hovered", false);
    style()->unpolish(this);
    style()->polish(this);
    QFrame::leaveEvent(e);
}
