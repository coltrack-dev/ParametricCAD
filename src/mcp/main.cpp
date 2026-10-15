#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>
#include <QTimer>

namespace {
constexpr int TimeoutMs = 5000;

QJsonArray toolDefinitions()
{
    const QJsonObject empty{{"type", "object"}, {"additionalProperties", false}};
    const QJsonObject feature{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"featureId"}}};
    const QJsonObject units{{"type", "string"}, {"enum", QJsonArray{"mm"}},
        {"description", "Model length unit; ParametricCAD uses millimeters"}};
    const QJsonObject newProject{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"units", units}}}};
    const QJsonObject project{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"path", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"path"}}};
    const QJsonObject sketch{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"support", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"XY", "XZ", "YZ"}}}},
            {"faceReference", QJsonObject{{"type", "object"}, {"description", "Serialized TopologicalReference for a planar face"}}},
            {"units", units}}}};
    const QJsonObject rectangle{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"sketchId", QJsonObject{{"type", "string"}}}, {"x", QJsonObject{{"type", "number"}}},
            {"y", QJsonObject{{"type", "number"}}}, {"width", QJsonObject{{"type", "number"}}},
            {"height", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "x", "y", "width", "height", "units"}}};
    const QJsonObject circle{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"sketchId", QJsonObject{{"type", "string"}}}, {"x", QJsonObject{{"type", "number"}}},
            {"y", QJsonObject{{"type", "number"}}}, {"radius", QJsonObject{{"type", "number"}}},
            {"units", units}}},
        {"required", QJsonArray{"sketchId", "x", "y", "radius", "units"}}};
    const QJsonObject extrude{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"distance", QJsonObject{{"type", "number"}}}, {"reversed", QJsonObject{{"type", "boolean"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "distance", "units"}}};
    const QJsonObject pocket{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"depth", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "depth", "units"}}};
    const QJsonObject edit{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"property", QJsonObject{{"type", "string"}}}, {"value", QJsonObject{}}, {"units", units}}},
        {"required", QJsonArray{"featureId", "property", "value"}}};
    const QJsonObject recompute{{"type", "object"}, {"additionalProperties", false}};
    const QJsonObject sketchId{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"sketchId"}}};
    const QJsonObject line{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"x1", QJsonObject{{"type", "number"}}}, {"y1", QJsonObject{{"type", "number"}}},
            {"x2", QJsonObject{{"type", "number"}}}, {"y2", QJsonObject{{"type", "number"}}},
            {"construction", QJsonObject{{"type", "boolean"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "x1", "y1", "x2", "y2", "units"}}};
    const QJsonObject editLine{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"entityId", QJsonObject{{"type", "string"}}}, {"x1", QJsonObject{{"type", "number"}}},
            {"y1", QJsonObject{{"type", "number"}}}, {"x2", QJsonObject{{"type", "number"}}},
            {"y2", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "entityId", "x1", "y1", "x2", "y2", "units"}}};
    const QJsonObject removeEntity{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"entityId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"sketchId", "entityId"}}};
    const QJsonObject topology{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"featureId"}}};
    const QJsonObject chamfer{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"corners", QJsonObject{{"type", "array"}}}, {"featureId", QJsonObject{{"type", "string"}}},
            {"edgeReferences", QJsonObject{{"type", "array"}}}, {"distance", QJsonObject{{"type", "number"}}},
            {"units", units}}},
        {"required", QJsonArray{"distance", "units"}}};
    const QJsonObject tools = QJsonObject{
        {"cad.get_model_tree", QJsonObject{{"description", "Inspect the parametric model tree"}, {"inputSchema", empty}}},
        {"cad.get_feature", QJsonObject{{"description", "Inspect one parametric feature"}, {"inputSchema", feature}}},
        {"cad.inspect_shape", QJsonObject{{"description", "Inspect OCCT shape validity and metrics"}, {"inputSchema", feature}}},
        {"cad.get_selection", QJsonObject{{"description", "Read the current viewer selection"}, {"inputSchema", empty}}},
        {"cad.get_view_state", QJsonObject{{"description", "Read viewer camera and display state"}, {"inputSchema", empty}}},
        {"cad.screenshot", QJsonObject{{"description", "Capture the current viewer as PNG"}, {"inputSchema", empty}}},
        {"cad.get_application_status", QJsonObject{{"description", "Read application and automation status"}, {"inputSchema", empty}}},
        {"cad.new_project", QJsonObject{{"description", "Create an empty parametric project"}, {"inputSchema", newProject}}},
        {"cad.open_project", QJsonObject{{"description", "Open a .pcad project"}, {"inputSchema", project}}},
        {"cad.save_project", QJsonObject{{"description", "Save the current parametric project as .pcad"}, {"inputSchema", project}}},
        {"cad.create_sketch", QJsonObject{{"description", "Create a global or planar-face-attached Sketch"}, {"inputSchema", sketch}}},
        {"cad.add_rectangle", QJsonObject{{"description", "Add four parametric line entities to a Sketch"}, {"inputSchema", rectangle}}},
        {"cad.add_circle", QJsonObject{{"description", "Add a parametric circle entity to a Sketch"}, {"inputSchema", circle}}},
        {"cad.extrude", QJsonObject{{"description", "Create an Extrude from a Sketch"}, {"inputSchema", extrude}}},
        {"cad.pocket", QJsonObject{{"description", "Create a Pocket from a face-attached Sketch"}, {"inputSchema", pocket}}},
        {"cad.recompute", QJsonObject{{"description", "Recompute the parametric document"}, {"inputSchema", recompute}}},
        {"cad.edit_feature", QJsonObject{{"description", "Edit an existing editable feature property"}, {"inputSchema", edit}}},
        {"cad.undo", QJsonObject{{"description", "Undo the latest modeling command"}, {"inputSchema", empty}}},
        {"cad.redo", QJsonObject{{"description", "Redo the latest undone modeling command"}, {"inputSchema", empty}}},
        {"cad.get_sketch_entities", QJsonObject{{"description", "Inspect Sketch entities and constraints"}, {"inputSchema", sketchId}}},
        {"cad.add_line", QJsonObject{{"description", "Add a parametric Sketch Line"}, {"inputSchema", line}}},
        {"cad.edit_sketch_entity", QJsonObject{{"description", "Edit a Sketch Line with Undo/Redo"}, {"inputSchema", editLine}}},
        {"cad.remove_sketch_entity", QJsonObject{{"description", "Remove an unconstrained Sketch entity with Undo/Redo"}, {"inputSchema", removeEntity}}},
        {"cad.get_topology", QJsonObject{{"description", "Get stable face and edge topology references"}, {"inputSchema", topology}}},
        {"cad.chamfer", QJsonObject{{"description", "Chamfer Sketch corners or referenced 3D edges"}, {"inputSchema", chamfer}}}
    };
    QJsonArray result;
    for (auto it = tools.begin(); it != tools.end(); ++it) {
        QJsonObject item = it.value().toObject();
        item.insert("name", it.key());
        result.append(item);
    }
    return result;
}

QJsonObject errorResponse(const QJsonValue& id, int code, const QString& message)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id},
        {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject callApplication(const QString& socketName, const QString& tool,
                            const QJsonObject& arguments, const QJsonValue& id)
{
    QLocalSocket socket;
    socket.connectToServer(socketName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(TimeoutMs))
        return errorResponse(id, -32001, "ParametricCAD is not connected: " + socket.errorString());
    const QJsonObject request{{"jsonrpc", "2.0"}, {"id", id}, {"method", "automation/call"},
        {"params", QJsonObject{{"tool", tool}, {"arguments", arguments}}}};
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(TimeoutMs) || !socket.waitForReadyRead(TimeoutMs))
        return errorResponse(id, -32002, "Timed out waiting for ParametricCAD");
    QByteArray line = socket.readLine();
    while (!line.endsWith('\n') && socket.waitForReadyRead(TimeoutMs)) line += socket.readLine();
    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isObject())
        return errorResponse(id, -32003, "ParametricCAD returned invalid JSON");
    return response.object();
}

QJsonObject handle(const QJsonObject& request, const QString& socketName)
{
    const QJsonValue id = request.value("id");
    const QString method = request.value("method").toString();
    if (request.value("jsonrpc").toString() != "2.0") return errorResponse(id, -32600, "JSON-RPC 2.0 is required");
    if (method == "initialize") {
        return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{
            {"protocolVersion", "2025-06-18"},
            {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
            {"serverInfo", QJsonObject{{"name", "parametriccad-mcp"}, {"version", "0.1.0"}}}}}};
    }
    if (method == "notifications/initialized") return {};
    if (method == "tools/list") return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{{"tools", toolDefinitions()}}}};
    if (method != "tools/call") return errorResponse(id, -32601, "Method not found");
    const QJsonObject params = request.value("params").toObject();
    const QString tool = params.value("name").toString();
    bool known = false;
    for (const auto& value : toolDefinitions()) if (value.toObject().value("name").toString() == tool) known = true;
    if (!known) return errorResponse(id, -32601, "Unknown tool: " + tool);
    const QJsonObject appResponse = callApplication(socketName, tool, params.value("arguments").toObject(), id);
    if (appResponse.contains("error")) return appResponse;
    const QJsonValue value = appResponse.value("result");
    QJsonObject content;
    if (value.toObject().value("mimeType").toString() == "image/png") {
        content = QJsonObject{{"type", "image"},
            {"data", value.toObject().value("data")}, {"mimeType", "image/png"}};
    } else {
        content = QJsonObject{{"type", "text"},
            {"text", QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact))}};
    }
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{
        {"content", QJsonArray{content}}, {"structuredContent", value}}}};
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString socketName = qEnvironmentVariable("PARAMETRICCAD_AUTOMATION_SOCKET", "parametriccad-automation");
    QTextStream input(stdin);
    QTextStream output(stdout);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonDocument request = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        QJsonObject response;
        if (parseError.error != QJsonParseError::NoError || !request.isObject())
            response = errorResponse(QJsonValue(), -32700, "Invalid JSON");
        else response = handle(request.object(), socketName);
        if (!response.isEmpty()) { output << QJsonDocument(response).toJson(QJsonDocument::Compact) << '\n'; output.flush(); }
    }
    return 0;
}
