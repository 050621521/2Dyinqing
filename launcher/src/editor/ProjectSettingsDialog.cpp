#include "ProjectSettingsDialog.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

static QString projectJsonPath(const QString& projectPath) {
    return projectPath + "/project.json";
}

QString ProjectSettingsDialog::readDefaultLevel(const QString& projectPath) {
    QFile f(projectJsonPath(projectPath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return obj.value("defaultLevel").toString();
}

float ProjectSettingsDialog::readPixelsPerUnit(const QString& projectPath) {
    QFile f(projectJsonPath(projectPath));
    if (!f.open(QIODevice::ReadOnly)) return 100.0f;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return (float)obj.value("pixelsPerUnit").toDouble(100.0);
}

ProjectSettingsDialog::ProjectSettingsDialog(const ProjectInfo& project, QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setWindowTitle("项目设置");
    setModal(true);
    setFixedSize(520, 300);
    setObjectName("projectSettingsDialog");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(12);

    auto* grid = new QGridLayout;
    grid->setSpacing(10);
    grid->setColumnMinimumWidth(0, 72);
    grid->setColumnStretch(1, 1);

    auto makeLabel = [&](const QString& text) {
        auto* lbl = new QLabel(text, this);
        lbl->setObjectName("psLabel");
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return lbl;
    };

    // 项目名称
    m_nameEdit = new QLineEdit(m_project.name, this);
    m_nameEdit->setObjectName("psInput");
    grid->addWidget(makeLabel("项目名称"), 0, 0);
    grid->addWidget(m_nameEdit, 0, 1);

    // 项目路径（只读）
    auto* pathEdit = new QLineEdit(m_project.path, this);
    pathEdit->setObjectName("psInput");
    pathEdit->setReadOnly(true);
    grid->addWidget(makeLabel("项目路径"), 1, 0);
    grid->addWidget(pathEdit, 1, 1);

    // 引擎版本（只读标签）
    auto* verLbl = new QLabel(
        m_project.engineVersion.isEmpty() ? "0.1" : m_project.engineVersion, this);
    verLbl->setObjectName("psLabel");
    grid->addWidget(makeLabel("引擎版本"), 2, 0);
    grid->addWidget(verLbl, 2, 1);

    // 默认关卡
    m_defaultLevelCombo = new QComboBox(this);
    m_defaultLevelCombo->setObjectName("psCombo");
    m_defaultLevelCombo->addItem("（无）", QString());

    const QString levelsDir = m_project.path + "/Levels";
    const QStringList files = QDir(levelsDir).entryList({"*.level"}, QDir::Files, QDir::Name);
    for (const QString& f : files)
        m_defaultLevelCombo->addItem(f, levelsDir + "/" + f);

    // 选中当前已保存的默认关卡
    const QString saved = readDefaultLevel(m_project.path);
    if (!saved.isEmpty()) {
        for (int i = 1; i < m_defaultLevelCombo->count(); ++i) {
            if (m_defaultLevelCombo->itemData(i).toString() == saved) {
                m_defaultLevelCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    grid->addWidget(makeLabel("默认关卡"), 3, 0);
    grid->addWidget(m_defaultLevelCombo, 3, 1);

    // 像素/单位
    m_ppuSpinBox = new QSpinBox(this);
    m_ppuSpinBox->setObjectName("psInput");
    m_ppuSpinBox->setRange(1, 10000);
    m_ppuSpinBox->setSuffix(" px/unit");
    m_ppuSpinBox->setValue((int)readPixelsPerUnit(m_project.path));
    grid->addWidget(makeLabel("像素/单位"), 4, 0);
    grid->addWidget(m_ppuSpinBox, 4, 1);

    root->addLayout(grid);
    root->addStretch();

    // 按钮行
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("取消", this);
    auto* okBtn     = new QPushButton("确定", this);
    cancelBtn->setObjectName("psBtn");
    okBtn->setObjectName("psBtnPrimary");
    okBtn->setDefault(true);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    root->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        saveSettings();
        accept();
    });
}

void ProjectSettingsDialog::saveSettings() {
    QFile rf(projectJsonPath(m_project.path));
    QJsonObject obj;
    if (rf.open(QIODevice::ReadOnly))
        obj = QJsonDocument::fromJson(rf.readAll()).object();

    obj["defaultLevel"] = m_defaultLevelCombo->currentData().toString();
    obj["pixelsPerUnit"] = m_ppuSpinBox->value();

    QFile wf(projectJsonPath(m_project.path));
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        wf.write(QJsonDocument(obj).toJson());
}
