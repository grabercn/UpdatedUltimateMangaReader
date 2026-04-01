#ifndef BOOKMARKS_H
#define BOOKMARKS_H

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QList>

#include "staticsettings.h"

struct Bookmark
{
    QString mangaTitle;
    QString sourceName;
    int chapter;
    int page;
    QString note;
    QDateTime created;
};

class BookmarkManager
{
public:
    BookmarkManager();

    void addBookmark(const QString &title, const QString &source, int chapter, int page,
                     const QString &note = "");
    void removeBookmark(int index);
    bool hasBookmark(const QString &title, int chapter, int page) const;

    QList<Bookmark> getBookmarks(const QString &title = "") const;
    const QList<Bookmark> &allBookmarks() const { return bookmarks; }

    void serialize();
    void deserialize();

private:
    QList<Bookmark> bookmarks;
};

#endif  // BOOKMARKS_H
