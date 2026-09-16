#pragma once
#include <string>
#include <vector>

namespace cad::parametric {
class Body;
// Derived presentation policy: no separate visibility history is needed for Cut.
std::vector<std::string> hiddenFeatureIds(const Body& body);
}
