#include "snapshotinfo.h"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>

namespace
{
QString rootSource()
{
    QProcess process;
    process.start(QStringLiteral("findmnt"), {QStringLiteral("--noheadings"), QStringLiteral("--output"), QStringLiteral("SOURCE"), QStringLiteral("/")});
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        return {};
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

QString subvolumeFromSource(const QString &source)
{
    const qsizetype openingBracket = source.lastIndexOf(QLatin1Char('['));
    if (openingBracket < 0 || !source.endsWith(QLatin1Char(']'))) {
        return {};
    }
    return source.mid(openingBracket + 1, source.size() - openingBracket - 2);
}

void applySubvolume(SnapshotInfo &info, QString subvolume)
{
    while (subvolume.startsWith(QLatin1Char('/'))) {
        subvolume.remove(0, 1);
    }

    static const QRegularExpression snapshotPattern(QStringLiteral("^(?:(.+)/)?\\.snapshots/([1-9][0-9]*)/snapshot$"));
    const QRegularExpressionMatch match = snapshotPattern.match(subvolume);
    if (!match.hasMatch()) {
        return;
    }

    info.subvolume = subvolume;
    info.rootSubvolume = match.captured(1);
    info.number = match.captured(2).toInt();
}
}

SnapshotInfo SnapshotInfo::current()
{
    SnapshotInfo info;
    QString commandLine;
    QFile file(QStringLiteral("/proc/cmdline"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        commandLine = QString::fromLocal8Bit(file.readAll()).trimmed();
    }

    const QStringList arguments = commandLine.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QString rootFlags;
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("systemd.volatile=overlay")) {
            info.volatileOverlay = true;
        } else if (argument.startsWith(QStringLiteral("root="))) {
            info.device = argument.mid(5);
        } else if (argument.startsWith(QStringLiteral("rootflags="))) {
            rootFlags = argument.mid(10);
        }
    }

    const QStringList flags = rootFlags.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &flag : flags) {
        if (flag.startsWith(QStringLiteral("subvol="))) {
            applySubvolume(info, flag.mid(7));
            break;
        }
    }

    const QString source = rootSource();
    SnapshotInfo mounted;
    applySubvolume(mounted, subvolumeFromSource(source));
    if (info.volatileOverlay) {
        if (!info.isSnapshot()) {
            applySubvolume(info, subvolumeFromSource(source));
        }
    } else {
        info.number = mounted.number;
        info.subvolume = mounted.subvolume;
        info.rootSubvolume = mounted.rootSubvolume;
        info.device = source.section(QLatin1Char('['), 0, 0);
    }
    if (info.device.isEmpty()) {
        info.device = source.section(QLatin1Char('['), 0, 0);
    }

    return info;
}
