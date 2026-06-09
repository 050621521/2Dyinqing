#pragma once
#include "models/ProjectInfo.h"
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    ProjectInfo result() const;

private:
    QLineEdit* m_nameEdit;
    QLineEdit* m_pathEdit;
    QComboBox* m_templateCombo;

    void browsePath();
    void validate();
};
