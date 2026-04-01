#ifndef ALLNOVEL_H
#define ALLNOVEL_H

#include "abstractmangasource.h"
#include "mangainfo.h"

class AllNovel : public AbstractMangaSource
{
public:
    explicit AllNovel(NetworkManager *dm);
    virtual ~AllNovel() = default;

    bool updateMangaList(UpdateProgressToken *token) override;
    Result<MangaList, QString> searchManga(const QString &query, int maxResults = 25) override;
    Result<MangaChapterCollection, QString> updateMangaInfoFinishedLoading(
        QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info) override;
    Result<QStringList, QString> getPageList(const QString &chapterUrl) override;
    Result<QString, QString> getChapterText(const QString &chapterUrl) override;
};

#endif  // ALLNOVEL_H
