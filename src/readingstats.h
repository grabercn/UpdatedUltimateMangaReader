#ifndef READINGSTATS_H
#define READINGSTATS_H

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QMap>

#include "staticsettings.h"

class ReadingStats
{
public:
    ReadingStats();

    // Track events
    void startReading(const QString &title);
    void stopReading();
    void chapterCompleted(const QString &title);
    void pageRead(const QString &title);

    // Queries
    int totalChaptersRead() const { return chaptersRead; }
    int totalPagesRead() const { return pagesRead; }
    int totalMinutesRead() const { return minutesRead; }
    int currentStreak() const { return streak; }
    int longestStreak() const { return bestStreak; }
    QString mostReadManga() const;
    QMap<QString, int> chaptersPerManga() const { return mangaChapters; }

    void serialize();
    void deserialize();

private:
    int chaptersRead;
    int pagesRead;
    int minutesRead;
    int streak;
    int bestStreak;
    QDate lastReadDate;
    QDateTime sessionStart;
    QString currentTitle;
    QMap<QString, int> mangaChapters;  // title -> chapters read

    void updateStreak();
};

#endif  // READINGSTATS_H
