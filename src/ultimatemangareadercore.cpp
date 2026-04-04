#include "ultimatemangareadercore.h"

#include "mangahere.h"

UltimateMangaReaderCore::UltimateMangaReaderCore(QObject* parent)
    : QObject(parent),
      mangaSources(),
      activeMangaSources(),
      currentMangaSource(nullptr),
      currentManga(),
      networkManager(new NetworkManager(this)),
      mangaController(new MangaController(networkManager, this)),
      favoritesManager(new FavoritesManager(activeMangaSources, this)),
      mangaChapterDownloadManager(new MangaChapterDownloadManager(networkManager, this)),
      suspendManager(new SuspendManager(networkManager, this)),
      aniList(new AniList(networkManager, this)),
      updater(new Updater(networkManager, this)),
      settings(),
      timer(),
      autoSuspendTimer(),
      currentDay(QDate::currentDate().day())
{
    setupDirectories();
    settings.deserialize();

    mangaSources.append(QSharedPointer<AbstractMangaSource>(new MangaDex(networkManager)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(new MangaPlus(networkManager)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(new MangaTown(networkManager)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(new MangaHere(networkManager)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(
        new InternetArchive(networkManager, "IAManga", "manga", ContentManga)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(new AllNovel(networkManager)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(
        new InternetArchive(networkManager, "IANovels", "light novel", ContentLightNovel)));
    mangaSources.append(QSharedPointer<AbstractMangaSource>(
        new InternetArchive(networkManager, "IABooks (Beta)", "", ContentLightNovel)));

    currentMangaSource = mangaSources.isEmpty() ? nullptr : mangaSources.first().get();

    for (const auto& ms : qAsConst(mangaSources))
        ms->deserializeMangaList();

    aniList->deserialize();
    readingStats.deserialize();
    bookmarkManager.deserialize();
    loadHistory();

    updateActiveScources();

    // Refresh AniList data after UI is connected (deferred)
    QTimer::singleShot(1000, this, [this]()
    {
        if (aniList->isLoggedIn())
            aniList->fetchMangaList();
    });

    timer.setInterval(CONF.globalTickIntervalSeconds * 1000);
    connect(&timer, &QTimer::timeout, this, &UltimateMangaReaderCore::timerTick);

    // auto suspend
    connect(networkManager, &NetworkManager::activity, this, &UltimateMangaReaderCore::activity);
    connect(mangaController, &MangaController::activity, this, &UltimateMangaReaderCore::activity);

    // Use configurable auto-suspend
    int suspendMs = settings.autoSuspendMinutes * 60 * 1000;
    if (suspendMs <= 0)
        suspendMs = CONF.autoSuspendIntervalMinutes * 60 * 1000;
    autoSuspendTimer.setInterval(suspendMs);
    connect(&autoSuspendTimer, &QTimer::timeout,
            [this]()
            {
                qDebug() << "Auto Suspend after" << settings.autoSuspendMinutes << "minutes";
                if (settings.wifiAutoDisconnect)
                    networkManager->disconnectWifi();
                suspendManager->suspend();
            });
}

void UltimateMangaReaderCore::enableTimers(bool enabled)
{
    if (enabled == timer.isActive())
        return;

    if (enabled)
    {
        // Update auto-suspend interval from settings
        if (settings.autoSuspendMinutes > 0)
        {
            autoSuspendTimer.setInterval(settings.autoSuspendMinutes * 60 * 1000);
            autoSuspendTimer.start();
        }
        else
        {
            autoSuspendTimer.stop();  // "Never" setting
        }
        timerTick();
        QTimer::singleShot(1000 * 60 - QTime::currentTime().second() * 1000 - QTime::currentTime().msec(),
                           this,
                           [this]()
                           {
                               timer.start();
                               timerTick();
                           });
    }
    else
    {
        autoSuspendTimer.stop();
        timer.stop();
    }
}

void UltimateMangaReaderCore::activity()
{
    autoSuspendTimer.start();
}

void UltimateMangaReaderCore::timerTick()
{
    if (currentDay != QDate::currentDate().day())
    {
        currentDay = QDate::currentDate().day();
        favoritesManager->resetUpdatedStatus();
    }

    emit timeTick();
}

void UltimateMangaReaderCore::updateActiveScources()
{
    activeMangaSources.clear();
    QMap<QString, bool> enabledMangaSources;
    for (const auto& ms : qAsConst(mangaSources))
    {
        if (!settings.enabledMangaSources.contains(ms->name))
        {
            // IABooks defaults to disabled (beta)
            bool defaultEnabled = !ms->name.contains("Beta");
            enabledMangaSources.insert(ms->name, defaultEnabled);
        }
        else
            enabledMangaSources.insert(ms->name, settings.enabledMangaSources[ms->name]);

        if (enabledMangaSources[ms->name])
            activeMangaSources.insert(ms->name, ms.get());
    }
    settings.enabledMangaSources = enabledMangaSources;

    this->currentMangaSource = nullptr;

    emit activeMangaSourcesChanged(activeMangaSources.values());
}

void UltimateMangaReaderCore::setCurrentMangaSource(AbstractMangaSource* mangaSource)
{
    this->currentMangaSource = mangaSource;
    emit currentMangaSourceChanged(mangaSource);

    activity();
}

void UltimateMangaReaderCore::addToHistory(const QString &title, const QString &url,
                                            const QString &source)
{
    // Remove duplicate if exists
    for (int i = browsingHistory.size() - 1; i >= 0; i--)
        if (browsingHistory[i].title == title && browsingHistory[i].sourceName == source)
            browsingHistory.removeAt(i);

    HistoryEntry entry;
    entry.title = title;
    entry.url = url;
    entry.sourceName = source;
    entry.timestamp = QDateTime::currentDateTime();
    browsingHistory.prepend(entry);

    while (browsingHistory.size() > 50)
        browsingHistory.removeLast();

    saveHistory();
}

void UltimateMangaReaderCore::saveHistory()
{
    QFile file(CONF.cacheDir + "history.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&file);
    out << (int)browsingHistory.size();
    for (const auto &e : browsingHistory)
        out << e.title << e.url << e.sourceName << e.timestamp;
    file.close();
}

void UltimateMangaReaderCore::loadHistory()
{
    QFile file(CONF.cacheDir + "history.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;
    try
    {
        QDataStream in(&file);
        int count;
        in >> count;
        count = qBound(0, count, 200);
        for (int i = 0; i < count && !in.atEnd(); i++)
        {
            HistoryEntry e;
            in >> e.title >> e.url >> e.sourceName >> e.timestamp;
            if (in.status() == QDataStream::Ok)
                browsingHistory.append(e);
        }
    }
    catch (...) {}
    file.close();
}

void UltimateMangaReaderCore::setCurrentManga(const QString& mangaUrl, const QString& mangatitle)
{
    if (!currentMangaSource)
    {
        emit error("No manga source selected.");
        return;
    }

    addToHistory(mangatitle, mangaUrl, currentMangaSource->name);

    try
    {
        auto res = currentMangaSource->loadMangaInfo(mangaUrl, mangatitle);
        if (res.isOk())
            mangaController->setCurrentManga(res.unwrap());
        else
            emit error(res.unwrapErr());
    }
    catch (const QException &e)
    {
        emit error(QString("Failed to load manga info: ") + e.what());
    }
    catch (...)
    {
        emit error("Unexpected error loading manga info.");
    }
}

void UltimateMangaReaderCore::setupDirectories()
{
    if (!QDir(CONF.cacheDir).exists())
        QDir().mkpath(CONF.cacheDir);

    if (!QDir(CONF.mangaListDir).exists())
        QDir().mkpath(CONF.mangaListDir);

    if (!QDir(CONF.screensaverDir).exists())
        QDir().mkpath(CONF.screensaverDir);
}

void UltimateMangaReaderCore::clearDownloadCache(ClearDownloadCacheLevel level)
{
    switch (level)
    {
        case ClearImages:
            for (const auto& ms : qAsConst(mangaSources))
            {
                for (auto& info :
                     QDir(CONF.cacheDir + ms->name)
                         .entryInfoList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden | QDir::AllDirs))
                    removeDir(info.absoluteFilePath() + "/images");
            }
            break;

        case ClearInfos:
            for (const auto& ms : qAsConst(mangaSources))
                removeDir(CONF.cacheDir + ms->name, "progress.dat");

            break;

        case ClearAll:
            for (const auto& ms : qAsConst(mangaSources))
                removeDir(CONF.cacheDir + ms->name);
            QFile::remove(CONF.cacheDir + "favorites.dat");
            favoritesManager->clearFavorites();
            break;

        default:
            break;
    }
    emit downloadCacheCleared(level);
}

void UltimateMangaReaderCore::updateMangaLists(QSharedPointer<UpdateProgressToken> progressToken)
{
    for (const auto& name : progressToken->sourcesProgress.keys())
    {
        if (progressToken->sourcesProgress[name] == 100)
            continue;

        progressToken->currentSourceName = name;
        if (!activeMangaSources.contains(name))
            continue;
        auto ms = activeMangaSources[name];
        if (ms && ms->updateMangaList(progressToken.get()))
        {
            ms->mangaList.filter();
            ms->serializeMangaList();
        }
        else
        {
            sortMangaLists();
            return;
        }
    }
    progressToken->sendFinished();

    sortMangaLists();
}

bool UltimateMangaReaderCore::exportMangaAsCBZ(QSharedPointer<MangaInfo> manga, int fromCh, int toCh)
{
    if (!manga || !manga->mangaSource)
        return false;

    auto dir = CONF.exportDir();
    QDir().mkpath(dir);

    // Copy downloaded images to export directory as a folder (Kobo can browse)
    auto exportDir = dir + makePathLegal(manga->title) + "/";
    QDir().mkpath(exportDir);

    int count = 0;
    for (int c = fromCh; c <= toCh && c < manga->chapters.count(); c++)
    {
        auto imgDir = CONF.mangaimagesdir(manga->hostname, manga->title);
        QDir src(imgDir);
        for (const auto &f : src.entryList(QDir::Files))
        {
            if (f.startsWith(QString::number(c) + "_"))
            {
                auto dest = exportDir + f;
                if (!QFile::exists(dest))
                    QFile::copy(src.absoluteFilePath(f), dest);
                count++;
            }
        }
    }

    if (count == 0)
    {
        emit error("No downloaded images found. Download chapters first.");
        return false;
    }

    qDebug() << "Exported" << count << "images to" << exportDir;
    return true;
}

bool UltimateMangaReaderCore::exportNovelAsEPUB(QSharedPointer<MangaInfo> manga, int fromCh, int toCh)
{
    if (!manga || !manga->mangaSource)
        return false;

    auto dir = CONF.exportDir();
    QDir().mkpath(dir);

    // Check if chapters are download-only (raw PDFs) - download files directly
    bool downloadOnly = fromCh >= 0 && fromCh < manga->chapters.count() &&
                        manga->mangaSource->isDownloadOnly(manga->chapters[fromCh].chapterUrl);

    if (downloadOnly)
    {
        auto exportDir = dir + makePathLegal(manga->title) + "/";
        QDir().mkpath(exportDir);

        int exported = 0, failed = 0;
        for (int c = fromCh; c <= toCh && c < manga->chapters.count(); c++)
        {
            auto chapterUrl = manga->chapters[c].chapterUrl;
            // Build download URL: https://archive.org/download/{identifier}/{filename}
            auto downloadUrl = "https://archive.org/download/" + chapterUrl;
            auto filename = chapterUrl.mid(chapterUrl.indexOf('/') + 1);
            auto localPath = exportDir + makePathLegal(filename);

            qDebug() << "Exporting PDF:" << downloadUrl << "to" << localPath;

            auto job = networkManager->downloadAsFile(downloadUrl, localPath);
            if (job->await(120000))
                exported++;
            else
                failed++;
        }

        if (exported == 0)
        {
            emit error("Failed to download any files.");
            return false;
        }

        if (failed > 0)
            emit error(QString("Exported %1 files, but %2 failed to download.").arg(exported).arg(failed));

        qDebug() << "Exported" << exported << "PDFs to" << exportDir;
        return true;
    }

    // Text-based export (TXT chapters, AllNovel, etc.)
    auto filepath = dir + makePathLegal(manga->title) + ".html";

    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        emit error("Couldn't create export file.");
        return false;
    }

    QTextStream out(&file);

    out << "<!DOCTYPE html>\n<html>\n<head>\n"
        << "<meta charset='utf-8'>\n"
        << "<title>" << manga->title.toHtmlEscaped() << "</title>\n"
        << "<style>body{font-family:serif;font-size:12pt;line-height:1.8;padding:20px;}"
        << "h1{text-align:center;}h2{margin-top:2em;border-bottom:1px solid #ccc;padding-bottom:8px;}"
        << "p{text-indent:2em;margin:5px 0;}</style>\n"
        << "</head>\n<body>\n"
        << "<h1>" << manga->title.toHtmlEscaped() << "</h1>\n"
        << "<p>Author: " << manga->author.toHtmlEscaped() << "</p>\n";

    int exported = 0, failed = 0;
    for (int c = fromCh; c <= toCh && c < manga->chapters.count(); c++)
    {
        auto textResult = manga->mangaSource->getChapterText(manga->chapters[c].chapterUrl);
        if (!textResult.isOk())
        {
            failed++;
            continue;
        }

        out << "<h2>" << manga->chapters[c].chapterTitle.toHtmlEscaped() << "</h2>\n";
        auto text = textResult.unwrap();
        if (text.contains("<p>") || text.contains("<img"))
            out << text << "\n";
        else
            for (const auto &p : text.split("\n\n", Qt::SkipEmptyParts))
                if (!p.trimmed().isEmpty())
                    out << "<p>" << p.trimmed().toHtmlEscaped() << "</p>\n";
        exported++;
    }

    out << "</body>\n</html>\n";
    file.close();

    if (exported == 0)
    {
        QFile::remove(filepath);
        emit error("Couldn't fetch chapter text for export.");
        return false;
    }

    if (failed > 0)
        emit error(QString("Exported %1 chapters, but %2 failed to fetch.").arg(exported).arg(failed));

    qDebug() << "Exported" << exported << "chapters to" << filepath;
    return true;
}

QStringList UltimateMangaReaderCore::listDeviceExports()
{
    QStringList result;
    QDir dir(CONF.exportDir());
    if (!dir.exists())
        return result;
    for (const auto &f : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        result.append(f);
    return result;
}

void UltimateMangaReaderCore::sortMangaLists()
{
    QElapsedTimer timer;
    timer.start();

    for (const auto& ms : qAsConst(mangaSources))
    {
        ms->mangaList.sortBy(settings.mangaOrder);
        ms->serializeMangaList();
    }

    emit currentMangaSourceChanged(this->currentMangaSource);
}
