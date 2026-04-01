#ifndef ULITIMATEMANGAREADERCORE_H
#define ULITIMATEMANGAREADERCORE_H

#include <QObject>

#include "anilist.h"
#include "bookmarks.h"
#include "favoritesmanager.h"
#include "readingstats.h"
#include "mangachapterdownloadmanager.h"
#include "mangacontroller.h"
#include "mangadex.h"
#include "allnovel.h"
#include "mangaplus.h"
#include "mangatown.h"
#include "internetarchive.h"
#include "settings.h"
#include "suspendmanager.h"
#include "utils.h"

class UltimateMangaReaderCore : public QObject
{
    Q_OBJECT
public:
    explicit UltimateMangaReaderCore(QObject *parent = nullptr);

    QList<QSharedPointer<AbstractMangaSource>> mangaSources;

    QMap<QString, AbstractMangaSource *> activeMangaSources;

    AbstractMangaSource *currentMangaSource;

    QSharedPointer<MangaInfo> currentManga;

    NetworkManager *networkManager;
    MangaController *mangaController;
    FavoritesManager *favoritesManager;
    MangaChapterDownloadManager *mangaChapterDownloadManager;
    SuspendManager *suspendManager;
    AniList *aniList;
    ReadingStats readingStats;
    BookmarkManager bookmarkManager;

    Settings settings;

    // Browsing history
    struct HistoryEntry
    {
        QString title;
        QString url;
        QString sourceName;
        QDateTime timestamp;
    };
    QList<HistoryEntry> browsingHistory;
    void addToHistory(const QString &title, const QString &url, const QString &source);
    void saveHistory();
    void loadHistory();

public:
    void setCurrentMangaSource(AbstractMangaSource *mangaSource);
    void setCurrentManga(const QString &mangaUrl, const QString &mangatitle);

    void clearDownloadCache(ClearDownloadCacheLevel level);
    void updateActiveScources();

    void updateMangaLists(QSharedPointer<UpdateProgressToken> progressToken);
    void sortMangaLists();

    void enableTimers(bool enabled);

    void activity();

    // Export to device
    bool exportMangaAsCBZ(QSharedPointer<MangaInfo> manga, int fromCh, int toCh);
    bool exportNovelAsEPUB(QSharedPointer<MangaInfo> manga, int fromCh, int toCh);
    QStringList listDeviceExports();

signals:
    void currentMangaSourceChanged(AbstractMangaSource *source);
    void currentMangaChanged();
    void currentMangaIndexChanged();

    void activeMangaSourcesChanged(const QList<AbstractMangaSource *> &sources);

    void downloadCacheCleared(ClearDownloadCacheLevel level);

    void error(const QString &error);

    void timeTick();

private:
    QTimer timer;
    QTimer autoSuspendTimer;
    int currentDay;

    void timerTick();
    void setupDirectories();
};

#endif  // ULITIMATEMANGAREADERCORE_H
