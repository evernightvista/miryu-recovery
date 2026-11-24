#include "recoverynotifier.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>

using namespace std::chrono_literals;

namespace
{
const QString notificationService = QStringLiteral("org.freedesktop.Notifications");
const QString notificationPath = QStringLiteral("/org/freedesktop/Notifications");
const QString notificationInterface = QStringLiteral("org.freedesktop.Notifications");
const QString helperService = QStringLiteral("org.miryugaming.recovery.helper");
}

RecoveryNotifier::RecoveryNotifier(const SnapshotInfo &snapshot, QObject *parent)
    : QObject(parent)
    , m_snapshot(snapshot)
{
    m_notificationWatcher.setConnection(QDBusConnection::sessionBus());
    m_notificationWatcher.addWatchedService(notificationService);
    m_notificationWatcher.setWatchMode(QDBusServiceWatcher::WatchForRegistration);
    connect(&m_notificationWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString &service) {
        if (service == notificationService) {
            notifyWhenReady();
        }
    });

    QDBusConnection::sessionBus().connect(notificationService,
                                          notificationPath,
                                          notificationInterface,
                                          QStringLiteral("ActionInvoked"),
                                          this,
                                          SLOT(actionInvoked(uint, QString)));

    m_retryTimer.setInterval(2s);
    connect(&m_retryTimer, &QTimer::timeout, this, &RecoveryNotifier::notifyWhenReady);

    notifyWhenReady();
}

void RecoveryNotifier::notifyWhenReady()
{
    if (m_busy || m_notificationId) {
        m_retryTimer.stop();
        return;
    }

    QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface();
    if (interface && interface->isServiceRegistered(notificationService)) {
        m_retryTimer.stop();
        showBootNotification();
    } else if (!m_retryTimer.isActive()) {
        m_retryTimer.start();
    }
}

// The notification is sent straight to org.freedesktop.Notifications, the same
// protocol KNotification uses internally. No desktop-entry or x-kde-appname
// hint is sent, so Plasma does not consider the popup configurable and hides
// its "Configure" button. No "resident" hint is sent either, which hides the
// "Minimize" button. expire_timeout 0 keeps the popup until the user decides.
void RecoveryNotifier::sendNotification(const QString &title, const QString &text, const QStringList &actions)
{
    QDBusMessage message = QDBusMessage::createMethodCall(notificationService,
                                                          notificationPath,
                                                          notificationInterface,
                                                          QStringLiteral("Notify"));

    // The notification specification defines urgency 2 as critical. Plasma
    // draws an orange strip beside the title and body for critical notifications.
    const QVariantMap hints{{QStringLiteral("urgency"), QVariant::fromValue<quint8>(2)}};

    message << i18n("Miryu Recovery") // app_name
            << m_notificationId // replaces_id
            << QStringLiteral("backup") // app_icon
            << title // summary
            << text // body
            << actions // actions: key, label, ...
            << hints
            << 0; // expire_timeout: persistent

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_notificationId = reply.arguments().constFirst().toUInt();
        } else {
            qWarning().noquote() << "Could not show the notification:" << reply.errorMessage();
        }
    });
}

void RecoveryNotifier::closeNotification()
{
    if (!m_notificationId) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(notificationService,
                                                          notificationPath,
                                                          notificationInterface,
                                                          QStringLiteral("CloseNotification"));
    message << m_notificationId;
    m_notificationId = 0;
    QDBusConnection::sessionBus().asyncCall(message);
}

void RecoveryNotifier::actionInvoked(uint id, const QString &actionKey)
{
    if (id != m_notificationId) {
        return;
    }

    if (actionKey == QLatin1String("rollback")) {
        rollback();
    } else if (actionKey == QLatin1String("keep")) {
        closeNotification();
        QCoreApplication::quit();
    } else if (actionKey == QLatin1String("restart")) {
        reboot();
    }
}

void RecoveryNotifier::showBootNotification()
{
    sendNotification(i18n("Btrfs snapshot started successfully"),
                     i18n("You are running Btrfs snapshot #%1. To make this snapshot permanent, click 'Rollback Snapshot'. Otherwise, keep the snapshot for this boot.",
                          m_snapshot.number),
                     {QStringLiteral("rollback"),
                      i18n("Rollback Snapshot"),
                      QStringLiteral("keep"),
                      i18n("Keep Snapshot")});
}

void RecoveryNotifier::rollback()
{
    if (m_busy) {
        return;
    }

    m_busy = true;
    closeNotification();

    // The helper is a root D-Bus service guarded by the polkit action
    // org.miryugaming.recovery, so the authentication dialog carries the
    // action's own title instead of the generic pkexec prompt.
    const QDBusMessage message = QDBusMessage::createMethodCall(helperService,
                                                                QStringLiteral("/Recovery"),
                                                                QStringLiteral("org.miryugaming.recovery"),
                                                                QStringLiteral("Rollback"));
    m_callWatcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 600000), this);
    connect(m_callWatcher, &QDBusPendingCallWatcher::finished, this, &RecoveryNotifier::rollbackFinished);
}

void RecoveryNotifier::rollbackFinished(QDBusPendingCallWatcher *watcher)
{
    m_callWatcher = nullptr;
    watcher->deleteLater();

    if (!m_busy) {
        return;
    }
    m_busy = false;

    const QDBusMessage reply = watcher->reply();
    if (!watcher->isError()) {
        showRollbackCompleted();
        return;
    }

    if (reply.errorName() == QStringLiteral("org.miryugaming.recovery.Error.AuthorizationDenied")) {
        showRollbackFailed(i18n("Authentication failed or was cancelled."));
        return;
    }

    qWarning().noquote() << reply.errorName() << reply.errorMessage();
    showRollbackFailed(reply.errorMessage());
}

void RecoveryNotifier::showRollbackCompleted()
{
    sendNotification(i18n("Snapshot rollback completed"),
                     i18n("Restart your computer now to finish rolling back the snapshot."),
                     {QStringLiteral("restart"), i18n("Restart Now")});
}

void RecoveryNotifier::showRollbackFailed(const QString &detail)
{
    closeNotification();

    QString text = i18n("The snapshot could not be restored. Check the system state before restarting.");
    if (!detail.isEmpty()) {
        const QStringList lines = detail.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            text += QStringLiteral("<br/><br/>%1").arg(lines.constLast().toHtmlEscaped());
        }
    }

    sendNotification(i18n("Snapshot rollback failed"), text, {});
}

void RecoveryNotifier::reboot()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                          QStringLiteral("/org/freedesktop/login1"),
                                                          QStringLiteral("org.freedesktop.login1.Manager"),
                                                          QStringLiteral("Reboot"));
    message << true;
    QDBusConnection::systemBus().asyncCall(message);
}
