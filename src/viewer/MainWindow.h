#pragma once

#include <QMainWindow>
#include "model/Document.h"

class CadViewer;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createActions();
    void createBox();
    void createCylinder();
    void clearDocument();

    Document document_;
    CadViewer* viewer_{nullptr};
};
