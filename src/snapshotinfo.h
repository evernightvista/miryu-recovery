#ifndef SNAPSHOTINFO_H
#define SNAPSHOTINFO_H

#include <QString>

class SnapshotInfo
{
public:
    static SnapshotInfo current();

    bool isSnapshot() const
    {
        return number > 0;
    }

    int number = 0;
    QString subvolume;
    QString rootSubvolume;
    QString device;
    bool volatileOverlay = false;
};

#endif
