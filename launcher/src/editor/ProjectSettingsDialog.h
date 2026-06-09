#pragma once
#include "models/ProjectInfo.h"
#include <QDialog>

class QLineEdit;
class QComboBox;

class ProjectSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectSettingsDialog(const ProjectInfo& project, QWidget* parent = nullptr);

    static QString readDefaultLevel(const QString& projectPath);

private:
    void saveSettings();

    ProjectInfo  m_project;
    QLineEdit*   m_nameEdit          = nullptr;
    QComboBox*   m_defaultLevelCombo = nullptr;
};
