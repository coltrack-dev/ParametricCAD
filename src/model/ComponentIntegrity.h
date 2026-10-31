#pragma once

#include "model/Body.h"

#include <string>
#include <vector>

namespace cad::parametric {

enum class ComponentIntegritySeverity
{
    Warning,
    Error
};

struct ComponentIntegrityIssue
{
    std::string code;
    std::string featureId;
    std::string componentId;
    std::string description;
    ComponentIntegritySeverity severity{ComponentIntegritySeverity::Error};
};

/** Read-only validation of persisted and derived component state. */
std::vector<ComponentIntegrityIssue> inspectComponentIntegrity(
    const Body& body, double tolerance = 1.0e-6);

} // namespace cad::parametric
