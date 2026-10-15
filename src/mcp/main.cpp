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
    const QJsonObject tools = QJsonObject{
        {"cad.get_model_tree", QJsonObject{{"description", "Inspect the parametric model tree"}, {"inputSchema", empty}}},
        {"cad.get_feature", QJsonObject{{"description", "Inspect one parametric feature"}, {"inputSchema", feature}}},
        {"cad.inspect_shape", QJsonObject{{"description", "Inspect OCCT shape validity and metrics"}, {"inputSchema", feature}}},
        {"cad.get_selection", QJsonObject{{"description", "Read the current viewer selection"}, {"inputSchema", empty}}},
        {"cad.get_view_state", QJsonObject{{"description", "Read viewer camera and display state"}, {"inputSchema", empty}}},
        {"cad.screenshot", QJsonObject{{"description", "Capture the current viewer as PNG"}, {"inputSchema", empty}}},
        {"cad.get_application_status", QJsonObject{{"description", "Read application and automation status"}, {"inputSchema", empty}}}
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
