#include "application/VisibilityManager.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "model/ProjectFile.h"
#include "operations/ParametricFeatures.h"

#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr double length = 13000.0;
constexpr double width = 9000.0;
constexpr double firstFloorHeight = 3000.0;
constexpr double rightAtticWallHeight = 1500.0;
constexpr double leftSlopeDegrees = 60.0;
constexpr double rightSlopeDegrees = 25.0;
constexpr double wallThickness = 380.0;
constexpr double roofOverhang = 400.0;

struct HouseBuilder
{
    cad::parametric::Body body;
    int sequence{0};

    std::shared_ptr<cad::parametric::BoxParametricFeature> box(
        const std::string& name, const double boxWidth, const double boxDepth,
        const double boxHeight, const gp_Trsf& placement = gp_Trsf())
    {
        const auto id = QStringLiteral("house-%1").arg(++sequence, 3, 10, QLatin1Char('0'))
            .toStdString();
        auto feature = std::make_shared<cad::parametric::BoxParametricFeature>(
            id, boxWidth, boxDepth, boxHeight);
        feature->setName(name);
        feature->setPlacement(placement);
        body.addFeature(feature);
        return feature;
    }

    gp_Trsf at(const double x, const double y, const double z) const
    {
        gp_Trsf result;
        result.SetTranslationPart(gp_Vec(x, y, z));
        return result;
    }

    gp_Trsf slope(const double degrees, const double x, const double y,
                 const double z) const
    {
        gp_Trsf result;
        result.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)),
            degrees * std::acos(-1.0) / 180.0);
        result.SetTranslationPart(gp_Vec(x, y, z));
        return result;
    }

    void window(const std::string& name, const double x, const double y,
                const double z, const double windowWidth, const double windowHeight)
    {
        box(name + " glazing", 35, windowWidth, windowHeight, at(x, y, z));
        box(name + " frame bottom", 55, 65, 65, at(x - 10, y, z));
        box(name + " frame top", 55, 65, 65, at(x - 10, y, z + windowHeight - 65));
        box(name + " frame left", 55, 65, windowHeight, at(x - 10, y, z));
        box(name + " frame right", 55, 65, windowHeight,
            at(x - 10, y + windowWidth - 65, z));
        box(name + " center mullion", 55, 44, windowHeight,
            at(x - 10, y + windowWidth / 2.0 - 22, z));
    }

    void build()
    {
        const auto leftSlopeRadians = leftSlopeDegrees * std::acos(-1.0) / 180.0;
        const auto rightSlopeRadians = rightSlopeDegrees * std::acos(-1.0) / 180.0;
        const auto leftTangent = std::tan(leftSlopeRadians);
        const auto rightTangent = std::tan(rightSlopeRadians);
        const auto ridgeY = (rightAtticWallHeight + rightTangent * width)
            / (leftTangent + rightTangent);
        const auto ridgeZ = firstFloorHeight + leftTangent * ridgeY;
        const auto leftRoofDepth = (ridgeY + roofOverhang) / std::cos(leftSlopeRadians);
        const auto rightRoofDepth = (width - ridgeY + roofOverhang)
            / std::cos(rightSlopeRadians);

        box("Brick long front wall", length, wallThickness, firstFloorHeight, at(0, 0, 0));
        box("Brick long rear wall", length, wallThickness, firstFloorHeight,
            at(0, width - wallThickness, 0));
        box("Brick left facade beside door", wallThickness, 1200, firstFloorHeight,
            at(0, 0, 0));
        box("Brick left facade beside window", wallThickness, 3000, firstFloorHeight,
            at(0, 2400, 0));
        box("Brick left facade end", wallThickness, 2000, firstFloorHeight,
            at(0, 7000, 0));
        box("Brick right facade below window", wallThickness, 3500, 1200,
            at(length - wallThickness, 0, 0));
        box("Brick right facade beside window", wallThickness, 3500, firstFloorHeight,
            at(length - wallThickness, 0, 0));
        box("Brick right facade end", wallThickness, 4000, firstFloorHeight,
            at(length - wallThickness, 5000, 0));
        box("Door left jamb", wallThickness, 100, 2200, at(0, 1200, 0));
        box("Door right jamb", wallThickness, 100, 2200, at(0, 2300, 0));
        box("Door lintel", wallThickness, 1200, 800, at(0, 1200, 2200));
        box("Right facade window sill", wallThickness, 1500, 700,
            at(length - wallThickness, 3500, 0));
        box("Right facade window lintel", wallThickness, 1500, 600,
            at(length - wallThickness, 3500, 2400));
        box("Ground floor lintel", length, wallThickness, 350,
            at(0, 0, firstFloorHeight - 350));

        box("Upper floor base slab", length, width - 2 * wallThickness, 180,
            at(0, wallThickness, firstFloorHeight));
        box("Brick horizontal cornice", length, 450, 220, at(0, -150, firstFloorHeight));
        box("Upper long front OSB wall", length, wallThickness, 2200,
            at(0, 0, 3180));
        box("Right attic side wall 1500 mm", length, wallThickness,
            rightAtticWallHeight, at(0, width - wallThickness, firstFloorHeight));
        box("Upper left facade panel", 160, 2400, 2200, at(0, 120, 3180));
        box("Upper left facade window panel", 160, 1800, 2200, at(0, 5400, 3180));
        box("Upper left facade end panel", 160, 1800, 2200, at(0, 7200, 3180));
        box("Upper right facade panel", 160, 3000, 2200,
            at(length - 160, 120, 3180));
        box("Upper right facade window panel", 160, 1800, 2200,
            at(length - 160, 4800, 3180));
        box("Upper right facade end panel", 160, 3830, 2200,
            at(length - 160, 6770, 3180));
        box("Left gable wood base", 160, width, 450, at(-160, 0, 3000));
        box("Left gable wood middle", 160, 6000, 500, at(-160, 1500, 4500));
        box("Left gable wood upper", 160, 3000, 500, at(-160, 3000, 6000));
        box("Right gable OSB base", 160, width, 450, at(length, 0, 3000));
        box("Right gable OSB middle", 160, 6000, 500, at(length, 1500, 4500));
        box("Right gable OSB upper", 160, 3000, 500, at(length, 3000, 6000));

        window("Left ground facade window", -55, 5400, 1100, 1500, 1100);
        window("Right ground facade window", length, 3500, 1100, 1500, 1100);
        window("Left upper facade window", -55, 4200, 4300, 1500, 900);
        window("Right upper facade window", length, 4300, 4300, 1500, 900);

        box("Left steep roof slope", length + 2 * roofOverhang, leftRoofDepth,
            170, slope(leftSlopeDegrees, -roofOverhang, -roofOverhang,
                ridgeZ - leftTangent * (ridgeY + roofOverhang)));
        box("Right shallow roof slope", length + 2 * roofOverhang, rightRoofDepth,
            170, slope(-rightSlopeDegrees, -roofOverhang, ridgeY, ridgeZ));

        for (int index = 0; index < 6; ++index) {
            const auto x = 500.0 + index * 1700.0;
            box("Roof rafter " + std::to_string(index + 1), 100, 620, 155,
                slope(-rightSlopeDegrees, x, -roofOverhang, ridgeZ
                    - rightTangent * (width - ridgeY + roofOverhang)));
        }
        box("Left roof fascia", length + 2 * roofOverhang, 180, 260,
            slope(leftSlopeDegrees, -roofOverhang, -roofOverhang,
                ridgeZ - leftTangent * (ridgeY + roofOverhang)));
        box("Right roof fascia", length + 2 * roofOverhang, 180, 260,
            slope(-rightSlopeDegrees, -roofOverhang, width + roofOverhang,
                ridgeZ - rightTangent * (width - ridgeY + roofOverhang)));
        box("Ridge cap", length + 2 * roofOverhang, 260, 220,
            slope(-rightSlopeDegrees, -roofOverhang, ridgeY, ridgeZ));

        if (sequence != 65) throw std::runtime_error("Unexpected generated base feature count");
        if (!body.recompute()) throw std::runtime_error(body.lastError());

        std::vector<cad::parametric::ModelComponent> componentMetadata;
        const auto trimPlane = [this, &componentMetadata](const std::string& sourceId,
            const gp_Pnt& origin, const gp_Dir& normal, const std::string& suffix) {
            auto source = body.findFeature(sourceId);
            if (!source) throw std::runtime_error("Trim source feature is missing");
            const auto components = cad::parametric::ComponentResolver::enumerate(body, sourceId);
            if (components.size() != 1) throw std::runtime_error("Trim source must have one component");
            const auto component = components.front();
            const auto trimId = "trim-" + sourceId + "-" + suffix;
            auto trim = std::make_shared<cad::parametric::TrimBodyFeature>(
                trimId, source, component, origin, normal,
                cad::parametric::TrimSide::Negative);
            body.addFeature(trim);
            if (!body.recompute())
                throw std::runtime_error("Trim " + trimId + " failed: " + body.lastError());
            const auto results = cad::parametric::ComponentResolver::enumerate(body, trimId);
            if (results.size() != 1) throw std::runtime_error("Trim must have one result component");
            auto result = results.front();
            result.parentComponentId = component.id;
            auto sourceMetadata = std::find_if(componentMetadata.begin(), componentMetadata.end(),
                [&component](const auto& value) { return value.id == component.id; });
            if (sourceMetadata == componentMetadata.end()) {
                sourceMetadata = componentMetadata.insert(componentMetadata.end(), component);
            }
            sourceMetadata->visible = false;
            sourceMetadata->technicalHidden = true;
            sourceMetadata->replacementComponentIds = {result.id};
            componentMetadata.push_back(result);
            return trimId;
        };
        const auto leftPlaneOrigin = gp_Pnt(0, 0, firstFloorHeight);
        const auto leftPlaneNormal = gp_Dir(0, -std::tan(leftSlopeRadians), 1);
        const auto rightPlaneOrigin = gp_Pnt(0, ridgeY, ridgeZ);
        const auto rightPlaneNormal = gp_Dir(0, std::tan(rightSlopeRadians), 1);
        for (const auto& sourceId : {"house-025", "house-026", "house-027",
                                     "house-028", "house-029", "house-030"}) {
            auto current = trimPlane(sourceId, leftPlaneOrigin, leftPlaneNormal, "left-slope");
            trimPlane(current, rightPlaneOrigin, rightPlaneNormal, "right-slope");
        }
        body.replaceComponents(std::move(componentMetadata));
        if (!body.recompute()) throw std::runtime_error(body.lastError());
    }
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const auto output = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().filePath("examples/house_facade_photo.pcad");
    try {
        HouseBuilder builder;
        builder.build();
        Document document;
        cad::application::VisibilityManager visibility;
        QString error;
        if (!ProjectFile::save(output, document, builder.body, error, &visibility))
            throw std::runtime_error(error.toStdString());
        std::cout << "Generated " << output.toStdString() << " with "
                  << builder.body.features().size() << " features\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
