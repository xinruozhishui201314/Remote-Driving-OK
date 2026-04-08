#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "core/tracing.h"
#include "infrastructure/itransportmanager.h"
#include "services/vehiclecontrolservice.h"
#include "vehiclemanager.h"

namespace {

class RecordingTransport final : public ITransportManager {
public:
    QByteArray lastControlJson;

    explicit RecordingTransport(QObject *parent = nullptr)
        : ITransportManager(parent)
    {}

    bool initialize(const TransportConfig &) override { return true; }
    void shutdown() override {}
    void connectAsync(const EndpointInfo &) override {}
    void disconnect() override {}

    SendResult send(TransportChannel channel, const uint8_t *data, size_t len,
                    SendFlags /*flags*/) override
    {
        if (channel == TransportChannel::CONTROL_CRITICAL) {
            lastControlJson =
                QByteArray(reinterpret_cast<const char *>(data), static_cast<int>(len));
        }
        return SendResult{true, QString()};
    }

    void registerReceiver(
        TransportChannel /*channel*/,
        std::function<void(const uint8_t *, size_t, const PacketMetadata &)> /*callback*/) override
    {}

    NetworkQuality getNetworkQuality() const override { return {}; }

    ChannelStats getChannelStats(TransportChannel /*ch*/) const override { return {}; }

    ConnectionState connectionState() const override { return ConnectionState::CONNECTED; }
};

} // namespace

class TestVehicleControlService : public QObject {
    Q_OBJECT

private slots:
    void sendUiCommand_buildsEnvelopeWithTraceAndSession();
};

void TestVehicleControlService::sendUiCommand_buildsEnvelopeWithTraceAndSession()
{
    VehicleManager vm;
    vm.setCurrentVin(QStringLiteral("VIN_UNIT_TEST"));

    RecordingTransport transport;
    VehicleControlService vcs(nullptr, &vm, nullptr);
    vcs.setTransport(&transport);
    QVERIFY(vcs.initialize());

    Tracing::instance().setCurrentTraceId(QStringLiteral("trace-unit-test-aaaaaaaa"));
    vcs.setSessionCredentials(QStringLiteral("VIN_UNIT_TEST"), QStringLiteral("session-unit-xyz"),
                              QStringLiteral("token"));

    QVariantMap payload;
    payload.insert(QStringLiteral("value"), 3);
    vcs.sendUiCommand(QStringLiteral("gear"), payload);

    QVERIFY(!transport.lastControlJson.isEmpty());
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(transport.lastControlJson, &err);
    QVERIFY2(err.error == QJsonParseError::NoError, err.errorString().toUtf8().constData());
    QVERIFY(doc.isObject());
    const QJsonObject o = doc.object();

    QCOMPARE(o.value(QStringLiteral("type")).toString(), QStringLiteral("gear"));
    QCOMPARE(o.value(QStringLiteral("schemaVersion")).toString(), QStringLiteral("1.0"));
    QCOMPARE(o.value(QStringLiteral("vin")).toString(), QStringLiteral("VIN_UNIT_TEST"));
    QCOMPARE(o.value(QStringLiteral("sessionId")).toString(), QStringLiteral("session-unit-xyz"));
    QCOMPARE(o.value(QStringLiteral("trace_id")).toString(),
             QStringLiteral("trace-unit-test-aaaaaaaa"));
    QVERIFY(o.contains(QStringLiteral("timestampMs")));
    QVERIFY(o.contains(QStringLiteral("seq")));

    QVERIFY(o.value(QStringLiteral("payload")).isObject());
    QCOMPARE(o.value(QStringLiteral("payload")).toObject().value(QStringLiteral("value")).toInt(),
             3);
}

QTEST_MAIN(TestVehicleControlService)
#include "test_vehiclecontrolservice.moc"
