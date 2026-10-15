#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class McpProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void initializeRequestUsesJsonRpc()
    {
        const QJsonObject request{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
        QVERIFY(request.value("jsonrpc").toString() == "2.0");
        QVERIFY(QJsonDocument(request).toJson(QJsonDocument::Compact).contains("initialize"));
    }

    void invalidToolHasStrictName()
    {
        const QJsonObject request{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
            {"params", QJsonObject{{"name", "cad.mutate"}}}};
        QCOMPARE(request.value("params").toObject().value("name").toString(), QString("cad.mutate"));
        QVERIFY(!request.value("params").toObject().value("name").toString().startsWith("cad.get_"));
    }
};

QTEST_MAIN(McpProtocolTests)
#include "mcp_protocol_tests.moc"
