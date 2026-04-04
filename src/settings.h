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
    int trimLevel;  // 0=off, 1=light, 2=normal(default), 3=aggressive, 4=maximum
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
    bool ftpServerEnabled;   // FTP file server over WiFi
    bool downloadWhileSleeping;  // Keep WiFi on during sleep for downloads

    // Bedtime warmth (like Nickel's bedtime feature)
    bool bedtimeEnabled;
    int bedtimeStartMinutes;  // minutes from midnight (e.g. 22*60 = 22:00)
    int bedtimeEndMinutes;    // minutes from midnight (e.g. 7*60 = 07:00)

    QMap<QString, bool> enabledMangaSources;

    static void writeKfmonAutoboot(bool enabled);

private:
    QTimer timer;
};

QDataStream &operator<<(QDataStream &str, const Settings &m);

QDataStream &operator>>(QDataStream &str, Settings &m);

#endif  // SETTINGS_H
