#include "recoverynotifier.h"
#include "snapshotinfo.h"

#include <KDBusService>
#include <KLocalizedString>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("miryu-recovery");
    QApplication::setApplicationName(QStringLiteral("miryu-recovery"));
    QApplication::setApplicationDisplayName(i18n("Miryu Recovery"));
    QApplication::setOrganizationDomain(QStringLiteral("miryu.org"));
    QApplication::setDesktopFileName(QStringLiteral("miryu-recovery"));
    QApplication::setQuitOnLastWindowClosed(false);

    KDBusService service(KDBusService::Unique);

    const SnapshotInfo snapshot = SnapshotInfo::current();
    if (!snapshot.isSnapshot()) {
        return 0;
    }

    RecoveryNotifier notifier(snapshot);
    return app.exec();
}
