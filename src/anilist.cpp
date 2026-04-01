#include "anilist.h"
#include "staticsettings.h"

AniList::AniList(NetworkManager *networkManager, QObject *parent)
    : QObject(parent), networkManager(networkManager), userId(0),
      trackDebounceTimer(new QTimer(this)), pendingTrackChapter(0)
{
    trackDebounceTimer->setSingleShot(true);
    trackDebounceTimer->setInterval(10000);  // 10 seconds
    connect(trackDebounceTimer, &QTimer::timeout, this, [this]()
    {
        if (!pendingTrackTitle.isEmpty() && pendingTrackChapter >= 0)
        {
            try
            {
                auto entry = findByTitle(pendingTrackTitle);
                int mediaId = entry.mediaId;
                if (mediaId == 0)
                    mediaId = searchMediaId(pendingTrackTitle);
                if (mediaId > 0)
                {
                    // Only update if we've progressed beyond what AniList knows
                    if (pendingTrackChapter > entry.progress || entry.status != 1)
                        updateProgress(mediaId, pendingTrackChapter, 1);
                }
            }
            catch (...)
            {
                qDebug() << "AniList sync failed (non-fatal)";
            }
        }
        pendingTrackTitle.clear();
        pendingTrackChapter = 0;
    });
}

QSharedPointer<DownloadStringJob> AniList::graphqlRequest(const QString &query, const QString &variables)
{
    auto cleanQuery = QString(query).simplified().replace("\"", "\\\"").replace("\n", " ");
    auto body = QString("{\"query\":\"%1\",\"variables\":%2}").arg(cleanQuery, variables);

    QList<std::tuple<const char *, const char *>> headers;
    if (!authToken.isEmpty())
    {
        static QByteArray authHeader;
        authHeader = ("Bearer " + authToken).toUtf8();
        headers.append(std::make_tuple("Authorization", authHeader.constData()));
    }
    headers.append(std::make_tuple("Content-Type", "application/json"));
    headers.append(std::make_tuple("Accept", "application/json"));

    return networkManager->downloadAsString("https://graphql.anilist.co", 15000,
                                            body.toUtf8(), headers);
}

void AniList::loginWithToken(const QString &token)
{
    authToken = token;

    auto job = graphqlRequest("{ Viewer { id name } }");
    if (!job->await(10000))
    {
        authToken.clear();
        emit error("Failed to connect to AniList.");
        return;
    }

    auto data = job->bufferStr;
    // Simple JSON parse for viewer
    QRegularExpression idRx(R"lit("id"\s*:\s*(\d+))lit");
    QRegularExpression nameRx(R"lit("name"\s*:\s*"([^"]*)")lit");
    auto idMatch = idRx.match(data);
    auto nameMatch = nameRx.match(data);

    if (idMatch.hasMatch() && nameMatch.hasMatch())
    {
        userId = idMatch.captured(1).toInt();
        m_username = nameMatch.captured(1);
        serialize();
        emit loginStatusChanged(true);
        fetchMangaList();
    }
    else
    {
        authToken.clear();
        emit error("AniList login failed. Check your token.");
    }
}

void AniList::logout()
{
    authToken.clear();
    m_username.clear();
    userId = 0;
    m_entries.clear();
    serialize();
    emit loginStatusChanged(false);
}

void AniList::fetchMangaList()
{
    if (!isLoggedIn())
        return;

    auto query = R"(
        query ($userId: Int) {
            MediaListCollection(userId: $userId, type: MANGA) {
                lists {
                    entries {
                        id mediaId status progress
                        score(format: POINT_10)
                        media {
                            title { romaji english }
                            chapters
                            coverImage { medium }
                        }
                    }
                }
            }
        }
    )";
    auto vars = QString(R"({"userId":%1})").arg(userId);
    auto job = graphqlRequest(query, vars);

    if (!job->await(15000))
    {
        emit error("Failed to fetch AniList manga list.");
        return;
    }

    m_entries.clear();

    // Parse entries using regex (avoid rapidjson dependency here)
    auto data = job->bufferStr;

    // Find each entry block
    QRegularExpression entryRx(
        R"lit("id"\s*:\s*(\d+)\s*,\s*"mediaId"\s*:\s*(\d+)\s*,\s*"status"\s*:\s*"(\w+)"\s*,\s*"progress"\s*:\s*(\d+)\s*,\s*"score"\s*:\s*(\d+))lit",
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpression titleRx(R"lit("english"\s*:\s*(?:"([^"]*)"|null))lit", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression romajiRx(R"lit("romaji"\s*:\s*"([^"]*)")lit", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression chaptersRx(R"lit("chapters"\s*:\s*(?:(\d+)|null))lit");
    QRegularExpression coverRx(R"lit("medium"\s*:\s*"([^"]*)")lit");

    // Split by entry blocks
    int pos = 0;
    auto entryMatches = entryRx.globalMatch(data);
    while (entryMatches.hasNext())
    {
        auto m = entryMatches.next();
        AniListEntry entry;
        entry.listEntryId = m.captured(1).toInt();
        entry.mediaId = m.captured(2).toInt();

        auto statusStr = m.captured(3);
        if (statusStr == "CURRENT") entry.status = 1;
        else if (statusStr == "PLANNING") entry.status = 2;
        else if (statusStr == "COMPLETED") entry.status = 3;
        else if (statusStr == "DROPPED") entry.status = 4;
        else if (statusStr == "PAUSED") entry.status = 5;
        else if (statusStr == "REPEATING") entry.status = 6;

        entry.progress = m.captured(4).toInt();
        entry.score = m.captured(5).toInt();

        // Find title near this match
        int searchStart = m.capturedEnd();
        int searchEnd = qMin(searchStart + 500, data.length());
        auto titleSlice = data.mid(searchStart, searchEnd - searchStart);

        auto tm = titleRx.match(titleSlice);
        auto rm = romajiRx.match(titleSlice);
        entry.titleRomaji = rm.hasMatch() ? rm.captured(1) : "";
        entry.title = (tm.hasMatch() && !tm.captured(1).isEmpty()) ? tm.captured(1)
                    : !entry.titleRomaji.isEmpty() ? entry.titleRomaji : "Unknown";

        auto cm = chaptersRx.match(titleSlice);
        if (cm.hasMatch() && !cm.captured(1).isEmpty())
            entry.totalChapters = cm.captured(1).toInt();

        auto covm = coverRx.match(titleSlice);
        if (covm.hasMatch())
            entry.coverUrl = covm.captured(1).replace("\\/", "/");

        m_entries.append(entry);
    }

    serialize();
    emit mangaListUpdated();
}

static QString normalizeTitle(const QString &t)
{
    return t.toLower().trimmed()
        .remove(QRegularExpression(R"([^\w\s])"))  // remove punctuation
        .replace(QRegularExpression(R"(\s+)"), " ");  // collapse whitespace
}

AniListEntry AniList::findByTitle(const QString &title) const
{
    auto norm = normalizeTitle(title);

    // Exact normalized match (English or Romaji)
    for (const auto &e : m_entries)
    {
        if (normalizeTitle(e.title) == norm)
            return e;
        if (!e.titleRomaji.isEmpty() && normalizeTitle(e.titleRomaji) == norm)
            return e;
    }

    // Substring match (either direction, both titles)
    for (const auto &e : m_entries)
    {
        auto en = normalizeTitle(e.title);
        auto rn = normalizeTitle(e.titleRomaji);
        if (en.contains(norm) || norm.contains(en))
            return e;
        if (!rn.isEmpty() && (rn.contains(norm) || norm.contains(rn)))
            return e;
    }

    // Word overlap match - if >60% of words match
    auto searchWords = norm.split(' ', Qt::SkipEmptyParts).toSet();
    if (searchWords.size() >= 2)
    {
        for (const auto &e : m_entries)
        {
            auto entryWords = normalizeTitle(e.title).split(' ', Qt::SkipEmptyParts).toSet();
            auto common = searchWords & entryWords;
            int minSize = qMin(searchWords.size(), entryWords.size());
            if (minSize > 0 && common.size() * 100 / minSize >= 60)
                return e;

            if (!e.titleRomaji.isEmpty())
            {
                auto romajiWords = normalizeTitle(e.titleRomaji).split(' ', Qt::SkipEmptyParts).toSet();
                auto commonR = searchWords & romajiWords;
                int minR = qMin(searchWords.size(), romajiWords.size());
                if (minR > 0 && commonR.size() * 100 / minR >= 60)
                    return e;
            }
        }
    }

    return AniListEntry();
}

void AniList::updateProgress(int mediaId, int chapters, int status)
{
    if (!isLoggedIn())
        return;

    static const char *statusNames[] = {"", "CURRENT", "PLANNING", "COMPLETED", "DROPPED", "PAUSED", "REPEATING"};
    auto statusStr = (status >= 1 && status <= 6) ? statusNames[status] : "CURRENT";

    auto query = R"(
        mutation ($mediaId: Int, $progress: Int, $status: MediaListStatus) {
            SaveMediaListEntry(mediaId: $mediaId, progress: $progress, status: $status) {
                id progress status
            }
        }
    )";
    auto vars = QString(R"({"mediaId":%1,"progress":%2,"status":"%3"})")
                    .arg(mediaId).arg(chapters).arg(statusStr);

    auto job = graphqlRequest(query, vars);
    if (!job->await(10000))
        return;

    // Update local cache
    for (auto &e : m_entries)
    {
        if (e.mediaId == mediaId)
        {
            e.progress = chapters;
            e.status = status;
            break;
        }
    }
    serialize();
}

int AniList::searchMediaId(const QString &title)
{
    if (title.isEmpty())
        return 0;

    auto query = R"(query ($search: String) { Media(search: $search, type: MANGA) { id } })";
    auto vars = QString(R"({"search":"%1"})").arg(QString(title).replace("\"", "\\\""));
    auto job = graphqlRequest(query, vars);

    if (!job->await(10000))
        return 0;

    QRegularExpression idRx(R"lit("id"\s*:\s*(\d+))lit");
    auto m = idRx.match(job->bufferStr);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

void AniList::trackReading(const QString &title, int chapter)
{
    if (!isLoggedIn() || title.isEmpty())
        return;

    // chapter is 0-based index of the chapter being READ
    // AniList progress = number of COMPLETED chapters
    // So reading ch.0 means 0 completed, finishing ch.0 and moving to ch.1 means 1 completed
    // The caller passes chapter+1 as the completed count

    // Debounce: store latest progress and restart timer
    if (chapter > pendingTrackChapter || title != pendingTrackTitle)
    {
        pendingTrackTitle = title;
        pendingTrackChapter = chapter;
        trackDebounceTimer->start();

        // Update local cache immediately (no network)
        auto entry = findByTitle(title);
        if (entry.mediaId > 0)
        {
            for (auto &e : m_entries)
            {
                if (e.mediaId == entry.mediaId)
                {
                    if (chapter > e.progress)
                        e.progress = chapter;
                    if (e.status != 1)
                        e.status = 1;  // Set to CURRENT/Reading
                    break;
                }
            }
        }
        else
        {
            // Not in local cache yet - will be searched on debounce fire
            // Add a placeholder entry so future findByTitle works
            AniListEntry newEntry;
            newEntry.title = title;
            newEntry.progress = chapter;
            newEntry.status = 1;
            m_entries.append(newEntry);
        }
    }
}

void AniList::updateStatus(int mediaId, int status)
{
    if (!isLoggedIn())
        return;

    static const char *statusNames[] = {"", "CURRENT", "PLANNING", "COMPLETED",
                                         "DROPPED", "PAUSED", "REPEATING"};
    if (status < 1 || status > 6)
        return;

    auto query = R"(mutation ($mediaId: Int, $status: MediaListStatus) { SaveMediaListEntry(mediaId: $mediaId, status: $status) { id status } })";
    auto vars = QString(R"({"mediaId":%1,"status":"%2"})").arg(mediaId).arg(statusNames[status]);
    auto job = graphqlRequest(query, vars);
    if (!job->await(10000))
        return;

    for (auto &e : m_entries)
    {
        if (e.mediaId == mediaId)
        {
            e.status = status;
            break;
        }
    }
    serialize();
    emit mangaListUpdated();
}

void AniList::updateScore(int mediaId, int score)
{
    if (!isLoggedIn())
        return;

    auto query = R"(mutation ($mediaId: Int, $score: Float) { SaveMediaListEntry(mediaId: $mediaId, score: $score) { id score } })";
    auto vars = QString(R"({"mediaId":%1,"score":%2})").arg(mediaId).arg(score);
    auto job = graphqlRequest(query, vars);
    if (!job->await(10000))
        return;

    for (auto &e : m_entries)
    {
        if (e.mediaId == mediaId)
        {
            e.score = score;
            break;
        }
    }
    serialize();
    emit mangaListUpdated();
}

QString AniList::statusName(int status)
{
    switch (status)
    {
        case 1: return "Reading";
        case 2: return "Planning";
        case 3: return "Completed";
        case 4: return "Dropped";
        case 5: return "Paused";
        case 6: return "Repeating";
        default: return "Not Tracked";
    }
}

QList<AniListEntry> AniList::entriesByStatus(int status) const
{
    QList<AniListEntry> result;
    for (const auto &e : m_entries)
        if (e.status == status)
            result.append(e);
    return result;
}

void AniList::serialize()
{
    QFile file(QString(CONF.cacheDir) + "/anilist.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&file);
    out << authToken << m_username << userId << (int)m_entries.size();
    for (const auto &e : m_entries)
        out << e.mediaId << e.listEntryId << e.title << e.titleRomaji << e.coverUrl
            << e.status << e.progress << e.score << e.totalChapters;
    file.close();
}

void AniList::deserialize()
{
    QFile file(QString(CONF.cacheDir) + "/anilist.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;

    try
    {
        QDataStream in(&file);
        int count = 0;
        in >> authToken >> m_username >> userId >> count;

        if (in.status() != QDataStream::Ok || count < 0 || count > 100000)
        {
            qDebug() << "Corrupt AniList cache, resetting";
            authToken.clear();
            m_username.clear();
            userId = 0;
            file.close();
            return;
        }

        m_entries.clear();
        for (int i = 0; i < count && !in.atEnd(); i++)
        {
            AniListEntry e;
            in >> e.mediaId >> e.listEntryId >> e.title >> e.titleRomaji >> e.coverUrl
               >> e.status >> e.progress >> e.score >> e.totalChapters;
            if (in.status() != QDataStream::Ok)
                break;
            m_entries.append(e);
        }
    }
    catch (...)
    {
        qDebug() << "Error deserializing AniList cache";
        authToken.clear();
        userId = 0;
        m_entries.clear();
    }

    file.close();
    if (!authToken.isEmpty() && userId > 0)
        emit loginStatusChanged(true);
}
