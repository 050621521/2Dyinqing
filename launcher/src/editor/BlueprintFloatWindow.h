#pragma once
#include <QMainWindow>

class BlueprintFloatWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BlueprintFloatWindow(QWidget* parent = nullptr);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* e) override;
};
