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
    void closeEvent(QCloseEvent* event) override;
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool confirmReplacement();
    bool saveDocumentAs();
    bool saveTo(const QString& path);
    void updateTitle();
    void restoreViewer();
    QString currentFile_;
    void createActions();
    void createParametricPanel();
    void refreshParametricModel();
    void selectParametricFeatures(const QStringList& featureIds);
    void createBox();
    void createCylinder();
    void clearDocument();

    Document document_;
    cad::parametric::Body parametricBody_;
    CadViewer* viewer_{nullptr};
    FeatureEditorPanel* featureEditorPanel_{nullptr};
};
