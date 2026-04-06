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
      trimLevel(2),  // Normal by default
      manhwaMode(true),
      ditheringMode(SWDithering),
      colorMode(false),  // greyscale by default for fast page loading
      preloadEnabled(true),
      preloadPages(3),
      preloadChapters(2),
      autoBootEnabled(false),
      offlineMode(false),
      autoSuspendMinutes(15),
      wifiAutoDisconnect(true),
      iaGeneralBooksEnabled(false),
      debugScreenshots(false),
      usbNetworkMode(false),
      ftpServerEnabled(false),
      downloadWhileSleeping(false),
      bedtimeEnabled(false),
      bedtimeStartMinutes(22 * 60),  // 10:00 PM
      bedtimeEndMinutes(7 * 60),     // 7:00 AM
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
    bool hasKfmon = QDir("/mnt/onboard/.adds/kfmon/config").exists();
    bool hasNickelMenu = QDir("/mnt/onboard/.adds/nm").exists();

    // KFMon config: read-modify-write to preserve existing settings
    if (hasKfmon)
    {
        QString kfmonPath = "/mnt/onboard/.adds/kfmon/config/UltimateMangaReader.ini";
        QFile file(kfmonPath);

        // Read existing config if present
        QString existingContent;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            existingContent = QTextStream(&file).readAll();
            file.close();
        }

        if (!existingContent.isEmpty())
        {
            // Update only the on_boot line, preserve everything else
            QStringList lines = existingContent.split('\n');
            bool foundOnBoot = false;
            for (int i = 0; i < lines.size(); i++)
            {
                if (lines[i].trimmed().startsWith("on_boot"))
                {
                    lines[i] = QString("on_boot = %1").arg(enabled ? "true" : "false");
                    foundOnBoot = true;
                    break;
                }
            }
            if (!foundOnBoot)
                lines.append(QString("on_boot = %1").arg(enabled ? "true" : "false"));

            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream out(&file);
                out << lines.join('\n');
                file.close();
            }
        }
        else
        {
            // No existing config — create a minimal one
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream out(&file);
                out << "# UltimateMangaReader KFMon config\n"
                    << "[watch]\n"
                    << "filename = /mnt/onboard/.adds/UltimateMangaReader/UltimateMangaReader\n"
                    << "label = Ultimate Manga Reader\n"
                    << "on_boot = " << (enabled ? "true" : "false") << "\n"
                    << "on_failure = nickel\n"
                    << "block_spawns = false\n";
                file.close();
            }
        }
        qDebug() << "KFMon autoboot config updated:" << enabled;
    }

    // NickelMenu config: only manage UMR's own file, toggle startup line only
    if (hasNickelMenu)
    {
        QString nmPath = "/mnt/onboard/.adds/nm/umr";
        QFile file(nmPath);

        // Read existing NickelMenu entries for UMR
        QString existingContent;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            existingContent = QTextStream(&file).readAll();
            file.close();
        }

        // Preserve all existing lines except startup entries for UMR
        QStringList preservedLines;
        bool hasMainEntry = false;
        for (const auto &line : existingContent.split('\n'))
        {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            // Remove old startup entries (we'll re-add if needed)
            if (trimmed.contains(":startup") && trimmed.contains("Ultimate Manga Reader"))
                continue;
            preservedLines.append(line);
            if (trimmed.contains(":main") && trimmed.contains("Ultimate Manga Reader"))
                hasMainEntry = true;
        }

        // Ensure a main menu entry exists
        if (!hasMainEntry)
        {
            preservedLines.append(
                "menu_item :main :Ultimate Manga Reader :cmd_spawn "
                ":quiet:/mnt/onboard/.adds/UltimateMangaReader/ultimatemangareader.sh");
        }

        // Add startup entry only if autoboot enabled
        if (enabled)
        {
            preservedLines.append(
                "menu_item :startup :Ultimate Manga Reader :cmd_spawn "
                ":quiet:/mnt/onboard/.adds/UltimateMangaReader/ultimatemangareader.sh");
        }

        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&file);
            out << preservedLines.join('\n') << "\n";
            file.close();
        }
        qDebug() << "NickelMenu config updated:" << enabled;
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
        << m.ftpServerEnabled << m.downloadWhileSleeping << m.trimLevel
        << m.bedtimeEnabled << m.bedtimeStartMinutes << m.bedtimeEndMinutes;

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
        if (!str.atEnd()) str >> m.downloadWhileSleeping;
        if (!str.atEnd()) str >> m.trimLevel;
        if (!str.atEnd()) str >> m.bedtimeEnabled;
        if (!str.atEnd()) str >> m.bedtimeStartMinutes;
        if (!str.atEnd()) str >> m.bedtimeEndMinutes;
    }
    catch (...)
    {
        qDebug() << "Error reading settings, using defaults";
    }

    return str;
}
