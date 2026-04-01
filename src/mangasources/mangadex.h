#ifndef MANGADEX_H
#define MANGADEX_H

#include <QSet>

#include "abstractmangasource.h"
#include "mangainfo.h"

#define RAPIDJSON_ASSERT(x) \
    if (!(x))               \
        throw QException();

#include "thirdparty/rapidjson.h"

class MangaDex : public AbstractMangaSource
{
public:
    explicit MangaDex(NetworkManager *dm);
    virtual ~MangaDex() = default;

    bool updateMangaList(UpdateProgressToken *token) override;
    Result<MangaList, QString> searchManga(const QString &query, int maxResults = 25) override;
    Result<MangaChapterCollection, QString> updateMangaInfoFinishedLoading(
        QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info) override;
    Result<QStringList, QString> getPageList(const QString &chapterUrl) override;

    QString getFallbackImageUrl(int pageIndex) const;

private:
    void login();
    QString apiUrl;

    QVector<QString> statuses;
    QVector<QString> demographies;
    QMap<int, QString> genreMap;

    // CDN fallback: data-saver URLs when full quality fails
    mutable QString dataSaverHash;
    mutable QString dataSaverBaseUrl;
    mutable QStringList dataSaverFilenames;
};

#endif  // MANGADEX_H
