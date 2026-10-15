#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

#ifndef PARAMETRICCAD_APP_PATH
#error PARAMETRICCAD_APP_PATH must be defined by CMake
#endif
#ifndef PARAMETRICCAD_MCP_PATH
#error PARAMETRICCAD_MCP_PATH must be defined by CMake
#endif

class McpModelingIntegrationTests final : public QObject
{
    Q_OBJECT

private:
    QProcess application_;
    QTemporaryDir temporaryDirectory_;
    QString socketName_;

    QJsonObject call(const QString& name, const QJsonObject& arguments = {})
    {
        QProcess mcp;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PARAMETRICCAD_AUTOMATION_SOCKET", socketName_);
        mcp.setProcessEnvironment(environment);
        mcp.setProgram(QStringLiteral(PARAMETRICCAD_MCP_PATH));
        mcp.start();
        if (!mcp.waitForStarted(5000)) {
            qWarning() << "MCP failed to start:" << mcp.errorString();
            return {};
        }
        const QJsonObject request{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", QJsonObject{{"name", name}, {"arguments", arguments}}}};
        mcp.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        mcp.closeWriteChannel();
        if (!mcp.waitForFinished(15000)) {
            qWarning() << "MCP did not finish:" << mcp.errorString();
            mcp.kill();
            return {};
        }
        const auto lines = mcp.readAllStandardOutput().split('\n');
        for (const auto& line : lines) {
            if (line.trimmed().isEmpty()) continue;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (error.error == QJsonParseError::NoError && document.isObject())
                return document.object();
        }
        qWarning() << "MCP output:" << mcp.readAllStandardError();
        return {};
    }

    QJsonObject callTool(const QString& name, const QJsonObject& arguments = {})
    {
        const auto response = call(name, arguments);
        if (!response.contains("result")) {
            qWarning() << "MCP tool failed:" << name
                       << QJsonDocument(response).toJson(QJsonDocument::Compact);
            return {};
        }
        return response.value("result").toObject().value("structuredContent").toObject();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(temporaryDirectory_.isValid());
        socketName_ = temporaryDirectory_.filePath("parametriccad-mcp-2a");
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "xcb");
        environment.insert("PARAMETRICCAD_ENABLE_AUTOMATION", "1");
        environment.insert("PARAMETRICCAD_AUTOMATION_SOCKET", socketName_);
        application_.setProcessEnvironment(environment);
        application_.setProgram(QStringLiteral(PARAMETRICCAD_APP_PATH));
        application_.setArguments({"--enable-automation"});
        application_.start();
        QVERIFY2(application_.waitForStarted(10000), qPrintable(application_.errorString()));

        bool connected = false;
        for (int attempt = 0; attempt < 30 && !connected; ++attempt) {
            QLocalSocket socket;
            socket.connectToServer(socketName_);
            connected = socket.waitForConnected(500);
            if (!connected) QTest::qWait(100);
        }
        QVERIFY2(connected, qPrintable(QString("Automation socket did not become ready: %1; app output: %2")
            .arg(socketName_, QString::fromUtf8(application_.readAllStandardError()))));
        QVERIFY2(application_.state() != QProcess::NotRunning,
                 qPrintable(QString::fromUtf8(application_.readAllStandardError())));
    }

    void modelingWorkflowThroughMcp()
    {
        QVERIFY(callTool("cad.get_application_status").value("automationEnabled").toBool());
        QCOMPARE(callTool("cad.new_project").value("featureCount").toInt(), 0);

        const auto sketch = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}});
        const QString sketchId = sketch.value("featureId").toString();
        QVERIFY(!sketchId.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", sketchId}, {"x", 0.0},
            {"y", 0.0}, {"width", 120.0}, {"height", 80.0}, {"units", "mm"}});

        const auto extrude = callTool("cad.extrude", QJsonObject{{"sketchId", sketchId},
            {"distance", 30.0}, {"units", "mm"}});
        const QString extrudeId = extrude.value("featureId").toString();
        QVERIFY(!extrudeId.isEmpty());

        const auto extrudeFeature = callTool("cad.get_feature", QJsonObject{{"featureId", extrudeId}});
        const auto faces = extrudeFeature.value("topology").toObject().value("faces").toArray();
        QVERIFY(!faces.isEmpty());
        QJsonObject faceReference;
        for (const auto& candidate : faces) {
            if (candidate.toObject().value("semanticId").isString()
                || candidate.toObject().value("signature").isObject()) {
                faceReference = candidate.toObject();
                break;
            }
        }
        QVERIFY(!faceReference.isEmpty());

        const auto attachedSketch = callTool("cad.create_sketch",
            QJsonObject{{"faceReference", faceReference}});
        const QString attachedSketchId = attachedSketch.value("featureId").toString();
        QVERIFY(!attachedSketchId.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", attachedSketchId}, {"x", 2.0},
            {"y", 2.0}, {"width", 116.0}, {"height", 76.0}, {"units", "mm"}});

        const auto pocket = callTool("cad.pocket", QJsonObject{{"sketchId", attachedSketchId},
            {"depth", 28.0}, {"units", "mm"}});
        const QString pocketId = pocket.value("featureId").toString();
        QVERIFY(!pocketId.isEmpty());
        const auto shape = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}});
        QVERIFY(shape.value("valid").toBool());
        QCOMPARE(shape.value("solidCount").toInt(), 1);
        QVERIFY(shape.value("volume").toDouble() > 0.0);

        const QString path = qEnvironmentVariable("PARAMETRICCAD_MCP_EXAMPLE_PATH",
            temporaryDirectory_.filePath("electronics-enclosure.pcad"));
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", path}}).value("success").toBool());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", path}}).value("success").toBool());

        const double beforeEdit = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(callTool("cad.edit_feature", QJsonObject{{"featureId", extrudeId},
            {"property", "distance"}, {"value", 32.0}, {"units", "mm"}}).value("success").toBool());
        QVERIFY(callTool("cad.recompute").value("success").toBool());
        const double afterEdit = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(afterEdit > beforeEdit);

        QVERIFY(callTool("cad.undo").value("success").toBool());
        const double afterUndo = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(afterUndo < afterEdit);
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const double afterRedo = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(std::abs(afterRedo - afterEdit) < 1.0e-6);
        QVERIFY(callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
                    .value("valid").toBool());
    }

    void invalidArgumentsReturnStructuredErrors()
    {
        const auto invalid = call("cad.add_circle", QJsonObject{{"sketchId", "missing"},
            {"x", 0.0}, {"y", 0.0}, {"radius", 2.0}});
        QVERIFY(invalid.value("error").toObject().value("code").toInt() == -32602);
        const auto missing = call("cad.get_feature", QJsonObject{{"featureId", "missing"}});
        QVERIFY(missing.value("error").toObject().value("code").toInt() == -32004);
    }

    void cleanupTestCase()
    {
        application_.terminate();
        if (!application_.waitForFinished(5000)) application_.kill();
    }
};

QTEST_MAIN(McpModelingIntegrationTests)
#include "mcp_modeling_integration_tests.moc"
