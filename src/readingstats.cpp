#include "readingstats.h"

ReadingStats::ReadingStats()
    : chaptersRead(0), pagesRead(0), minutesRead(0),
      streak(0), bestStreak(0)
{
}

void ReadingStats::startReading(const QString &title)
{
    stopReading();  // end previous session
    currentTitle = title;
    sessionStart = QDateTime::currentDateTime();
    updateStreak();
}

void ReadingStats::stopReading()
{
    if (!currentTitle.isEmpty() && sessionStart.isValid())
    {
        int mins = sessionStart.secsTo(QDateTime::currentDateTime()) / 60;
        if (mins > 0 && mins < 600)  // cap at 10 hours to filter bugs
            minutesRead += mins;
    }
    currentTitle.clear();
    sessionStart = QDateTime();
    serialize();
}

void ReadingStats::chapterCompleted(const QString &title)
{
    chaptersRead++;
    mangaChapters[title] = mangaChapters.value(title, 0) + 1;
    updateStreak();
    serialize();
}

void ReadingStats::pageRead(const QString &title)
{
    Q_UNUSED(title);
    pagesRead++;
}

void ReadingStats::updateStreak()
{
    auto today = QDate::currentDate();
    if (!lastReadDate.isValid())
    {
        streak = 1;
        lastReadDate = today;
    }
    else if (lastReadDate == today)
    {
        // Already counted today
    }
    else if (lastReadDate.daysTo(today) == 1)
    {
        streak++;
        lastReadDate = today;
    }
    else
    {
        streak = 1;
        lastReadDate = today;
    }
    if (streak > bestStreak)
        bestStreak = streak;
}

QString ReadingStats::mostReadManga() const
{
    QString best;
    int maxCh = 0;
    for (auto it = mangaChapters.begin(); it != mangaChapters.end(); ++it)
    {
        if (it.value() > maxCh)
        {
            maxCh = it.value();
            best = it.key();
        }
    }
    return best;
}

void ReadingStats::serialize()
{
    QFile file(CONF.cacheDir + "readingstats.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&file);
    out << chaptersRead << pagesRead << minutesRead << streak << bestStreak
        << lastReadDate << mangaChapters;
    file.close();
}

void ReadingStats::deserialize()
{
    QFile file(CONF.cacheDir + "readingstats.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;
    try
    {
        QDataStream in(&file);
        in >> chaptersRead >> pagesRead >> minutesRead >> streak >> bestStreak
           >> lastReadDate >> mangaChapters;
        if (in.status() != QDataStream::Ok)
        {
            chaptersRead = pagesRead = minutesRead = streak = bestStreak = 0;
            mangaChapters.clear();
        }
    }
    catch (...) {}
    file.close();
}
