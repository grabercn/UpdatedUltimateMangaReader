#include "settings.h"

#include "staticsettings.h"

Settings::Settings()
    : lightValue(0),
      comflightValue(0),
      hideErrorMessages(false),
      tabAdvance(Right),
      swipeAdvance(Left),
      buttonAdvance(Down),
      mangaOrder(OrderByPopularity),
      doublePageMode(DoublePage90CW),
      trimPages(true),
      manhwaMode(true),
      ditheringMode(SWDithering),
      colorMode(false),  // greyscale by default for fast page loading
      iaGeneralBooksEnabled(false),
      debugScreenshots(false),
      usbNetworkMode(false),
      ftpServerEnabled(false),
      preloadEnabled(true),
      preloadPages(3),
      preloadChapters(2),
      autoBootEnabled(false),
      offlineMode(false),
      autoSuspendMinutes(15),
      wifiAutoDisconnect(true),
      timer()
{
    QObject::connect(&timer, &QTimer::timeout, [this]() { this->serialize(); });
}

void Settings::deserialize()
{
    QFile file(QString(CONF.cacheDir) + "/settings.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;

    QDataStream in(&file);
    in >> *this;
    file.close();
}

void Settings::scheduleSerialize()
{
    timer.start(1000);
}

void Settings::writeKfmonAutoboot(bool enabled)
{
#ifdef KOBO
    // Detect launcher: check for KFMon config dir and NickelMenu config dir
    bool hasKfmon = QDir("/mnt/onboard/.adds/kfmon/config").exists();
    bool hasNickelMenu = QDir("/mnt/onboard/.adds/nm").exists();

    // KFMon config
    if (hasKfmon)
    {
        QString kfmonPath = "/mnt/onboard/.adds/kfmon/config/UltimateMangaReader.ini";
        QFile file(kfmonPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&file);
            out << "# UltimateMangaReader KFMon config\n"
                << "[watch]\n"
                << "filename = /mnt/onboard/.adds/UltimateMangaReader/UltimateMangaReader\n"
                << "label = Ultimate Manga Reader\n"
                << "on_boot = " << (enabled ? "true" : "false") << "\n"
                << "on_failure = nickel\n"  // fall back to Nickel if app crashes
                << "block_spawns = false\n";
            file.close();
            qDebug() << "KFMon autoboot config written:" << enabled;
        }
    }

    // NickelMenu config - add/update a menu entry and optionally set as startup
    if (hasNickelMenu)
    {
        QString nmPath = "/mnt/onboard/.adds/nm/umr";
        QFile file(nmPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&file);

            // Always add a menu entry to launch the app via shell script
            out << "menu_item :main :Ultimate Manga Reader :cmd_spawn "
                << ":quiet:/mnt/onboard/.adds/UltimateMangaReader/ultimatemangareader.sh\n";

            // Add startup entry if autoboot enabled
            if (enabled)
            {
                out << "menu_item :startup :Ultimate Manga Reader :cmd_spawn "
                    << ":quiet:/mnt/onboard/.adds/UltimateMangaReader/ultimatemangareader.sh\n";
            }

            file.close();
            qDebug() << "NickelMenu config written:" << enabled;
        }
    }

    if (!hasKfmon && !hasNickelMenu)
        qDebug() << "No launcher detected (neither KFMon nor NickelMenu found)";
#else
    Q_UNUSED(enabled);
#endif
}

void Settings::serialize()
{
    timer.stop();
    QFile file(QString(CONF.cacheDir) + "/settings.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;

    QDataStream out(&file);
    out << *this;
    file.close();
}

QDataStream &operator<<(QDataStream &str, const Settings &m)
{
    // Original fields (don't reorder - backwards compat)
    str << m.lightValue << m.comflightValue << m.hideErrorMessages << m.tabAdvance << m.swipeAdvance
        << m.buttonAdvance << m.mangaOrder << m.doublePageMode << m.trimPages << m.manhwaMode
        << m.enabledMangaSources << m.ditheringMode << m.aniListToken;

    // Extended fields (added in v2.x)
    str << m.colorMode << m.preloadEnabled << m.preloadPages << m.preloadChapters
        << m.autoBootEnabled << m.offlineMode << m.autoSuspendMinutes << m.wifiAutoDisconnect
        << m.iaGeneralBooksEnabled << m.debugScreenshots << m.usbNetworkMode
        << m.ftpServerEnabled;

    return str;
}

QDataStream &operator>>(QDataStream &str, Settings &m)
{
    try
    {
        m.enabledMangaSources.clear();
        str >> m.lightValue >> m.comflightValue >> m.hideErrorMessages >> m.tabAdvance >> m.swipeAdvance >>
            m.buttonAdvance >> m.mangaOrder >> m.doublePageMode >> m.trimPages >> m.manhwaMode >>
            m.enabledMangaSources >> m.ditheringMode;

        if (str.status() != QDataStream::Ok)
        {
            qDebug() << "Settings file partially corrupt, using defaults for remaining fields";
            return str;
        }

        if (!str.atEnd())
            str >> m.aniListToken;

        // Extended fields (backwards compat - only read if present)
        if (!str.atEnd()) str >> m.colorMode;
        if (!str.atEnd()) str >> m.preloadEnabled;
        if (!str.atEnd()) str >> m.preloadPages;
        if (!str.atEnd()) str >> m.preloadChapters;
        if (!str.atEnd()) str >> m.autoBootEnabled;
        if (!str.atEnd()) str >> m.offlineMode;
        if (!str.atEnd()) str >> m.autoSuspendMinutes;
        if (!str.atEnd()) str >> m.wifiAutoDisconnect;
        if (!str.atEnd()) str >> m.iaGeneralBooksEnabled;
        if (!str.atEnd()) str >> m.debugScreenshots;
        if (!str.atEnd()) str >> m.usbNetworkMode;
        if (!str.atEnd()) str >> m.ftpServerEnabled;
    }
    catch (...)
    {
        qDebug() << "Error reading settings, using defaults";
    }

    return str;
}
