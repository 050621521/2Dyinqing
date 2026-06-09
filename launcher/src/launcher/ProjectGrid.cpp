#include "ProjectGrid.h"
#include "ProjectCard.h"
#include <QGridLayout>
#include <QWidget>

static const int COL_COUNT = 6;
static const int SPACING   = 8;

ProjectGrid::ProjectGrid(QWidget* parent) : QScrollArea(parent) {
    setObjectName("projectGrid");
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);

    m_container = new QWidget(this);
    m_container->setObjectName("gridContainer");
    setWidget(m_container);
}

void ProjectGrid::setProjects(const QList<ProjectInfo>& projects) {
    m_allProjects = projects;
    m_selected = nullptr;
    filter(m_filterText);
}

void ProjectGrid::filter(const QString& text) {
    m_filterText = text;
    m_filtered.clear();

    for (const auto& p : m_allProjects) {
        if (text.isEmpty() || p.name.contains(text, Qt::CaseInsensitive))
            m_filtered.append(p);
    }
    rebuild();
}

void ProjectGrid::rebuild() {
    qDeleteAll(m_cards);
    m_cards.clear();
    m_selected = nullptr;

    delete m_container->layout();
    auto* grid = new QGridLayout(m_container);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setSpacing(SPACING);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    for (int i = 0; i < m_filtered.size(); ++i) {
        auto* card = new ProjectCard(m_filtered[i], m_container);
        m_cards.append(card);

        connect(card, &ProjectCard::clicked, this, [this, card](const ProjectInfo& info) {
            selectCard(card);
            emit selectionChanged(info);
        });
        connect(card, &ProjectCard::doubleClicked, this, [this](const ProjectInfo& info) {
            emit projectOpened(info);
        });
        connect(card, &ProjectCard::removeRequested, this, [this](const ProjectInfo& info) {
            emit removeRequested(info);
        });
        connect(card, &ProjectCard::deleteRequested, this, [this](const ProjectInfo& info) {
            emit deleteRequested(info);
        });

        grid->addWidget(card, i / COL_COUNT, i % COL_COUNT);
    }

    if (!m_cards.isEmpty()) {
        selectCard(m_cards.first());
        emit selectionChanged(m_cards.first()->projectInfo());
    }
}

void ProjectGrid::selectCard(ProjectCard* card) {
    if (m_selected) m_selected->setSelected(false);
    m_selected = card;
    if (m_selected) m_selected->setSelected(true);
}

ProjectInfo ProjectGrid::selectedProject() const {
    return m_selected ? m_selected->projectInfo() : ProjectInfo{};
}

bool ProjectGrid::hasSelection() const {
    return m_selected != nullptr;
}
