#pragma once

#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Aspect_DisplayConnection.hxx>

class CadViewer final : public QWidget
{
    Q_OBJECT

public:
    explicit CadViewer(QWidget* parent = nullptr);

    void display(const TopoDS_Shape& shape);
    void clear();
    void fitAll();

protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void initializeOcc();
    void bindWindow();

    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;

    QPoint lastMousePosition_;
    bool initialized_{false};

    Handle(Aspect_DisplayConnection) displayConnection_;

};
