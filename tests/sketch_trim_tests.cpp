#include "operations/SketchTrimService.h"
#include "operations/SketchExtendService.h"
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

void extendLineAndArc()
{
    using cad::operations::ExtendEndpoint;
    using cad::operations::SketchExtendService;
    SketchFeature lineSketch("line", SketchSupportType::XY, 100.0, 100.0, {
        SketchLine{{0, 0}, {5, 0}}, SketchLine{{10, -5}, {10, 5}},
        SketchLine{{7, -5}, {7, 5}}});
    const auto endPlan = SketchExtendService::analyzeExtend(lineSketch, {5, 0}, 0.5);
    assert(endPlan.changed && endPlan.endpoint == ExtendEndpoint::End);
    assert(std::get<SketchLine>(endPlan.extendedEntity).end.X() == 7.0);
    assert(std::get<SketchLine>(endPlan.extensionSpan.front()).end.X() == 7.0);

    SketchFeature startSketch("start", SketchSupportType::XY, 100.0, 100.0, {
        SketchLine{{0, 0}, {5, 0}}, SketchLine{{-10, -5}, {-10, 5}}});
    const auto startPlan = SketchExtendService::analyzeExtend(startSketch, {0, 0}, 0.5);
    assert(startPlan.changed && startPlan.endpoint == ExtendEndpoint::Start);
    assert(std::get<SketchLine>(startPlan.extendedEntity).start.X() == -10.0);

    SketchFeature wrongDirection("wrong", SketchSupportType::XY, 100.0, 100.0, {
        SketchLine{{0, 0}, {5, 0}}, SketchLine{{-10, -5}, {-10, 5}}});
    assert(!SketchExtendService::analyzeExtend(wrongDirection, {5, 0}, 0.5).changed);

    SketchFeature arcSketch("arc", SketchSupportType::XY, 100.0, 100.0, {
        SketchArc{{0, 0}, 10.0, 0.0, std::acos(-1.0) / 2.0, false},
        SketchLine{{-20, 0}, {20, 0}}});
    const auto arcEnd = SketchExtendService::analyzeExtend(arcSketch, {0, 10}, 0.5);
    assert(arcEnd.changed && arcEnd.endpoint == ExtendEndpoint::End);
    assert(std::abs(std::get<SketchArc>(arcEnd.extendedEntity).endAngle - std::acos(-1.0)) < 1.0e-6);
    const auto arcStart = SketchExtendService::analyzeExtend(arcSketch, {10, 0}, 0.5);
    assert(arcStart.changed && arcStart.endpoint == ExtendEndpoint::Start);

    SketchFeature clockwise("clockwise", SketchSupportType::XY, 100.0, 100.0, {
        SketchArc{{0, 0}, 10.0, 0.0, -std::acos(-1.0) / 2.0, true},
        SketchLine{{-20, 0}, {20, 0}}});
    const auto clockwiseEnd = SketchExtendService::analyzeExtend(clockwise, {0, -10}, 0.5);
    assert(clockwiseEnd.changed);
    assert(std::get<SketchArc>(clockwiseEnd.extendedEntity).endAngle < -std::acos(-1.0) / 2.0);

    SketchFeature circle("circle", SketchSupportType::XY, 100.0, 100.0, {
        SketchCircle{{0, 0}, 10.0}});
    assert(!SketchExtendService::analyzeExtend(circle, {10, 0}, 0.5).changed);
}

void extendClosesExtrudeProfile()
{
    cad::application::ModelingController controller;
    const auto sketch = controller.createSketch();
    assert(sketch.success);
    const auto add = [&controller, &sketch](gp_Pnt2d a, gp_Pnt2d b) {
        return controller.addSketchLine(sketch.id, a, b).success;
    };
    assert(add({-10, 0}, {10, 0}));
    assert(add({10, 0}, {10, 10}));
    assert(add({10, 10}, {-10, 10}));
    assert(add({-10, 10}, {-10, 0}));
    const auto extrude = controller.createExtrudeFromSketch({{
        {sketch.id, cad::application::SelectionKind::Object, std::nullopt}}}, 10.0);
    assert(extrude.success);
    auto sketchFeature = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketch.id));
    assert(sketchFeature);
    sketchFeature->replaceEntities(3, 1, {SketchLine{{-10, 10}, {-10, 2}}});
    controller.body().markDirtyFrom(sketch.id);
    assert(!controller.body().recompute());
    assert(controller.extendSketchEntity(sketch.id, {-10, 2}, 0.5).success);
    assert(controller.body().recompute());
    auto feature = controller.body().findFeature(extrude.id);
    assert(feature && !feature->shape().IsNull());
    controller.undo();
    assert(!controller.body().recompute());
    controller.redo();
    assert(controller.body().recompute());

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("extended.pcad");
    assert(ProjectFile::save(path, controller.document(), controller.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
        loadedBody.findFeature(sketch.id));
    assert(loadedSketch && loadedSketch->entityCount() == 4);
    const auto& extendedLine = std::get<SketchLine>(loadedSketch->entities()[3]);
    assert(std::abs(extendedLine.end.Y()) < 1.0e-6);
}

void constraintsSolveUndoAndPersist()
{
    cad::application::ModelingController controller;
    const auto sketchResult = controller.createSketch();
    assert(sketchResult.success);
    assert(controller.addSketchLine(sketchResult.id, {0, 0}, {10, 3}).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketchResult.id));
    assert(sketch);
    const auto lineId = std::get<SketchLine>(sketch->entities().front()).id;
    assert(controller.addSketchHorizontal(sketchResult.id, lineId).success);
    assert(std::abs(std::get<SketchLine>(sketch->entities().front()).end.Y()) < 1.0e-9);
    assert(sketch->constraintCount() == 1);
    controller.undo();
    assert(std::abs(std::get<SketchLine>(sketch->entities().front()).end.Y() - 3.0) < 1.0e-9);
    controller.redo();
    assert(std::abs(std::get<SketchLine>(sketch->entities().front()).end.Y()) < 1.0e-9);

    assert(controller.addSketchLine(sketchResult.id, {10.4, 0.2}, {20, 5}).success);
    const auto secondId = std::get<SketchLine>(sketch->entities().back()).id;
    const auto firstEnd = cad::parametric::SketchPointRef{lineId,
        cad::parametric::SketchPointRole::LineEnd};
    const auto secondStart = cad::parametric::SketchPointRef{secondId,
        cad::parametric::SketchPointRole::LineStart};
    assert(controller.addSketchCoincident(sketchResult.id, firstEnd, secondStart).success);
    const auto& first = std::get<SketchLine>(sketch->entities().front());
    const auto& second = std::get<SketchLine>(sketch->entities().back());
    assert(first.end.Distance(second.start) < 1.0e-9);

    const auto beforeConflict = sketch->constraintCount();
    assert(!controller.addSketchVertical(sketchResult.id, lineId).success);
    assert(sketch->constraintCount() == beforeConflict);

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("constraints.pcad");
    assert(ProjectFile::save(path, controller.document(), controller.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
        loadedBody.findFeature(sketchResult.id));
    assert(loadedSketch && loadedSketch->constraintCount() == 2);
    assert(std::abs(std::get<SketchLine>(loadedSketch->entities().front()).end.Y()) < 1.0e-9);
}

void dimensionalConstraintsSolveUndoAndPersist()
{
    cad::application::ModelingController controller;
    const auto sketchResult = controller.createSketch();
    assert(sketchResult.success);
    assert(controller.addSketchLine(sketchResult.id, {0, 0}, {10, 3}).success);
    assert(controller.addSketchCircle(sketchResult.id, {30, 0}, 5.0).success);
    assert(controller.addSketchArc(sketchResult.id, {50, 0}, {55, 0}, {50, 5}).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketchResult.id));
    assert(sketch && sketch->entityCount() == 3);
    const auto lineId = std::get<SketchLine>(sketch->entities()[0]).id;
    const auto circleId = std::get<SketchCircle>(sketch->entities()[1]).id;
    const auto arcId = std::get<SketchArc>(sketch->entities()[2]).id;
    assert(controller.addSketchHorizontal(sketchResult.id, lineId).success);
    assert(controller.addSketchDistance(sketchResult.id, lineId, 20.0).success);
    assert(std::abs(std::get<SketchLine>(sketch->entities()[0]).start.Distance(
        std::get<SketchLine>(sketch->entities()[0]).end) - 20.0) < 1.0e-8);
    const auto distanceId = std::get<cad::parametric::DistanceConstraint>(
        sketch->constraints().back()).id;
    assert(controller.updateSketchDistance(sketchResult.id, distanceId, 30.0).success);
    assert(std::abs(std::get<SketchLine>(sketch->entities()[0]).start.Distance(
        std::get<SketchLine>(sketch->entities()[0]).end) - 30.0) < 1.0e-8);
    controller.undo();
    assert(std::abs(std::get<SketchLine>(sketch->entities()[0]).start.Distance(
        std::get<SketchLine>(sketch->entities()[0]).end) - 20.0) < 1.0e-8);
    controller.redo();
    assert(std::abs(std::get<SketchLine>(sketch->entities()[0]).start.Distance(
        std::get<SketchLine>(sketch->entities()[0]).end) - 30.0) < 1.0e-8);
    const auto beforeRemoveCount = sketch->constraintCount();
    assert(controller.removeSketchConstraint(sketchResult.id, distanceId).success);
    assert(sketch->constraintCount() == beforeRemoveCount - 1);
    assert(std::abs(std::get<SketchLine>(sketch->entities()[0]).start.Distance(
        std::get<SketchLine>(sketch->entities()[0]).end) - 30.0) < 1.0e-8);
    controller.undo();
    assert(sketch->constraintCount() == beforeRemoveCount);
    controller.redo();
    assert(sketch->constraintCount() == beforeRemoveCount - 1);
    assert(controller.addSketchRadius(sketchResult.id, circleId, 10.0).success);
    assert(controller.addSketchRadius(sketchResult.id, arcId, 8.0).success);
    assert(std::abs(std::get<SketchCircle>(sketch->entities()[1]).radius - 10.0) < 1.0e-9);
    assert(std::abs(std::get<SketchArc>(sketch->entities()[2]).radius - 8.0) < 1.0e-9);
    const auto count = sketch->constraintCount();
    assert(!controller.addSketchRadius(sketchResult.id, circleId, -1.0).success);
    assert(sketch->constraintCount() == count);

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("dimensional-constraints.pcad");
    assert(ProjectFile::save(path, controller.document(), controller.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
        loadedBody.findFeature(sketchResult.id));
    assert(loadedSketch && loadedSketch->constraintCount() == count);
    assert(std::abs(std::get<SketchCircle>(loadedSketch->entities()[1]).radius - 10.0) < 1.0e-9);

    cad::application::ModelingController downstream;
    const auto downstreamSketch = downstream.createSketch();
    assert(downstreamSketch.success);
    assert(downstream.addSketchCircle(downstreamSketch.id, {0, 0}, 5.0).success);
    const auto extrude = downstream.createExtrudeFromSketch({{
        {downstreamSketch.id, cad::application::SelectionKind::Object, std::nullopt}}}, 10.0);
    assert(extrude.success);
    const auto downstreamFeature = downstream.body().findFeature(extrude.id);
    const auto circle = std::dynamic_pointer_cast<SketchFeature>(
        downstream.body().findFeature(downstreamSketch.id));
    assert(circle && downstreamFeature && downstreamFeature->state() == FeatureState::UpToDate);
    const auto downstreamCircleId = std::get<SketchCircle>(circle->entities().front()).id;
    assert(downstream.addSketchRadius(downstreamSketch.id, downstreamCircleId, 10.0).success);
    assert(downstream.body().recompute());
    assert(downstreamFeature->state() == FeatureState::UpToDate
        && !downstreamFeature->shape().IsNull());
    downstream.undo();
    assert(downstream.body().recompute());
    assert(downstreamFeature->state() == FeatureState::UpToDate);
    downstream.redo();
    assert(downstream.body().recompute());
    assert(downstreamFeature->state() == FeatureState::UpToDate);
}

void positionalAndAngleConstraints()
{
    cad::application::ModelingController controller;
    const auto sketchResult = controller.createSketch();
    assert(sketchResult.success);
    assert(controller.addSketchLine(sketchResult.id, {0, 0}, {10, 0}).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(
        controller.body().findFeature(sketchResult.id));
    assert(sketch);
    const auto lineId = std::get<SketchLine>(sketch->entities().front()).id;
    assert(controller.addSketchDistance(sketchResult.id, lineId, 50.0).success);
    assert(controller.addSketchAngle(sketchResult.id, lineId, 30.0 * 3.14159265358979323846 / 180.0).success);
    const auto& lineAtThirty = std::get<SketchLine>(sketch->entities().front());
    assert(std::abs(lineAtThirty.start.Distance(lineAtThirty.end) - 50.0) < 1.0e-7);
    assert(std::abs(std::atan2(lineAtThirty.end.Y() - lineAtThirty.start.Y(),
        lineAtThirty.end.X() - lineAtThirty.start.X()) - 30.0 * 3.14159265358979323846 / 180.0) < 1.0e-7);
    const auto angleId = std::get<cad::parametric::AngleConstraint>(sketch->constraints().back()).id;
    assert(controller.updateSketchAngle(sketchResult.id, angleId,
        60.0 * 3.14159265358979323846 / 180.0).success);
    assert(std::abs(std::get<SketchLine>(sketch->entities().front()).start.Distance(
        std::get<SketchLine>(sketch->entities().front()).end) - 50.0) < 1.0e-7);
    const auto distanceId = std::get<cad::parametric::DistanceConstraint>(sketch->constraints().front()).id;
    assert(controller.updateSketchDistance(sketchResult.id, distanceId, 80.0).success);
    assert(std::abs(std::get<SketchLine>(sketch->entities().front()).start.Distance(
        std::get<SketchLine>(sketch->entities().front()).end) - 80.0) < 1.0e-7);
    assert(std::abs(std::atan2(std::get<SketchLine>(sketch->entities().front()).end.Y(),
        std::get<SketchLine>(sketch->entities().front()).end.X()) - 60.0 * 3.14159265358979323846 / 180.0) < 1.0e-7);
    controller.undo();
    assert(std::abs(std::get<SketchLine>(sketch->entities().front()).start.Distance(
        std::get<SketchLine>(sketch->entities().front()).end) - 50.0) < 1.0e-7);
    controller.redo();

    cad::application::ModelingController positioning;
    const auto positioned = positioning.createSketch();
    assert(positioned.success);
    assert(positioning.addSketchLine(positioned.id, {0, 0}, {0, 0.1}).success);
    assert(positioning.addSketchCircle(positioned.id, {5, 5}, 10.0).success);
    auto positionedSketch = std::dynamic_pointer_cast<SketchFeature>(
        positioning.body().findFeature(positioned.id));
    const auto referenceId = std::get<SketchLine>(positionedSketch->entities()[0]).id;
    const auto circleId = std::get<SketchCircle>(positionedSketch->entities()[1]).id;
    const cad::parametric::SketchPointRef reference{referenceId, cad::parametric::SketchPointRole::LineStart};
    const cad::parametric::SketchPointRef center{circleId, cad::parametric::SketchPointRole::CircleCenter};
    assert(positioning.addSketchHorizontalDistance(positioned.id, reference, center, 30.0).success);
    assert(positioning.addSketchVerticalDistance(positioned.id, reference, center, 20.0).success);
    const auto& positionedCircle = std::get<SketchCircle>(positionedSketch->entities()[1]);
    assert(std::abs(positionedCircle.center.X() - 30.0) < 1.0e-7);
    assert(std::abs(positionedCircle.center.Y() - 20.0) < 1.0e-7);
    assert(!positioning.addSketchHorizontalDistance(positioned.id, reference, center, 40.0).success);

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("positional-constraints.pcad");
    assert(ProjectFile::save(path, positioning.document(), positioning.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loaded = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(positioned.id));
    assert(loaded && loaded->constraintCount() == 2);
}

void parallelAndPerpendicularConstraints()
{
    cad::application::ModelingController controller;
    const auto created = controller.createSketch();
    assert(created.success);
    assert(controller.addSketchLine(created.id, {0, 0}, {20, 0}).success);
    assert(controller.addSketchLine(created.id, {0, 10}, {8, 14}).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(controller.body().findFeature(created.id));
    const auto firstId = std::get<SketchLine>(sketch->entities()[0]).id;
    const auto secondId = std::get<SketchLine>(sketch->entities()[1]).id;
    const double secondLength = std::get<SketchLine>(sketch->entities()[1]).start.Distance(
        std::get<SketchLine>(sketch->entities()[1]).end);
    assert(controller.addSketchParallel(created.id, firstId, secondId).success);
    const auto& parallelLine = std::get<SketchLine>(sketch->entities()[1]);
    assert(std::abs(parallelLine.start.Distance(parallelLine.end) - secondLength) < 1.0e-7);
    assert(std::abs(parallelLine.end.Y() - parallelLine.start.Y()) < 1.0e-7);
    const auto parallelId = std::get<cad::parametric::ParallelConstraint>(sketch->constraints().back()).id;
    controller.undo();
    assert(sketch->constraintCount() == 0);
    controller.redo();
    assert(sketch->constraintCount() == 1);
    assert(controller.removeSketchConstraint(created.id, parallelId).success);

    cad::application::ModelingController perpendicular;
    const auto perpendicularSketch = perpendicular.createSketch();
    assert(perpendicularSketch.success);
    assert(perpendicular.addSketchLine(perpendicularSketch.id, {0, 0}, {20, 0}).success);
    assert(perpendicular.addSketchLine(perpendicularSketch.id, {0, 10}, {8, 14}).success);
    auto perpendicularFeature = std::dynamic_pointer_cast<SketchFeature>(
        perpendicular.body().findFeature(perpendicularSketch.id));
    const auto pFirst = std::get<SketchLine>(perpendicularFeature->entities()[0]).id;
    const auto pSecond = std::get<SketchLine>(perpendicularFeature->entities()[1]).id;
    const double pLength = std::get<SketchLine>(perpendicularFeature->entities()[1]).start.Distance(
        std::get<SketchLine>(perpendicularFeature->entities()[1]).end);
    assert(perpendicular.addSketchPerpendicular(perpendicularSketch.id, pFirst, pSecond).success);
    const auto& perpendicularLine = std::get<SketchLine>(perpendicularFeature->entities()[1]);
    assert(std::abs(perpendicularLine.start.Distance(perpendicularLine.end) - pLength) < 1.0e-7);
    assert(std::abs(perpendicularLine.end.X() - perpendicularLine.start.X()) < 1.0e-7);

    cad::application::ModelingController conflict;
    const auto conflictSketch = conflict.createSketch();
    assert(conflictSketch.success);
    assert(conflict.addSketchLine(conflictSketch.id, {0, 0}, {10, 0}).success);
    assert(conflict.addSketchLine(conflictSketch.id, {0, 1}, {1, 1}).success);
    auto conflictFeature = std::dynamic_pointer_cast<SketchFeature>(
        conflict.body().findFeature(conflictSketch.id));
    const auto cFirst = std::get<SketchLine>(conflictFeature->entities()[0]).id;
    const auto cSecond = std::get<SketchLine>(conflictFeature->entities()[1]).id;
    assert(conflict.addSketchParallel(conflictSketch.id, cFirst, cSecond).success);
    assert(!conflict.addSketchPerpendicular(conflictSketch.id, cFirst, cSecond).success);

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("line-relations.pcad");
    assert(ProjectFile::save(path, perpendicular.document(), perpendicular.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loaded = std::dynamic_pointer_cast<SketchFeature>(
        loadedBody.findFeature(perpendicularSketch.id));
    assert(loaded && loaded->constraintCount() == 1
        && std::holds_alternative<cad::parametric::PerpendicularConstraint>(loaded->constraints().front()));
}

void angleBetweenLinesConstraints()
{
    cad::application::ModelingController controller;
    const auto created = controller.createSketch();
    assert(created.success);
    assert(controller.addSketchLine(created.id, {0, 0}, {10, 0}).success);
    assert(controller.addSketchLine(created.id, {5, 5}, {11, 8}).success);
    auto sketch = std::dynamic_pointer_cast<SketchFeature>(controller.body().findFeature(created.id));
    const auto referenceId = std::get<SketchLine>(sketch->entities()[0]).id;
    const auto dependentId = std::get<SketchLine>(sketch->entities()[1]).id;
    const auto dependentStart = std::get<SketchLine>(sketch->entities()[1]).start;
    const double dependentLength = std::get<SketchLine>(sketch->entities()[1]).start.Distance(
        std::get<SketchLine>(sketch->entities()[1]).end);
    const double radians30 = 30.0 * 3.14159265358979323846 / 180.0;
    assert(controller.addSketchAngleBetweenLines(created.id, referenceId, dependentId, radians30).success);
    const auto& solved = std::get<SketchLine>(sketch->entities()[1]);
    assert(solved.start.Distance(dependentStart) < 1.0e-7);
    assert(near(solved.start.Distance(solved.end), dependentLength));
    assert(near(std::atan2(solved.end.Y() - solved.start.Y(), solved.end.X() - solved.start.X()), radians30));

    const auto angleId = std::get<AngleBetweenLinesConstraint>(sketch->constraints().back()).id;
    assert(controller.updateSketchAngleBetweenLines(created.id, angleId,
        -45.0 * 3.14159265358979323846 / 180.0).success);
    assert(near(std::atan2(solved.end.Y() - solved.start.Y(), solved.end.X() - solved.start.X()),
        -45.0 * 3.14159265358979323846 / 180.0));
    controller.undo();
    assert(near(std::atan2(solved.end.Y() - solved.start.Y(), solved.end.X() - solved.start.X()), radians30));
    controller.redo();
    assert(near(std::atan2(solved.end.Y() - solved.start.Y(), solved.end.X() - solved.start.X()),
        -45.0 * 3.14159265358979323846 / 180.0));

    cad::application::ModelingController compatibility;
    const auto compatibleSketch = compatibility.createSketch();
    assert(compatibleSketch.success);
    assert(compatibility.addSketchLine(compatibleSketch.id, {0, 0}, {10, 0}).success);
    assert(compatibility.addSketchLine(compatibleSketch.id, {0, 5}, {5, 5}).success);
    auto compatible = std::dynamic_pointer_cast<SketchFeature>(
        compatibility.body().findFeature(compatibleSketch.id));
    const auto a = std::get<SketchLine>(compatible->entities()[0]).id;
    const auto b = std::get<SketchLine>(compatible->entities()[1]).id;
    assert(compatibility.addSketchParallel(compatibleSketch.id, a, b).success);
    assert(compatibility.addSketchAngleBetweenLines(compatibleSketch.id, a, b, 0.0).success);
    assert(!compatibility.addSketchAngleBetweenLines(compatibleSketch.id, a, b, radians30).success);

    QTemporaryDir directory;
    assert(directory.isValid());
    QString error;
    const auto path = directory.filePath("angle-between-lines.pcad");
    assert(ProjectFile::save(path, controller.document(), controller.body(), error));
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    assert(ProjectFile::load(path, loadedDocument, loadedBody, error));
    const auto loaded = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(created.id));
    assert(loaded && loaded->constraintCount() == 1
        && std::holds_alternative<AngleBetweenLinesConstraint>(loaded->constraints().front()));
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
    extendLineAndArc();
    extendClosesExtrudeProfile();
    constraintsSolveUndoAndPersist();
    dimensionalConstraintsSolveUndoAndPersist();
    positionalAndAngleConstraints();
    parallelAndPerpendicularConstraints();
    angleBetweenLinesConstraints();
    arcTrim();
    circleTrim();
    intersectionMatrix();
    undoRedo();
    downstreamAndPersistence();
    return 0;
}
