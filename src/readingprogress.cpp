#include "readingprogress.h"

ReadingProgress::ReadingProgress(const MangaIndex &index, int numChapters,
                                 int numPages)
    : index(index), numChapters(numChapters), numPages(numPages){};

ReadingProgress::ReadingProgress(const QString &hostname, const QString &title)
    : numChapters(0), numPages(0)
{
    deserialize(hostname, title);
}

void ReadingProgress::serialize(const QString &hostname, const QString &title)

{
    QFile file(CONF.mangainfodir(hostname, title) + "progress.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;

    QDataStream out(&file);
    out << *this;

    file.close();
}

bool ReadingProgress::deserialize(const QString &hostname, const QString &title)
{
    QFile file(CONF.mangainfodir(hostname, title) + "progress.dat");

    if (!file.open(QIODevice::ReadOnly))
        return false;

    try
    {
        QDataStream in2(&file);
        in2 >> *this;
        if (in2.status() != QDataStream::Ok)
        {
            index = MangaIndex(0, 0);
            numChapters = 0;
            numPages = 0;
        }
    }
    catch (...)
    {
        index = MangaIndex(0, 0);
        numChapters = 0;
        numPages = 0;
    }

    file.close();
    return true;
}

QDataStream &operator<<(QDataStream &str, const ReadingProgress &m)
{
    return str << m.index << m.numChapters << m.numPages;
}

QDataStream &operator>>(QDataStream &str, ReadingProgress &m)
{
    return str >> m.index >> m.numChapters >> m.numPages;
}
