#include "bookmarks.h"

BookmarkManager::BookmarkManager() {}

void BookmarkManager::addBookmark(const QString &title, const QString &source,
                                   int chapter, int page, const QString &note)
{
    // Don't duplicate
    for (const auto &b : bookmarks)
        if (b.mangaTitle == title && b.chapter == chapter && b.page == page)
            return;

    Bookmark bm;
    bm.mangaTitle = title;
    bm.sourceName = source;
    bm.chapter = chapter;
    bm.page = page;
    bm.note = note;
    bm.created = QDateTime::currentDateTime();
    bookmarks.prepend(bm);

    if (bookmarks.size() > 200)
        bookmarks.removeLast();

    serialize();
}

void BookmarkManager::removeBookmark(int index)
{
    if (index >= 0 && index < bookmarks.size())
    {
        bookmarks.removeAt(index);
        serialize();
    }
}

bool BookmarkManager::hasBookmark(const QString &title, int chapter, int page) const
{
    for (const auto &b : bookmarks)
        if (b.mangaTitle == title && b.chapter == chapter && b.page == page)
            return true;
    return false;
}

QList<Bookmark> BookmarkManager::getBookmarks(const QString &title) const
{
    if (title.isEmpty())
        return bookmarks;

    QList<Bookmark> result;
    for (const auto &b : bookmarks)
        if (b.mangaTitle == title)
            result.append(b);
    return result;
}

void BookmarkManager::serialize()
{
    QFile file(CONF.cacheDir + "bookmarks.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&file);
    out << (int)bookmarks.size();
    for (const auto &b : bookmarks)
        out << b.mangaTitle << b.sourceName << b.chapter << b.page << b.note << b.created;
    file.close();
}

void BookmarkManager::deserialize()
{
    QFile file(CONF.cacheDir + "bookmarks.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;
    try
    {
        QDataStream in(&file);
        int count;
        in >> count;
        count = qBound(0, count, 500);
        for (int i = 0; i < count && !in.atEnd(); i++)
        {
            Bookmark b;
            in >> b.mangaTitle >> b.sourceName >> b.chapter >> b.page >> b.note >> b.created;
            if (in.status() == QDataStream::Ok)
                bookmarks.append(b);
        }
    }
    catch (...) {}
    file.close();
}
