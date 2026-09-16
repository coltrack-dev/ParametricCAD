#include "model/Body.h"
#include "operations/BasicFeatures.h"
#include "operations/ParametricFeatures.h"

#include <QtTest/QtTest>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace cad::parametric;
using cad::modeling::BasicFeatures;

namespace {
int faceCount(const TopoDS_Shape& shape)
{
    int count = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) ++count;
    return count;
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}
}

class BooleanCutTests final : public QObject
{
    Q_OBJECT
private slots:
    void hexagonMinusCylinder()
    {
        Body body;
        auto base = std::make_shared<HexagonFeature>("hexagon", 30, 12);
        auto tool = std::make_shared<CylinderParametricFeature>("cylinder", 5, 20);
        auto cut = std::make_shared<BooleanFeature>("cut", base, tool, BooleanOperation::Cut);
        cut->setName("Cut001");
        body.addFeature(base);
        body.addFeature(tool);
        body.addFeature(cut);
        QVERIFY2(body.recompute(), body.lastError().c_str());
        QCOMPARE(cut->left().get(), base.get());
        QCOMPARE(cut->right().get(), tool.get());
        QCOMPARE(faceCount(base->shape()), 8);
        QCOMPARE(faceCount(tool->shape()), 3);
        QVERIFY(!cut->shape().IsNull());
        QVERIFY(BRepCheck_Analyzer(cut->shape()).IsValid());
        QCOMPARE(faceCount(cut->shape()), 9);
        const double expected = (std::sqrt(3.0) * 30 * 30 / 2 - std::numbers::pi * 25) * 12;
        QVERIFY(std::abs(volume(cut->shape()) - expected) < 1e-6);
        QVERIFY(volume(cut->shape()) < volume(base->shape()));

        int cylindricalWalls = 0;
        for (TopExp_Explorer it(cut->shape(), TopAbs_FACE); it.More(); it.Next()) {
            const auto face = TopoDS::Face(it.Current());
            BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Cylinder) continue;
            ++cylindricalWalls;
            QVERIFY(std::abs(surface.Cylinder().Radius() - 5) < 1e-8);
            gp_Pnt point;
            gp_Vec du, dv;
            surface.D1((surface.FirstUParameter() + surface.LastUParameter()) / 2,
                       (surface.FirstVParameter() + surface.LastVParameter()) / 2,
                       point, du, dv);
            gp_Vec normal = du.Crossed(dv);
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
            QVERIFY(normal.Dot(gp_Vec(point.X(), point.Y(), 0)) < 0);
        }
        QCOMPARE(cylindricalWalls, 1);
        TopExp_Explorer solids(cut->shape(), TopAbs_SOLID);
        QVERIFY(solids.More());
        const auto solid = TopoDS::Solid(solids.Current());
        BRepClass3d_SolidClassifier hole(solid, gp_Pnt(0, 0, 6), 1e-7);
        QCOMPARE(hole.State(), TopAbs_OUT);
        BRepClass3d_SolidClassifier material(solid, gp_Pnt(10, 0, 6), 1e-7);
        QCOMPARE(material.State(), TopAbs_IN);

        tool->setRadius(6);
        body.markDirtyFrom(tool->id());
        QVERIFY2(body.recompute(), body.lastError().c_str());
        const double edited = (std::sqrt(3.0) * 30 * 30 / 2 - std::numbers::pi * 36) * 12;
        QVERIFY(std::abs(volume(cut->shape()) - edited) < 1e-6);
    }

    void disjointAndTouchingTools()
    {
        HexagonFeature base("hexagon", 30, 12);
        QVERIFY(base.recompute());
        for (const double offset : {50.0, 20.0}) {
            gp_Trsf transform;
            transform.SetTranslation(gp_Vec(offset, 0, 0));
            const auto tool = BRepBuilderAPI_Transform(BasicFeatures::cylinder(5, 20), transform, true).Shape();
            try {
                BasicFeatures::cut(base.shape(), tool);
                QFAIL("Cut must reject a cutter that removes no volume");
            } catch (const std::runtime_error& error) {
                QVERIFY(QString::fromUtf8(error.what()).contains("does not intersect"));
            }
        }
    }

    void oversizedCutter()
    {
        auto base = std::make_shared<HexagonFeature>("hexagon", 30, 12);
        auto tool = std::make_shared<CylinderParametricFeature>("cylinder", 25, 60);
        QVERIFY(base->recompute());
        QVERIFY(tool->recompute());
        BooleanFeature cut("cut", base, tool, BooleanOperation::Cut);
        QVERIFY(!cut.recompute());
        QCOMPARE(cut.state(), FeatureState::Failed);
        QVERIFY(cut.shape().IsNull());
        QVERIFY(QString::fromStdString(cut.error()).contains("entire base"));
    }
};

QTEST_APPLESS_MAIN(BooleanCutTests)
#include "boolean_cut_tests.moc"
