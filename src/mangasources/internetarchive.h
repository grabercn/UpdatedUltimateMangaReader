#ifndef INTERNETARCHIVE_H
#define INTERNETARCHIVE_H

#include "abstractmangasource.h"
#include "mangainfo.h"

#define RAPIDJSON_ASSERT(x) \
    if (!(x))               \
        throw QException();

#include "thirdparty/rapidjson.h"

class InternetArchive : public AbstractMangaSource
{
public:
    explicit InternetArchive(NetworkManager *dm,
                             const QString &sourceName = "IA Manga",
                             const QString &subjectFilter = "manga",
                             ContentType type = ContentManga);
    virtual ~InternetArchive() = default;

    bool updateMangaList(UpdateProgressToken *token) override;
    Result<MangaList, QString> searchManga(const QString &query, int maxResults = 25) override;
    Result<QSharedPointer<MangaInfo>, QString> getMangaInfo(const QString &mangaUrl,
                                                            const QString &mangaTitle) override;
    Result<MangaChapterCollection, QString> updateMangaInfoFinishedLoading(
        QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info) override;
    Result<QStringList, QString> getPageList(const QString &chapterUrl) override;
    Result<QString, QString> getChapterText(const QString &chapterUrl) override;
    bool isDownloadOnly(const QString &chapterUrl) override;

private:
    QString searchApiUrl;
    QString metadataUrl;
    QString downloadUrl;
    QString subjectFilter;

    // Cache: identifier -> has page images (jp2.zip derived files)
    mutable QMap<QString, bool> hasPageImagesCache;
    bool checkHasPageImages(const QString &identifier) const;
};

#endif  // INTERNETARCHIVE_H
