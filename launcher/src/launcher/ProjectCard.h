#pragma once
#include "models/ProjectInfo.h"
#include <QFrame>
#include <QLabel>

class ProjectCard : public QFrame {
    Q_OBJECT
public:
    explicit ProjectCard(const ProjectInfo& info, QWidget* parent = nullptr);

    const ProjectInfo& projectInfo() const { return m_info; }
    void setSelected(bool selected);

signals:
    void clicked(const ProjectInfo& info);
    void doubleClicked(const ProjectInfo& info);
    void removeRequested(const ProjectInfo& info);
    void deleteRequested(const ProjectInfo& info);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    ProjectInfo m_info;
    QLabel*     m_thumbnail;
    QLabel*     m_name;
    QLabel*     m_tag;
    bool        m_selected = false;

    void updateStyle();
};
