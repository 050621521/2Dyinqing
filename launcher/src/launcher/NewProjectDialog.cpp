#include "NewProjectDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QToolButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QLabel>

NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("新建项目");
    setFixedSize(480, 260);
    setObjectName("newProjectDialog");

    auto* root = new QVBoxLayout(this);
    root->setSpacing(16);

    auto* title = new QLabel("新建 2D 游戏项目", this);
    title->setObjectName("dialogTitle");
    root->addWidget(title);

    auto* form = new QFormLayout();
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight);

    m_nameEdit = new QLineEdit("MyGame", this);
    m_nameEdit->setObjectName("formEdit");
    form->addRow("项目名称：", m_nameEdit);

    auto* pathRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    m_pathEdit->setObjectName("formEdit");
    auto* browseBtn = new QToolButton(this);
    browseBtn->setText("浏览…");
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browseBtn);
    form->addRow("项目位置：", pathRow);

    m_templateCombo = new QComboBox(this);
    m_templateCombo->addItems({"空白项目", "平台游戏", "RPG", "射击游戏", "益智游戏"});
    m_templateCombo->setObjectName("formCombo");
    form->addRow("模板：", m_templateCombo);

    root->addLayout(form);
    root->addStretch();

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText("创建");
    btns->button(QDialogButtonBox::Cancel)->setText("取消");
    root->addWidget(btns);

    connect(browseBtn, &QToolButton::clicked, this, &NewProjectDialog::browsePath);
    connect(btns, &QDialogButtonBox::accepted, this, &NewProjectDialog::validate);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void NewProjectDialog::browsePath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择项目位置", m_pathEdit->text());
    if (!dir.isEmpty()) m_pathEdit->setText(dir);
}

void NewProjectDialog::validate() {
    if (!m_nameEdit->text().trimmed().isEmpty())
        accept();
}

ProjectInfo NewProjectDialog::result() const {
    ProjectInfo info;
    info.name        = m_nameEdit->text().trimmed();
    info.path        = m_pathEdit->text() + QDir::separator() + info.name;
    info.lastOpened  = QDateTime::currentDateTime();
    info.engineVersion = "0.1";
    return info;
}
