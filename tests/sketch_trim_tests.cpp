#include "operations/SketchTrimService.h"
#include "application/ModelingController.h"
#include "model/ProjectFile.h"

#include <cassert>
#include <cmath>
#include <QTemporaryDir>

using namespace cad::parametric;
using cad::operations::SketchTrimService;

namespace {
bool near(const double a, const double b) { return std::abs(a - b) < 1.0e-6; }

void lineTrim()
{
    SketchFeature sketch("s", SketchSupportType::XY, 100.0, 100.0, {
        SketchLine{{0, 0}, {10, 0}}, SketchLine{{3, -5}, {3, 5}},
        SketchLine{{7, -5}, {7, 5}}});
    const auto result = SketchTrimService::trim(sketch, {5, 0}, 0.25);
    const auto preview = SketchTrimService::previewTrim(sketch, {5, 0}, 0.25);
    assert(preview && preview->removedEntities.size() == 1);
    assert(result.changed && result.replacements.size() == 2);
    assert(std::get<SketchLine>(result.replacements[0]).end.X() == 3.0);
    assert(std::get<SketchLine>(result.replacements[1]).start.X() == 7.0);
}

void editSketchValidationAndReopen()
{
    cad::application::ModelingController controller;
    const auto sketch = controller.createSketch();
    assert(sketch.success);
    const cad::application::SelectionSnapshot objectSelection{{
        {sketch.id, cad::application::SelectionKind::Object, std::nullopt}}};
    assert(controller.canEditSketch(objectSelection));
    assert(controller.actionState(objectSelection).canEditSketch);
    assert(controller.addSketchCircle(sketch.id, {0, 0}, 5.0).success);
    // Re-entering uses the same SketchFeature; new geometry is appended to it.
    assert(controller.addSketchLine(sketch.id, {-5, 0}, {5, 0}).success);
    const auto sketchFeature = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketch.id));
    assert(sketchFeature && sketchFeature->entityCount() == 2);

    const auto box = controller.createBox();
    assert(box.success);
    const cad::application::SelectionSnapshot boxSelection{{
        {box.id, cad::application::SelectionKind::Object, std::nullopt}}};
    assert(!controller.canEditSketch(boxSelection));
    const cad::application::SelectionSnapshot faceSelection{{
        {box.id, cad::application::SelectionKind::Face, 1}}};
    assert(!controller.canEditSketch(faceSelection));
    assert(!controller.canEditSketch({{
        {sketch.id, cad::application::SelectionKind::Object, std::nullopt},
        {box.id, cad::application::SelectionKind::Object, std::nullopt}}}));
}

void faceAttachedSketchCanReopenAfterUpstreamEdit()
{
    cad::application::ModelingController controller;
    const auto box = controller.createBox();
    assert(box.success);
    const cad::application::SelectionSnapshot faceSelection{{
        {box.id, cad::application::SelectionKind::Face, 1}}};
    const auto sketch = controller.createSketchOnFace(faceSelection);
    assert(sketch.success);
    const auto sketchFeature = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketch.id));
    assert(sketchFeature);
    const auto firstFrame = sketchFeature->currentFrame();
    assert(controller.setFeatureProperty(box.id, "width", 80.0).success);
    assert(controller.body().recompute());
    const auto secondFrame = sketchFeature->currentFrame();
    assert(secondFrame.normal.IsParallel(firstFrame.normal, 1.0e-6));
    const cad::application::SelectionSnapshot sketchSelection{{
        {sketch.id, cad::application::SelectionKind::Object, std::nullopt}}};
    assert(controller.canEditSketch(sketchSelection));
    assert(controller.addSketchCircle(sketch.id, {0, 0}, 5.0).success);
    assert(sketchFeature->entityCount() == 1);
}

void arcTrim()
{
    SketchFeature sketch("s", SketchSupportType::XY, 100.0, 100.0, {
        SketchArc{{0, 0}, 10.0, 0.0, 3.141592653589793, false},
        SketchLine{{-5, -20}, {-5, 20}}, SketchLine{{5, -20}, {5, 20}}});
    const auto result = SketchTrimService::trim(sketch, {0, 10}, 0.25);
    assert(result.changed && result.replacements.size() == 2);
    assert(std::holds_alternative<SketchArc>(result.replacements.front()));
}

void circleTrim()
{
    SketchFeature sketch("s", SketchSupportType::XY, 100.0, 100.0, {
        SketchCircle{{0, 0}, 10.0}, SketchLine{{0, -20}, {0, 20}}});
    const auto result = SketchTrimService::trim(sketch, {10, 0}, 0.25);
    assert(result.changed && result.replacements.size() == 1);
    const auto& arc = std::get<SketchArc>(result.replacements.front());
    assert(near(std::abs(arc.signedSweep()), 3.141592653589793));

    SketchFeature four("four", SketchSupportType::XY, 100.0, 100.0, {
        SketchCircle{{0, 0}, 10.0}, SketchLine{{-20, 0}, {20, 0}},
        SketchLine{{0, -20}, {0, 20}}});
    const auto fourResult = SketchTrimService::trim(four, {7, 7}, 0.25);
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
    assert(controller.trimSketchEntity(created.id, {5, 0}, 0.25).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(controller.body().findFeature(created.id));
    assert(sketch && sketch->entityCount() == 4);
    controller.undo();
    assert(sketch->entityCount() == 3);
    controller.redo();
    assert(sketch->entityCount() == 4);
}

void downstreamAndPersistence()
{
    cad::application::ModelingController controller;
    const auto sketchResult = controller.createSketch();
    assert(sketchResult.success);
    const auto add = [&controller, &sketchResult](gp_Pnt2d a, gp_Pnt2d b) {
        return controller.addSketchLine(sketchResult.id, a, b).success;
    };
    assert(add({-40, -30}, {40, -30}));
    assert(add({40, -30}, {40, 30}));
    assert(add({40, 30}, {-40, 30}));
    assert(add({-40, 30}, {-40, -30}));
    const auto extrude = controller.createExtrudeFromSketch({{
        {sketchResult.id, cad::application::SelectionKind::Object, std::nullopt}}}, 20.0);
    assert(extrude.success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketchResult.id));
    auto feature = controller.body().findFeature(extrude.id);
    assert(sketch && feature && feature->state() == FeatureState::UpToDate);

    assert(controller.trimSketchEntity(sketchResult.id, {0, -30}, 0.25).success);
    assert(!controller.body().recompute());
    assert(feature->state() == FeatureState::Failed);
    assert(feature->error().find("open") != std::string::npos);

    controller.undo();
    assert(controller.body().recompute());
    assert(feature->state() == FeatureState::UpToDate && !feature->shape().IsNull());
    controller.redo();
    assert(!controller.body().recompute());
    assert(feature->state() == FeatureState::Failed);

    cad::application::ModelingController persistence;
    const auto persisted = persistence.createSketch();
    assert(persisted.success);
    const auto addPersisted = [&persistence, &persisted](gp_Pnt2d a, gp_Pnt2d b) {
        return persistence.addSketchLine(persisted.id, a, b).success;
    };
    assert(addPersisted({-40, -30}, {40, -30}));
    assert(addPersisted({40, -30}, {40, 30}));
    assert(addPersisted({40, 30}, {-40, 30}));
    assert(addPersisted({-40, 30}, {-40, -30}));
    assert(persistence.trimSketchEntity(persisted.id, {0, -30}, 0.25).success);

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("trimmed.pcad");
    assert(ProjectFile::save(path, persistence.document(), persistence.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
        loadedBody.findFeature(persisted.id));
    assert(loadedSketch && loadedSketch->entityCount() == 3);
    cad::application::ModelingController reloadedController;
    reloadedController.replaceProject(std::move(loadedDocument), std::move(loadedBody));
    const cad::application::SelectionSnapshot reloadedSelection{{
        {persisted.id, cad::application::SelectionKind::Object, std::nullopt}}};
    assert(reloadedController.canEditSketch(reloadedSelection));
    assert(reloadedController.addSketchLine(persisted.id, {-20, 0}, {20, 0}).success);
}
}

int main()
{
    lineTrim();
    editSketchValidationAndReopen();
    faceAttachedSketchCanReopenAfterUpstreamEdit();
    arcTrim();
    circleTrim();
    intersectionMatrix();
    undoRedo();
    downstreamAndPersistence();
    return 0;
}
