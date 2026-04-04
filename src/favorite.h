#ifndef READINGSTATE_H
#define READINGSTATE_H

#include "mangainfo.h"
#include "staticsettings.h"

struct Favorite
{
    QString hostname;
    QString title;
    QString mangaUrl;

    Favorite();

    Favorite(const QString &hostname, const QString &title, const QString &mangaUrl);

    static Favorite fromMangaInfo(MangaInfo *info)
    {
        return Favorite(info->hostname, info->title, info->url);
    }

private:
    QString mangaInfoPath() const;
};

QDataStream &operator<<(QDataStream &str, const Favorite &m);

QDataStream &operator>>(QDataStream &str, Favorite &m);

#endif  // READINGSTATE_H
