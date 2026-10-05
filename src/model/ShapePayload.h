#pragma once

#include <TopoDS_Shape.hxx>

#include <QByteArray>
#include <QString>

namespace cad::persistence {

// Transitional embedded payload used by the version-1 JSON container.  The
// feature model only deals with this abstraction; the storage can later move
// to .pcad archive entries without changing ImportedFeature.
QByteArray encodeBRep(const TopoDS_Shape& shape);
TopoDS_Shape decodeBRep(const QByteArray& payload);

} // namespace cad::persistence
