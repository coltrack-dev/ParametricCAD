#include "model/ComponentIntegrity.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/ModelComponent.h"
#include "model/ProjectFile.h"
#include "application/ModelingController.h"
#include "operations/ParametricFeatures.h"

#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <QTemporaryDir>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {

class CompoundFeature final : public cad::parametric::ParametricFeature
{
public:
    CompoundFeature(std::string id, bool equal)
        : ParametricFeature(std::move(id), "Compound"), equal_(equal) {}

    const char* typeId() const noexcept override { return "TestCompound"; }
    Ptr clone(std::string newId) const override
    { return std::make_shared<CompoundFeature>(std::move(newId), equal_); }

protected:
    TopoDS_Shape build() const override
    {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        builder.Add(compound, BRepPrimAPI_MakeBox(10, 10, 10).Shape());
        builder.Add(compound, equal_
            ? BRepPrimAPI_MakeBox(10, 10, 10).Shape()
            : BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 5, 6, 7).Shape());
        return compound;
    }

private:
    bool equal_;
};

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

bool hasCode(const std::vector<cad::parametric::ComponentIntegrityIssue>& issues,
             const std::string& code)
{
    return std::any_of(issues.begin(), issues.end(),
        [&code](const auto& issue) { return issue.code == code; });
}

cad::parametric::ModelComponent oneComponent(
    const cad::parametric::Body& body, const std::string& featureId)
{
    const auto components = cad::parametric::ComponentResolver::enumerate(body, featureId);
    check(components.size() == 1, "expected one component");
    return components.front();
}

void c1FeatureDeletion()
{
    cad::application::ModelingController controller;
    auto source = std::make_shared<cad::parametric::BoxParametricFeature>("delete-source", 10, 10, 10);
    controller.body().addFeature(source);
    check(controller.body().recompute(), "C-1 recompute");
    controller.body().replaceComponents({oneComponent(controller.body(), source->id())});

    check(controller.deleteFeature(source->id()).success, "C-1 delete");
    check(controller.body().components().empty(), "C-1 metadata removed");
    check(!hasCode(cad::parametric::inspectComponentIntegrity(controller.body()),
                  "MISSING_SOURCE_FEATURE"), "C-1 stale metadata remains");
    controller.undo();
    check(controller.body().findFeature(source->id()) != nullptr, "C-1 undo feature");
    check(controller.body().components().size() == 1, "C-1 undo metadata");
    controller.redo();
    check(controller.body().findFeature(source->id()) == nullptr, "C-1 redo feature");
    check(controller.body().components().empty(), "C-1 redo metadata");
    check(!hasCode(cad::parametric::inspectComponentIntegrity(controller.body()),
                  "MISSING_SOURCE_FEATURE"), "C-1 redo stale metadata");
    QTemporaryDir directory;
    check(directory.isValid(), "C-1 temporary directory");
    Document document;
    Document loadedDocument;
    cad::parametric::Body loadedBody;
    QString error;
    const auto path = directory.filePath("deleted.pcad");
    check(ProjectFile::save(path, document, controller.body(), error), "C-1 save");
    check(ProjectFile::load(path, loadedDocument, loadedBody, error), "C-1 load");
    check(loadedBody.components().empty(), "C-1 save/load stale metadata");
}

void c2GeneratedCollision()
{
    cad::parametric::Body body;
    auto feature = std::make_shared<CompoundFeature>("equal-solids", true);
    body.addFeature(feature);
    check(body.recompute(), "C-2 recompute");
    const auto generated = cad::parametric::ComponentResolver::enumerate(body, feature->id());
    check(generated.size() == 2 && generated[0].id == generated[1].id,
        "C-2 metric collision was not reproduced");
    const auto issues = cad::parametric::inspectComponentIntegrity(body);
    check(hasCode(issues, "AMBIGUOUS_GENERATED_COMPONENT_ID"),
        "C-2 collision diagnostic");
    std::string error;
    check(!cad::parametric::ComponentResolver::resolve(body, generated.front().id, error),
        "C-2 ambiguous ID unexpectedly resolved");
}

void c3ReplacementChain()
{
    cad::application::ModelingController controller;
    auto source = std::make_shared<cad::parametric::BoxParametricFeature>("chain-source", 10, 10, 10);
    controller.body().addFeature(source);
    check(controller.body().recompute(), "C-3 source recompute");
    const auto sourceComponent = oneComponent(controller.body(), source->id());
    controller.body().replaceComponents({sourceComponent});

    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(2, 0, 0));
    const auto transform = controller.transformComponent(sourceComponent.id,
        cad::parametric::TransformOperation::Translate,
        cad::parametric::TransformResultMode::ReplaceResult, translation);
    check(transform.success && transform.componentIds.size() == 1, "C-3 transform");
    const auto trim = controller.trimBody(transform.componentIds, gp_Pnt(4, 0, 0),
        gp_Dir(1, 0, 0), cad::parametric::TrimSide::Positive);
    check(trim.success && trim.componentIds.size() == 1, "C-3 trim");
    const auto split = controller.splitComponent(trim.componentIds.front(),
        gp_Pnt(7, 0, 0), gp_Dir(1, 0, 0));
    check(split.success && split.componentIds.size() == 2, "C-3 split");

    const auto issues = cad::parametric::inspectComponentIntegrity(controller.body());
    check(hasCode(issues, "REPLACED_COMPONENT_RESOLVABLE"),
        "C-3 resolver replacement diagnostic");
    check(!hasCode(issues, "DANGLING_REPLACEMENT_REFERENCE"),
        "C-3 replacement chain has dangling reference");
    std::string error;
    check(static_cast<bool>(cad::parametric::ComponentResolver::resolve(
              controller.body(), sourceComponent.id, error)),
        "C-3 expected current resolver behavior for replaced ID");
}

void c4PartialMetadata()
{
    cad::parametric::Body body;
    auto feature = std::make_shared<CompoundFeature>("partial", false);
    body.addFeature(feature);
    check(body.recompute(), "C-4 recompute");
    const auto components = cad::parametric::ComponentResolver::enumerate(body, feature->id());
    check(components.size() == 2, "C-4 components");
    body.replaceComponents({components.front()});
    check(hasCode(cad::parametric::inspectComponentIntegrity(body),
                 "PARTIAL_COMPONENT_METADATA"), "C-4 partial metadata diagnostic");

    cad::application::ModelingController controller;
    auto partial = std::make_shared<CompoundFeature>("partial-operation", false);
    controller.body().addFeature(partial);
    check(controller.body().recompute(), "C-4 controller recompute");
    const auto all = cad::parametric::ComponentResolver::enumerate(
        controller.body(), partial->id());
    controller.body().replaceComponents({all.front()});
    std::string mergeError;
    const auto merged = cad::parametric::ComponentResolver::mergeFeatureMetadata(
        controller.body(), partial->id(), mergeError);
    check(mergeError.empty(), "C-4 metadata merge error");
    check(merged.size() == 2, "C-4 per-solid metadata merge");
    const auto result = controller.trimBody(
        {all.back().id}, gp_Pnt(22, 0, 0), gp_Dir(1, 0, 0),
        cad::parametric::TrimSide::Positive);
    check(result.success, "C-4 operation resolves generated partial component");
    check(!hasCode(cad::parametric::inspectComponentIntegrity(controller.body()),
                  "PARTIAL_COMPONENT_METADATA"), "C-4 partial state remains after merge");

    cad::application::ModelingController ambiguousController;
    auto ambiguous = std::make_shared<CompoundFeature>("ambiguous-partial", true);
    ambiguousController.body().addFeature(ambiguous);
    check(ambiguousController.body().recompute(), "C-4 ambiguous recompute");
    const auto ambiguousGenerated = cad::parametric::ComponentResolver::enumerate(
        ambiguousController.body(), ambiguous->id());
    ambiguousController.body().replaceComponents({ambiguousGenerated.front()});
    std::string ambiguousError;
    const auto ambiguousMerged = cad::parametric::ComponentResolver::mergeFeatureMetadata(
        ambiguousController.body(), ambiguous->id(), ambiguousError);
    check(ambiguousMerged.empty() && !ambiguousError.empty(),
          "C-4 ambiguous signature rejected");
}

void diagnosticIssueCoverage()
{
    cad::parametric::Body body;
    auto first = std::make_shared<cad::parametric::BoxParametricFeature>("issue-first", 10, 10, 10);
    auto second = std::make_shared<cad::parametric::BoxParametricFeature>("issue-second", 8, 8, 8);
    body.addFeature(first);
    body.addFeature(second);
    check(body.recompute(), "diagnostic fixture recompute");
    auto firstComponent = oneComponent(body, first->id());
    auto secondComponent = oneComponent(body, second->id());
    firstComponent.replacementComponentIds = {secondComponent.id, "missing-replacement"};
    firstComponent.technicalHidden = false;
    secondComponent.replacementComponentIds = {firstComponent.id};
    secondComponent.minX += 1000.0;
    body.replaceComponents({firstComponent, secondComponent});
    const auto issues = cad::parametric::inspectComponentIntegrity(body);
    check(hasCode(issues, "DANGLING_REPLACEMENT_REFERENCE"),
        "dangling replacement diagnostic");
    check(hasCode(issues, "REPLACEMENT_CYCLE"), "replacement cycle diagnostic");
    check(hasCode(issues, "REPLACED_COMPONENT_ACTIVE"),
        "active replacement diagnostic");
    check(hasCode(issues, "UNRESOLVED_COMPONENT_GEOMETRY"),
        "unresolved geometry diagnostic");
}

void deletionAfterComponentOperations()
{
    auto makeSource = [](const char* id) {
        return std::make_shared<cad::parametric::BoxParametricFeature>(id, 10, 10, 10);
    };

    {
        cad::application::ModelingController controller;
        auto source = makeSource("delete-transform");
        controller.body().addFeature(source);
        check(controller.body().recompute(), "delete transform recompute");
        const auto component = oneComponent(controller.body(), source->id());
        controller.body().replaceComponents({component});
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(2, 0, 0));
        const auto transform = controller.transformComponent(component.id,
            cad::parametric::TransformOperation::Translate,
            cad::parametric::TransformResultMode::ReplaceResult, translation);
        check(transform.success, "delete after transform");
        check(controller.deleteFeature(source->id()).success, "delete transform chain");
        check(controller.body().components().empty(), "transform delete metadata");
        controller.undo();
        check(controller.body().components().size() == 2, "transform delete undo metadata");
        controller.redo();
        check(controller.body().components().empty(), "transform delete redo metadata");
    }

    {
        cad::application::ModelingController controller;
        auto source = makeSource("delete-trim");
        controller.body().addFeature(source);
        check(controller.body().recompute(), "delete trim recompute");
        const auto component = oneComponent(controller.body(), source->id());
        controller.body().replaceComponents({component});
        const auto trim = controller.trimBody({component.id}, gp_Pnt(5, 0, 0),
            gp_Dir(1, 0, 0), cad::parametric::TrimSide::Positive);
        check(trim.success, "delete after trim");
        check(controller.deleteFeature(source->id()).success, "delete trim chain");
        check(controller.body().components().empty(), "trim delete metadata");
        controller.undo();
        check(controller.body().components().size() == 2, "trim delete undo metadata");
    }

    {
        cad::application::ModelingController controller;
        auto source = makeSource("delete-split");
        controller.body().addFeature(source);
        check(controller.body().recompute(), "delete split recompute");
        const auto component = oneComponent(controller.body(), source->id());
        controller.body().replaceComponents({component});
        const auto split = controller.splitComponent(component.id, gp_Pnt(5, 0, 0),
            gp_Dir(1, 0, 0));
        check(split.success, "delete after split");
        check(controller.deleteFeature(source->id()).success, "delete split chain");
        check(controller.body().components().empty(), "split delete metadata");
        controller.undo();
        check(controller.body().components().size() == 3, "split delete undo metadata");
    }

    {
        cad::application::ModelingController controller;
        auto source = std::make_shared<CompoundFeature>("delete-multi", false);
        controller.body().addFeature(source);
        check(controller.body().recompute(), "delete multi recompute");
        controller.body().replaceComponents(cad::parametric::ComponentResolver::enumerate(
            controller.body(), source->id()));
        check(controller.deleteFeature(source->id()).success, "delete multi");
        check(controller.body().components().empty(), "delete multi metadata");
        controller.undo();
        check(controller.body().components().size() == 2, "delete multi undo metadata");
    }
}

void scenarioARepeatedTransform()
{
    cad::application::ModelingController controller;
    auto source = std::make_shared<cad::parametric::BoxParametricFeature>("repeat-transform", 10, 10, 10);
    controller.body().addFeature(source);
    check(controller.body().recompute(), "A recompute");
    auto current = oneComponent(controller.body(), source->id()).id;
    controller.body().replaceComponents({oneComponent(controller.body(), source->id())});
    for (int index = 0; index < 20; ++index) {
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(1, 0, 0));
        const auto result = controller.transformComponent(current,
            cad::parametric::TransformOperation::Translate,
            cad::parametric::TransformResultMode::ReplaceResult, translation);
        check(result.success && result.componentIds.size() == 1, "A transform");
        current = result.componentIds.front();
    }
    check(controller.body().features().size() == 21, "A feature count");
    check(controller.body().components().size() == 21, "A metadata count");
    check(hasCode(cad::parametric::inspectComponentIntegrity(controller.body()),
                 "REPLACED_COMPONENT_RESOLVABLE"), "A expected replacement diagnostic");
}

void scenarioBRepeatedTrim()
{
    cad::application::ModelingController controller;
    auto source = std::make_shared<cad::parametric::BoxParametricFeature>("repeat-trim", 20, 10, 10);
    controller.body().addFeature(source);
    check(controller.body().recompute(), "B recompute");
    auto current = oneComponent(controller.body(), source->id()).id;
    controller.body().replaceComponents({oneComponent(controller.body(), source->id())});
    for (int index = 0; index < 4; ++index) {
        const auto result = controller.trimBody({current}, gp_Pnt(2 + index, 0, 0),
            gp_Dir(1, 0, 0), cad::parametric::TrimSide::Positive);
        check(result.success && result.componentIds.size() == 1, "B trim");
        current = result.componentIds.front();
    }
    check(controller.body().components().size() == 5, "B metadata count");
    check(!cad::parametric::inspectComponentIntegrity(controller.body()).empty(),
        "B diagnostics should expose historical replacements");
}

void scenarioCUndoRedo()
{
    cad::application::ModelingController controller;
    auto source = std::make_shared<cad::parametric::BoxParametricFeature>("undo-split", 10, 10, 10);
    controller.body().addFeature(source);
    check(controller.body().recompute(), "C recompute");
    const auto component = oneComponent(controller.body(), source->id());
    controller.body().replaceComponents({component});
    const auto split = controller.splitComponent(component.id, gp_Pnt(5, 0, 0), gp_Dir(1, 0, 0));
    check(split.success && split.componentIds.size() == 2, "C split");
    controller.undo();
    check(controller.body().findFeature(split.id) == nullptr, "C undo split");
    controller.redo();
    check(controller.body().findFeature(split.id) != nullptr, "C redo split");
    check(controller.body().components().size() == 3, "C redo metadata");
}

void scenarioEAndFSaveLoadRecompute()
{
    QTemporaryDir directory;
    check(directory.isValid(), "E temporary directory");
    cad::parametric::Body body;
    auto sketch = std::make_shared<cad::parametric::SketchFeature>(
        "sketch", cad::parametric::SketchSupportType::XY, 20, 20,
        std::vector<cad::parametric::SketchEntity>{
            cad::parametric::SketchLine{{0, 0}, {10, 0}, "a"},
            cad::parametric::SketchLine{{10, 0}, {10, 10}, "b"},
            cad::parametric::SketchLine{{10, 10}, {0, 10}, "c"},
            cad::parametric::SketchLine{{0, 10}, {0, 0}, "d"}});
    auto extrude = std::make_shared<cad::parametric::ExtrudeFeature>(
        "extrude", sketch, 10.0);
    body.addFeature(sketch);
    body.addFeature(extrude);
    check(body.recompute(), "F initial recompute");
    const auto component = oneComponent(body, extrude->id());
    body.replaceComponents({component});
    const auto path = directory.filePath("phase1.pcad");
    QString error;
    Document document;
    check(ProjectFile::save(path, document, body, error), "E save");
    Document loadedDocument;
    cad::parametric::Body loaded;
    check(ProjectFile::load(path, loadedDocument, loaded, error), "E load");
    check(loaded.components().size() == body.components().size()
              && loaded.components().front().id == body.components().front().id,
          "E component identity roundtrip");

    auto loadedSketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        loaded.findFeature("sketch"));
    check(loadedSketch != nullptr, "F loaded sketch");
    loadedSketch->replaceEntities(0, 2, {
        cad::parametric::SketchLine{{0, 0}, {12, 0}, "a"},
        cad::parametric::SketchLine{{12, 0}, {10, 10}, "b"}});
    loaded.markDirtyFrom("sketch");
    check(loaded.recompute(), "F recompute after sketch edit");
    std::string resolveError;
    check(static_cast<bool>(cad::parametric::ComponentResolver::resolve(
              loaded, loaded.components().front().id, resolveError)),
        "F component resolution after edit");
}

} // namespace

int main()
{
    try {
        c1FeatureDeletion();
        c2GeneratedCollision();
        c3ReplacementChain();
        c4PartialMetadata();
        diagnosticIssueCoverage();
        deletionAfterComponentOperations();
        scenarioARepeatedTransform();
        scenarioBRepeatedTrim();
        scenarioCUndoRedo();
        scenarioEAndFSaveLoadRecompute();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
