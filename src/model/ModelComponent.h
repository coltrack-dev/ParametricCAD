#pragma once

#include <TopoDS_Shape.hxx>

#include <optional>
#include <string>
#include <vector>

namespace cad::parametric {

/** Persistent identity for one independently addressable result component.
 *
 * A component is deliberately not identified by the ordinal of a solid in a
 * compound.  The signature is used by ComponentResolver to re-find the
 * current solid after recompute/load; sourceFeatureId remains the semantic
 * owner of the result.
 */
struct ModelComponent
{
    std::string id;
    std::string name;
    std::string role{"solid"};
    std::string sourceFeatureId;
    std::optional<std::string> parentComponentId;
    double minX{0.0};
    double minY{0.0};
    double minZ{0.0};
    double maxX{0.0};
    double maxY{0.0};
    double maxZ{0.0};
    double volume{0.0};
    bool visible{true};
};

struct ComponentResolution
{
    ModelComponent component;
    TopoDS_Shape shape;
};

class Body;

class ComponentResolver final
{
public:
    static std::vector<ModelComponent> enumerate(const Body& body,
                                                 const std::string& featureId);
    static std::vector<ComponentResolution> resolveFeature(
        const Body& body, const std::string& featureId,
        double tolerance = 1.0e-6);
    static std::optional<ComponentResolution> resolve(
        const Body& body, const std::string& componentId,
        std::string& error, double tolerance = 1.0e-6);
};

} // namespace cad::parametric
