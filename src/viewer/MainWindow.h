#pragma once

#include <QMainWindow>

#include <string>
#include "model/Document.h"
#include "model/Body.h"

class CadViewer;
class FeatureEditorPanel;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createActions();
    void createParametricPanel();
    void refreshParametricModel();
    void displayParametricFeature(const std::string& featureId);
    void createBox();
    void createCylinder();
    void clearDocument();

    Document document_;
    cad::parametric::Body parametricBody_;
    CadViewer* viewer_{nullptr};
    FeatureEditorPanel* featureEditorPanel_{nullptr};
};
