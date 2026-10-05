#pragma once

#include <TopoDS_Shape.hxx>

#include <QByteArray>
#include <QString>

namespace cad::persistence {

// Legacy version-1 JSON uses Base64. Container storage uses the raw B-Rep
// bytes directly inside an archive entry.
QByteArray encodeBRep(const TopoDS_Shape& shape);
TopoDS_Shape decodeBRep(const QByteArray& payload);
QByteArray encodeBRepRaw(const TopoDS_Shape& shape);
TopoDS_Shape decodeBRepRaw(const QByteArray& payload);

} // namespace cad::persistence
