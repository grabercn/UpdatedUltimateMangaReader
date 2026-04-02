#ifndef ANILIST_H
#define ANILIST_H

#include <QObject>
#include <QTimer>

#include "networkmanager.h"

struct AniListEntry
{
    int mediaId = 0;
    int listEntryId = 0;
    QString title;        // English or best available
    QString titleRomaji;  // Japanese romanized
    QString coverUrl;
    int status = 0;  // 0=none,1=current,2=planning,3=completed,4=dropped,5=paused,6=repeating
    int progress = 0;
    int progressVolumes = 0;
    int score = 0;
    int totalChapters = 0;
    int totalVolumes = 0;
};

class AniList : public QObject
{
    Q_OBJECT

public:
    explicit AniList(NetworkManager *networkManager, QObject *parent = nullptr);

    bool isLoggedIn() const { return !authToken.isEmpty() && userId > 0; }
    QString username() const { return m_username; }

    void loginWithToken(const QString &token);
    void logout();
    void fetchMangaList();

    AniListEntry findByTitle(const QString &title) const;
    int searchMediaId(const QString &title);  // search AniList API for mediaId
    void updateProgress(int mediaId, int chapters, int status = 1, int volumes = -1);
    void updateStatus(int mediaId, int status);
    void updateScore(int mediaId, int score);

    // High-level: track reading of a manga/LN by title
    void trackReading(const QString &title, int chapter);

    void serialize();
    void deserialize();
    void syncOfflineChanges();  // call when coming back online

    QList<AniListEntry> entriesByStatus(int status) const;
    QList<AniListEntry> allEntries() const { return m_entries; }

    static QString statusName(int status);

    // AniList OAuth client ID
    static constexpr const char *clientId = "25108";

signals:
    void loginStatusChanged(bool loggedIn);
    void mangaListUpdated();
    void error(const QString &msg);

private:
    NetworkManager *networkManager;
    QString authToken;
    QString m_username;
    int userId;
    QList<AniListEntry> m_entries;

    QSharedPointer<DownloadStringJob> graphqlRequest(const QString &query, const QString &variables = "{}");

    QTimer *trackDebounceTimer;
    QString pendingTrackTitle;
    int pendingTrackChapter;

    // Offline queue: pending updates to sync when back online
    struct PendingSync
    {
        int mediaId;
        int progress;
        int status;
        int score;
        qint64 timestamp;  // ms since epoch
    };
    QList<PendingSync> offlineQueue;
    void saveOfflineQueue();
    void loadOfflineQueue();
    void flushOfflineQueue();
};

#endif  // ANILIST_H
