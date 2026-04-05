#include "suspendmanager.h"

SuspendManager::SuspendManager(NetworkManager *networkManager, QObject *parent)
    : QObject(parent), sleeping(false), networkManager(networkManager), timer()
{
    // No periodic re-suspend timer - single suspend-to-RAM is sufficient
    // The old 5-minute timer was waking the device from RAM sleep repeatedly,
    // causing massive battery drain (CPU + WiFi module power up each cycle)
}

bool SuspendManager::suspend()
{
    if (sleeping)
        return true;

    sleeping = true;  // Set before processEvents to prevent reentrant suspend

    emit suspending();
    qApp->processEvents();

    qDebug() << QTime::currentTime().toString("hh:mm:ss") << "Going to sleep...";

    // Give e-ink display time to render the screensaver
    QThread::msleep(500);
    qApp->processEvents();

    return suspendInternal();
}

bool SuspendManager::suspendInternal()
{
    if (networkManager->connected)
        networkManager->disconnectWifi();

#ifdef KOBO
    setCpuGovernor("powersave");

    int handleSE = open("/sys/power/state-extended", O_RDWR);
    if (handleSE >= 0)
    {
        write(handleSE, "1\n", 2);
        close(handleSE);
    }

    QThread::sleep(2);
    QProcess::execute("sync", {});

    int handleS = open("/sys/power/state", O_RDWR);
    if (handleS >= 0)
    {
        write(handleS, "mem\n", 4);
        close(handleS);
    }
#endif

    sleeping = true;

    return true;
}

bool SuspendManager::resume()
{
    if (!sleeping)
        return true;

    qDebug() << QTime::currentTime().toString("hh:mm:ss") << "Waking up...";

#ifdef KOBO
    setCpuGovernor("ondemand");

    int handleSE = open("/sys/power/state-extended", O_RDWR);
    if (handleSE >= 0)
    {
        write(handleSE, "0\n", 2);
        close(handleSE);
    }

    QThread::msleep(100);

    int handleNC = open("/sys/devices/virtual/input/input1/neocmd", O_RDWR);
    if (handleNC >= 0)
    {
        write(handleNC, "a\n", 2);
        close(handleNC);
    }
#endif

    timer.stop();
    sleeping = false;

    emit resuming();

    return true;
}

void SuspendManager::setCpuGovernor(const QString &governor)
{
#ifdef KOBO
    int fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_WRONLY);
    if (fd >= 0)
    {
        auto bytes = governor.toUtf8();
        write(fd, bytes.data(), bytes.size());
        close(fd);
    }
#else
    Q_UNUSED(governor);
#endif
}

void SuspendManager::powerOff()
{
#ifdef KOBO
    QProcess::execute("sync", {});
    QProcess::execute("poweroff", {});
#endif
}
