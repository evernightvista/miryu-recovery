#include "snapshotinfo.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QMetaType>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <functional>
#include <unistd.h>

namespace
{
const QString serviceName = QStringLiteral("org.miryugaming.recovery.helper");
const QString actionId = QStringLiteral("org.miryugaming.recovery");
constexpr uint polkitAllowUserInteraction = 0x1;

bool runCommand(const QString &program, const QStringList &arguments, QString *output = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    // btrfs-assistant selects its UI from these variables; the helper must
    // always use its command line interface, regardless of the caller's session.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("DISPLAY"));
    environment.remove(QStringLiteral("WAYLAND_DISPLAY"));
    process.setProcessEnvironment(environment);

    process.start(program, arguments);
    if (!process.waitForStarted(10000)) {
        return false;
    }
    if (!process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished();
        return false;
    }

    const QString commandOutput = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (output) {
        *output = commandOutput;
    } else if (!commandOutput.isEmpty()) {
        qInfo().noquote() << commandOutput;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

struct RollbackResult
{
    bool ok = false;
    QString error;
};

RollbackResult performRollback()
{
    const auto failure = [](const QString &message) {
        RollbackResult result;
        result.error = message;
        return result;
    };

    const SnapshotInfo snapshot = SnapshotInfo::current();
    if (!snapshot.isSnapshot()) {
        return failure(QStringLiteral("The running system is not a supported Btrfs snapshot"));
    }

    QString subvolume = snapshot.subvolume;
    while (subvolume.startsWith(QLatin1Char('/'))) {
        subvolume.remove(0, 1);
    }

    // btrfs-assistant --restore takes the one-based row number from --list,
    // not the snapper snapshot number embedded in the subvolume path.
    QString output;
    if (!runCommand(QStringLiteral("btrfs-assistant"), {QStringLiteral("--list")}, &output)) {
        return failure(output.isEmpty() ? QStringLiteral("btrfs-assistant --list failed")
                                       : QStringLiteral("btrfs-assistant --list failed:\n%1").arg(output));
    }

    QString index;
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        // Columns: index, target, snapper number, time, type, subvolume, UUID.
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 6 || fields.at(5).trimmed() != subvolume) {
            continue;
        }

        bool valid = false;
        const int candidate = fields.constFirst().trimmed().toInt(&valid);
        if (valid && candidate > 0) {
            index = QString::number(candidate);
            break;
        }
    }

    if (index.isEmpty()) {
        return failure(QStringLiteral("The booted snapshot '%1' is not listed by btrfs-assistant").arg(subvolume));
    }

    qInfo().noquote() << QStringLiteral("Restoring %1 with btrfs-assistant --restore %2").arg(subvolume, index);
    output.clear();
    if (!runCommand(QStringLiteral("btrfs-assistant"), {QStringLiteral("--restore"), index}, &output)) {
        return failure(output.isEmpty() ? QStringLiteral("btrfs-assistant --restore %1 failed").arg(index)
                                       : QStringLiteral("btrfs-assistant --restore %1 failed:\n%2").arg(index, output));
    }
    if (!output.isEmpty()) {
        qInfo().noquote() << output;
    }

    RollbackResult result;
    result.ok = true;
    return result;
}
} // namespace

class RecoveryService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.miryugaming.recovery")

public:
    explicit RecoveryService(const QDBusConnection &connection, QObject *parent = nullptr)
        : QObject(parent)
        , m_connection(connection)
    {
    }

public Q_SLOTS:
    void Rollback();

private:
    void authorize(const QString &busName, std::function<void(bool)> callback);

    QDBusConnection m_connection;
};

// Asks polkit for the custom org.miryugaming.recovery action, so the
// authentication dialog shows the action's own localized message.
void RecoveryService::authorize(const QString &busName, std::function<void(bool)> callback)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PolicyKit1"),
                                                       QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
                                                       QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
                                                       QStringLiteral("CheckAuthorization"));

    // Subject (sa{sv}): system-bus-name
    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("system-bus-name") << QVariantMap{{QStringLiteral("name"), busName}};
    subject.endStructure();

    // Details a{ss}
    QDBusArgument details;
    details.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QString>());
    details.endMap();

    call << QVariant::fromValue(subject) << actionId << QVariant::fromValue(details) << polkitAllowUserInteraction << QString();

    // Long enough for the user to enter a password and for the rollback to run.
    auto *watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(call, 600000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [callback = std::move(callback)](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        const QDBusMessage reply = watcher->reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qWarning().noquote() << "polkit check failed:" << reply.errorMessage();
            callback(false);
            return;
        }

        // Result (bba{ss}): is_authorized, is_challenge, details
        bool authorized = false;
        bool challenge = false;
        QMap<QString, QString> resultDetails;
        const QDBusArgument result = reply.arguments().value(0).value<QDBusArgument>();
        result.beginStructure();
        result >> authorized >> challenge >> resultDetails;
        result.endStructure();
        Q_UNUSED(challenge);
        callback(authorized);
    });
}

void RecoveryService::Rollback()
{
    if (!calledFromDBus()) {
        return;
    }

    const QString sender = message().service();
    const QDBusMessage call = message();
    setDelayedReply(true);

    authorize(sender, [this, call](bool authorized) {
        QDBusMessage reply;
        if (!authorized) {
            reply = call.createErrorReply(QStringLiteral("org.miryugaming.recovery.Error.AuthorizationDenied"),
                                          QStringLiteral("Authentication failed or was cancelled."));
        } else {
            const RollbackResult rollback = performRollback();
            if (rollback.ok) {
                qInfo().noquote() << QStringLiteral("Snapshot restored successfully with btrfs-assistant");
                reply = call.createReply();
            } else {
                qCritical().noquote() << rollback.error;
                reply = call.createErrorReply(QStringLiteral("org.miryugaming.recovery.Error.RollbackFailed"), rollback.error);
            }
        }

        if (!m_connection.send(reply)) {
            qCritical().noquote() << "Could not send the rollback D-Bus reply";
        }

        // The helper is D-Bus activated; leave the bus again once the reply is out.
        QTimer::singleShot(2000, QCoreApplication::instance(), [] {
            QCoreApplication::quit();
        });
    });
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (geteuid() != 0) {
        qCritical() << "The recovery helper must run as root";
        return 1;
    }

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCritical() << "Could not connect to the system bus";
        return 1;
    }
    if (!bus.registerService(serviceName)) {
        qCritical().noquote() << QStringLiteral("Could not register %1: %2").arg(serviceName, bus.lastError().message());
        return 1;
    }

    RecoveryService service(bus);
    if (!bus.registerObject(QStringLiteral("/Recovery"), &service, QDBusConnection::ExportAllSlots)) {
        qCritical() << "Could not export the recovery interface";
        return 1;
    }

    return app.exec();
}

#include "helper.moc"
