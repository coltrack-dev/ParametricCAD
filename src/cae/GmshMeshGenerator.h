#pragma once

#include "cae/MeshGenerator.h"

#include <QString>

namespace cad::cae {

class GmshMeshGenerator final : public IMeshGenerator
{
public:
    explicit GmshMeshGenerator(QString executable = QStringLiteral("gmsh"));
    MeshGenerationResult generate(const TopoDS_Shape& shape,
                                  const MeshSettings& settings,
                                  const std::atomic_bool* cancel = nullptr) override;

private:
    QString executable_;
};

} // namespace cad::cae
