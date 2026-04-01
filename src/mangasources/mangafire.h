#ifndef MANGAFIRE_H
#define MANGAFIRE_H

#include "abstractmangasource.h"
#include "mangainfo.h"

class MangaFire : public AbstractMangaSource
{
public:
    explicit MangaFire(NetworkManager *dm);
    virtual ~MangaFire() = default;

    bool updateMangaList(UpdateProgressToken *token) override;
    Result<MangaList, QString> searchManga(const QString &query, int maxResults = 25) override;
    Result<QSharedPointer<MangaInfo>, QString> getMangaInfo(const QString &mangaUrl,
                                                            const QString &mangaTitle) override;
    Result<MangaChapterCollection, QString> updateMangaInfoFinishedLoading(
        QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info) override;
    Result<QStringList, QString> getPageList(const QString &chapterUrl) override;

private:
    QString filterUrl;
};

#endif  // MANGAFIRE_H
