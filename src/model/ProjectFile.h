#pragma once
#include <QString>
class Document;
namespace cad::parametric { class Body; }
namespace ProjectFile {
bool save(const QString& path, const Document& document, const cad::parametric::Body& body, QString& error);
bool load(const QString& path, Document& document, cad::parametric::Body& body, QString& error);
}
