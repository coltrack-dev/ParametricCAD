#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "model/ModelComponent.h"
#include "model/ProjectFile.h"
#include "operations/ParametricFeatures.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace {

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

cad::parametric::ModelComponent componentFor(
    const cad::parametric::Body& body, const std::string& featureId)
{
    const auto components = cad::parametric::ComponentResolver::enumerate(body, featureId);
    if (components.size() != 1) throw std::runtime_error("expected one component");
    return components.front();
}

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main()
{
    using namespace cad::parametric;
    try {
        Body body;
        auto source = std::make_shared<BoxParametricFeature>("source", 10, 10, 10);
        body.addFeature(source);
        check(body.recompute(), "source recompute");
        const auto component = componentFor(body, "source");
        body.replaceComponents({component});

        auto positive = std::make_shared<TrimBodyFeature>("positive", source, component,
            gp_Pnt(5, 0, 0), gp_Dir(1, 0, 0), TrimSide::Positive);
        body.addFeature(positive);
        check(body.recompute(), "positive trim recompute");
        check(std::abs(volume(positive->shape()) - 500.0) < 1.0e-6,
            "positive trim volume");

        Body negativeBody;
        auto negativeSource = std::make_shared<BoxParametricFeature>("negative-source", 10, 10, 10);
        negativeBody.addFeature(negativeSource);
        check(negativeBody.recompute(), "negative source recompute");
        const auto negativeComponent = componentFor(negativeBody, "negative-source");
        auto negative = std::make_shared<TrimBodyFeature>("negative", negativeSource,
            negativeComponent, gp_Pnt(5, 0, 0), gp_Dir(1, 0, 0), TrimSide::Negative);
        negativeBody.addFeature(negative);
        check(negativeBody.recompute(), "negative trim recompute");
        check(std::abs(volume(negative->shape()) - 500.0) < 1.0e-6,
            "negative trim volume");

        auto noIntersection = std::make_shared<TrimBodyFeature>("no-intersection", source,
            component, gp_Pnt(-5, 0, 0), gp_Dir(1, 0, 0), TrimSide::Positive);
        check(noIntersection->recompute(), "non-intersecting keep-side trim");
        check(std::abs(volume(noIntersection->shape()) - 1000.0) < 1.0e-6,
            "non-intersecting trim preserves source");

        auto empty = std::make_shared<TrimBodyFeature>("empty", source, component,
            gp_Pnt(20, 0, 0), gp_Dir(1, 0, 0), TrimSide::Positive);
        check(!empty->recompute() && empty->error().find("entire") != std::string::npos,
            "empty trim is rejected");

        auto angled = std::make_shared<TrimBodyFeature>("angled", source, component,
            gp_Pnt(0, 0, 0), gp_Dir(1, 1, 0), TrimSide::Positive, 1.0);
        check(angled->recompute(), "angled trim recompute");
        check(volume(angled->shape()) > 0.0 && volume(angled->shape()) < 1000.0,
            "angled trim volume");

        source->setNumericProperty("width", 20.0);
        body.markDirtyFrom(source->id());
        check(body.recompute(), "parametric trim recompute");
        check(std::abs(volume(positive->shape()) - 1500.0) < 1.0e-6,
            "trim follows source edit");

        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory");
        QString error;
        const auto path = directory.filePath("trim.pcad");
        Document document;
        check(ProjectFile::save(path, document, body, error), "trim save");
        Document loadedDocument;
        Body loadedBody;
        check(ProjectFile::load(path, loadedDocument, loadedBody, error), "trim load");
        const auto loadedTrim = loadedBody.findFeature("positive");
        check(loadedTrim && std::string(loadedTrim->typeId()) == "TrimBody"
                  && !loadedTrim->shape().IsNull(), "trim persistence");

        cad::topology::TopologicalReference invalidReference;
        invalidReference.featureId = "source";
        invalidReference.kind = cad::topology::TopologicalKind::Face;
        invalidReference.transientIndex = 9999;
        auto invalidFaceTrim = std::make_shared<TrimBodyFeature>("invalid-face", source,
            component, source, invalidReference, TrimSide::Positive);
        check(!invalidFaceTrim->recompute(), "invalid plane reference is rejected");
    } catch (const std::exception& exception) {
        return (std::fprintf(stderr, "%s\n", exception.what()), 1);
    }
    return 0;
}
