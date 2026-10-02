#pragma once

#include "model/Body.h"

#include <string>
#include <vector>

class CadViewer;

namespace cad::viewer {

struct PresentationResult
{
    bool rebuilt{false};
    std::vector<std::string> presentedIds;
    std::string error;
};

class ModelPresenter final
{
public:
    ModelPresenter(cad::parametric::Body& body, CadViewer& viewer);

    PresentationResult refresh();
    void clear();

private:
    cad::parametric::Body& body_;
    CadViewer& viewer_;
};

} // namespace cad::viewer
