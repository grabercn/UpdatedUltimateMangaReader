#ifndef SETTINGS_H
#define SETTINGS_H

#include <QDataStream>
#include <QFile>
#include <QMap>
#include <QTimer>

#include "enums.h"

class Settings
{
public:
    Settings();

    void deserialize();
    void serialize();
    void scheduleSerialize();

    int lightValue;
    int comflightValue;

    bool hideErrorMessages;

    AdvancePageGestureDirection tabAdvance;
    AdvancePageGestureDirection swipeAdvance;
    AdvancePageHWButton buttonAdvance;

    MangaOrderMethod mangaOrder;

    DoublePageMode doublePageMode;
    bool trimPages;
    bool manhwaMode;
    DitheringMode ditheringMode;
    bool colorMode;  // true = keep color images (for color e-readers or desktop)

    bool preloadEnabled;
    int preloadPages;
    int preloadChapters;
    bool autoBootEnabled;   // KFMon on_boot flag

    QString aniListToken;
    bool offlineMode;
    int autoSuspendMinutes;  // 0 = disabled
    bool wifiAutoDisconnect; // disconnect wifi on sleep
    bool iaGeneralBooksEnabled;  // show general books from Internet Archive
    bool debugScreenshots;   // periodic screenshots to cache/screenshots/
    bool usbNetworkMode;     // USB networking over cable (OTG/telnet)

    QMap<QString, bool> enabledMangaSources;

    static void writeKfmonAutoboot(bool enabled);

private:
    QTimer timer;
};

QDataStream &operator<<(QDataStream &str, const Settings &m);

QDataStream &operator>>(QDataStream &str, Settings &m);

#endif  // SETTINGS_H
