#ifndef RECOVERYNOTIFIER_H
#define RECOVERYNOTIFIER_H

#include "snapshotinfo.h"

#include <QDBusServiceWatcher>
#include <QTimer>

class QDBusPendingCallWatcher;

class RecoveryNotifier : public QObject
{
    Q_OBJECT

public:
    explicit RecoveryNotifier(const SnapshotInfo &snapshot, QObject *parent = nullptr);

private Q_SLOTS:
    void actionInvoked(uint id, const QString &actionKey);

private:
    void notifyWhenReady();
    void showBootNotification();
    void rollback();
    void rollbackFinished(QDBusPendingCallWatcher *watcher);
    void showRollbackCompleted();
    void showRollbackFailed(const QString &detail = QString());
    void sendNotification(const QString &title, const QString &text, const QStringList &actions);
    void closeNotification();
    void reboot();

    SnapshotInfo m_snapshot;
    QDBusServiceWatcher m_notificationWatcher;
    QTimer m_retryTimer;
    QDBusPendingCallWatcher *m_callWatcher = nullptr;
    uint m_notificationId = 0;
    bool m_busy = false;
};

#endif
