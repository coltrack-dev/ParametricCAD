#include "model/ShapePayload.h"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <sstream>
#include <stdexcept>

namespace cad::persistence {

QByteArray encodeBRepRaw(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) throw std::invalid_argument("Cannot serialize a null B-Rep shape");
    std::ostringstream stream;
    BRepTools::Write(shape, stream);
    const auto value = stream.str();
    if (value.empty()) throw std::runtime_error("B-Rep serialization produced no data");
    return QByteArray::fromStdString(value);
}

TopoDS_Shape decodeBRepRaw(const QByteArray& payload)
{
    if (payload.isEmpty()) throw std::runtime_error("Invalid B-Rep payload");
    std::istringstream stream(payload.toStdString());
    TopoDS_Shape shape;
    BRep_Builder builder;
    BRepTools::Read(shape, stream, builder);
    if (shape.IsNull()) throw std::runtime_error("B-Rep payload contains a null shape");
    return shape;
}

QByteArray encodeBRep(const TopoDS_Shape& shape)
{
    return encodeBRepRaw(shape).toBase64();
}

TopoDS_Shape decodeBRep(const QByteArray& payload)
{
    return decodeBRepRaw(QByteArray::fromBase64(payload));
}

} // namespace cad::persistence
