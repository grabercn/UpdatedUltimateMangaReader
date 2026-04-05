#ifndef SUSPENDMANAGER_H
#define SUSPENDMANAGER_H

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include <QObject>

#include "staticsettings.h"

class SuspendManager : public QObject
{
    Q_OBJECT

public:
    explicit SuspendManager(NetworkManager *networkManager, QObject *parent = nullptr);

    bool suspend();
    bool resume();

    void setCpuGovernor(const QString &governor);
    void powerOff();

    bool sleeping;

signals:
    void suspending();
    void resuming();

private:
    NetworkManager *networkManager;
    QTimer timer;

    bool suspendInternal();
};

#endif  // SUSPENDMANAGER_H
