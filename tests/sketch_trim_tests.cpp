#include "operations/SketchTrimService.h"
#include "application/ModelingController.h"

#include <cassert>
#include <cmath>

using namespace cad::parametric;
using cad::operations::SketchTrimService;

namespace {
bool near(const double a, const double b) { return std::abs(a - b) < 1.0e-6; }

void lineTrim()
{
    SketchFeature sketch("s", SketchSupportType::XY, 100.0, 100.0, {
        SketchLine{{0, 0}, {10, 0}}, SketchLine{{3, -5}, {3, 5}},
        SketchLine{{7, -5}, {7, 5}}});
    const auto result = SketchTrimService::trim(sketch, {5, 0});
    assert(result.changed && result.replacements.size() == 2);
    assert(std::get<SketchLine>(result.replacements[0]).end.X() == 3.0);
    assert(std::get<SketchLine>(result.replacements[1]).start.X() == 7.0);
}

void arcTrim()
{
    SketchFeature sketch("s", SketchSupportType::XY, 100.0, 100.0, {
        SketchArc{{0, 0}, 10.0, 0.0, 3.141592653589793, false},
        SketchLine{{-5, -20}, {-5, 20}}, SketchLine{{5, -20}, {5, 20}}});
    const auto result = SketchTrimService::trim(sketch, {0, 10});
    assert(result.changed && result.replacements.size() == 2);
    assert(std::holds_alternative<SketchArc>(result.replacements.front()));
}

void circleTrim()
{
    SketchFeature sketch("s", SketchSupportType::XY, 100.0, 100.0, {
        SketchCircle{{0, 0}, 10.0}, SketchLine{{0, -20}, {0, 20}}});
    const auto result = SketchTrimService::trim(sketch, {10, 0});
    assert(result.changed && result.replacements.size() == 1);
    const auto& arc = std::get<SketchArc>(result.replacements.front());
    assert(near(std::abs(arc.signedSweep()), 3.141592653589793));

    SketchFeature four("four", SketchSupportType::XY, 100.0, 100.0, {
        SketchCircle{{0, 0}, 10.0}, SketchLine{{-20, 0}, {20, 0}},
        SketchLine{{0, -20}, {0, 20}}});
    const auto fourResult = SketchTrimService::trim(four, {7, 7});
    assert(fourResult.changed && fourResult.replacements.size() == 3);
    double totalSweep = 0.0;
    for (const auto& entity : fourResult.replacements) totalSweep += std::abs(std::get<SketchArc>(entity).signedSweep());
    assert(near(totalSweep, 1.5 * 3.141592653589793));
}

void intersectionMatrix()
{
    const SketchEntity line = SketchLine{{-10, 0}, {10, 0}};
    const SketchEntity crossing = SketchLine{{0, -10}, {0, 10}};
    const SketchEntity tangent = SketchLine{{-10, 10}, {10, 10}};
    const SketchEntity circle = SketchCircle{{0, 0}, 5.0};
    const SketchEntity arc = SketchArc{{0, 0}, 5.0, 0.0, 3.141592653589793, false};
    assert(SketchTrimService::intersections(line, crossing).size() == 1);
    assert(SketchTrimService::intersections(line, tangent).empty());
    assert(SketchTrimService::intersections(line, circle).size() == 2);
    assert(SketchTrimService::intersections(line, arc).size() == 2);
    assert(SketchTrimService::intersections(circle, circle).empty());
    const SketchEntity otherCircle = SketchCircle{{8, 0}, 5.0};
    assert(SketchTrimService::intersections(circle, otherCircle).size() == 2);
    const SketchEntity otherArc = SketchArc{{0, 0}, 5.0, 3.5, 5.0, false};
    assert(SketchTrimService::intersections(arc, otherArc).empty());
}

void undoRedo()
{
    cad::application::ModelingController controller;
    const auto created = controller.createSketch();
    assert(created.success);
    assert(controller.addSketchLine(created.id, {0, 0}, {10, 0}).success);
    assert(controller.addSketchLine(created.id, {3, -5}, {3, 5}).success);
    assert(controller.addSketchLine(created.id, {7, -5}, {7, 5}).success);
    assert(controller.trimSketchEntity(created.id, {5, 0}).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(controller.body().findFeature(created.id));
    assert(sketch && sketch->entityCount() == 4);
    controller.undo();
    assert(sketch->entityCount() == 3);
    controller.redo();
    assert(sketch->entityCount() == 4);
}
}

int main()
{
    lineTrim();
    arcTrim();
    circleTrim();
    intersectionMatrix();
    undoRedo();
    return 0;
}
