#include "mangadex.h"

#include <QUrl>

using namespace rapidjson;

inline QString getStringSafe(const GenericValue<UTF8<>> &jsonobject, const char *member)
{
    if (!jsonobject.HasMember(member))
        return "";

    auto &jm = jsonobject[member];

    if (jm.IsNull())
        return "";

    return jm.GetString();
}

QString padChapterNumber(const QString &number, int places = 4)
{
    auto range = number.split('-');
    QStringList result;
    std::transform(range.begin(), range.end(), std::back_inserter(result),
                   [places](QString chapter)
                   {
                       chapter = chapter.trimmed();
                       auto digits = chapter.split('.')[0].length();
                       return QString("0").repeated(qMax(0, places - digits)) + chapter;
                   });
    return result.join('-');
}

MangaDex::MangaDex(NetworkManager *dm) : AbstractMangaSource(dm)
{
    name = "MangaDex";
    apiUrl = "https://api.mangadex.org";
    baseUrl = apiUrl;

    // One-time invalidation of old data-saver cached URLs
    auto migrationFlag = CONF.mangasourcedir(this->name) + ".datasaver_migrated";
    if (!QFile::exists(migrationFlag))
    {
        auto path = CONF.mangasourcedir(this->name);
        QDir dir(path);
        for (const auto &mangadir : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            auto mangaPath = CONF.mangainfodir(name, mangadir) + "mangainfo.dat";
            if (!QFile::exists(mangaPath))
                continue;
            try
            {
                auto mi = MangaInfo::deserialize(this, mangaPath);
                bool dirty = false;
                for (int i = 0; i < mi->chapters.size(); i++)
                {
                    if (!mi->chapters[i].pagesLoaded)
                        continue;
                    for (int c = 0; c < mi->chapters[i].imageUrlList.size(); c++)
                    {
                        if (mi->chapters[i].imageUrlList[c].contains("data-saver"))
                        {
                            mi->chapters[i].imageUrlList[c] = "";
                            dirty = true;
                        }
                    }
                }
                if (dirty)
                    mi->serialize();
            }
            catch (...)
            {
            }
        }
        QFile(migrationFlag).open(QIODevice::WriteOnly);  // touch file
    }

    networkManager->addCookie(".mangadex.org", "mangadex_h_toggle", "1");
    networkManager->addCookie(".mangadex.org", "mangadex_title_mode", "2");
    networkManager->addCookie(".mangadex.org", "mangadex_filter_langs", "1");

    statuses = {"Ongoing", "Completed", "Cancelled", "Hiatus"};
    demographies = {"Shounen", "Shoujo", "Seinen", "Josei"};
    genreMap.insert(2, "Action");
    genreMap.insert(3, "Adventure");
    genreMap.insert(5, "Comedy");
    genreMap.insert(8, "Drama");
    genreMap.insert(9, "Ecchi");
    genreMap.insert(10, "Fantasy");
    genreMap.insert(13, "Historical");
    genreMap.insert(14, "Horror");
    genreMap.insert(17, "Mecha");
    genreMap.insert(18, "Medical");
    genreMap.insert(20, "Mystery");
    genreMap.insert(22, "Psychological");
    genreMap.insert(23, "Romance");
    genreMap.insert(25, "Sci-Fi");
    genreMap.insert(28, "Shoujo Ai");
    genreMap.insert(30, "Shounen Ai");
    genreMap.insert(31, "Slice of Life");
    genreMap.insert(32, "Smut");
    genreMap.insert(33, "Sports");
    genreMap.insert(35, "Tragedy");
    genreMap.insert(37, "Yaoi");
    genreMap.insert(38, "Yuri");
    genreMap.insert(41, "Isekai");
    genreMap.insert(49, "Gore");
    genreMap.insert(50, "Sexual Violence");
    genreMap.insert(51, "Crime");
    genreMap.insert(52, "Magical Girls");
    genreMap.insert(53, "Philosophical");
    genreMap.insert(54, "Superhero");
    genreMap.insert(55, "Thriller");
    genreMap.insert(56, "Wuxia");
}

Result<MangaList, QString> MangaDex::searchManga(const QString &query, int maxResults)
{
    MangaList results;
    results.absoluteUrls = false;

    if (query.isEmpty())
        return Ok(results);

    auto encodedQuery = QString::fromUtf8(QUrl::toPercentEncoding(query));
    auto searchUrlStr = apiUrl + "/manga?title=" + encodedQuery +
                        "&limit=" + QString::number(qMin(maxResults, 100)) +
                        "&contentRating[]=safe&contentRating[]=suggestive"
                        "&contentRating[]=erotica"
                        "&order[relevance]=desc";

    auto job = networkManager->downloadAsString(searchUrlStr, -1);

    if (!job->await(7000))
        return Err(job->errorString);

    try
    {
        Document doc;
        ParseResult res = doc.Parse(job->buffer.data());
        if (!res || !doc.HasMember("data"))
            return Err(QString("Couldn't parse search results."));

        auto data = doc["data"].GetArray();
        for (const auto &r : data)
        {
            auto title = getStringSafe(r["attributes"]["title"], "en");
            if (title.isEmpty())
            {
                // Try other languages
                auto &titleObj = r["attributes"]["title"];
                for (auto it = titleObj.MemberBegin(); it != titleObj.MemberEnd(); ++it)
                {
                    title = QString(it->value.GetString());
                    if (!title.isEmpty())
                        break;
                }
            }
            // Extract romaji/japanese alt title
            QString altTitle;
            if (r["attributes"].HasMember("altTitles") && r["attributes"]["altTitles"].IsArray())
            {
                for (const auto &alt : r["attributes"]["altTitles"].GetArray())
                {
                    // Prefer ja-ro (romaji) then ja
                    auto jaro = getStringSafe(alt, "ja-ro");
                    if (!jaro.isEmpty()) { altTitle = jaro; break; }
                    auto ja = getStringSafe(alt, "ja");
                    if (!ja.isEmpty() && altTitle.isEmpty()) altTitle = ja;
                    auto en = getStringSafe(alt, "en");
                    if (!en.isEmpty() && altTitle.isEmpty() && en != title) altTitle = en;
                }
            }

            auto id = getStringSafe(r, "id");
            auto url = QString("/manga/%1?includes[]=cover_art").arg(id);
            if (!title.isEmpty())
                results.append(title, url, altTitle);
        }
    }
    catch (QException &)
    {
        return Err(QString("Couldn't parse search results."));
    }

    return Ok(results);
}

bool MangaDex::updateMangaList(UpdateProgressToken *token)
{
    MangaList mangas;
    token->sendProgress(10);

    QElapsedTimer timer;
    timer.start();

    auto mangasQuerry = apiUrl +
                        "/manga?limit=100&offset=%1"
                        "&publicationDemographic[]=%2"
                        "&order[createdAt]=%3"
                        "&contentRating[]=safe"
                        "&contentRating[]=suggestive"
                        "&contentRating[]=erotica"
                        "&contentRating[]=pornographic";

    // ugly workaround, current search limit is 10000 entries
    // so we need to search multiple times with different filters
    const char *demographics[] = {"seinen", "none", "none", "josei", "shoujo", "shounen"};
    const char *order[] = {"asc", "asc", "desc", "asc", "asc", "asc"};

    int maxnummangas = 50000;
    int matches = 0;

    QSet<QString> mangaids;

    for (uint i = 0; i < sizeof(demographics) / (sizeof(demographics[0])); i++)
        for (int offset = 0; offset < 10000; offset += 100)
        {
            auto mangasUrl = mangasQuerry.arg(offset).arg(demographics[i], order[i]);
            auto job = networkManager->downloadAsString(mangasUrl, -1);

            if (!job->await(7000))
            {
                token->sendError(job->errorString);
                return false;
            }

            try
            {
                Document doc;
                ParseResult res = doc.Parse(job->buffer.data());

                if (!res)
                    return false;

                if (doc.HasMember("result") && QString(doc["result"].GetString()) == "error")
                    return false;

                auto results = doc["data"].GetArray();

                for (const auto &r : results)
                {
                    auto title = getStringSafe(r["attributes"]["title"], "en");
                    auto id = getStringSafe(r, "id");
                    auto url = QString("/manga/") + id;

                    if (!mangaids.contains(id))
                    {
                        mangas.append(title, url);
                        matches++;
                        mangaids.insert(id);
                    }
                }

                token->sendProgress(10 + 90 * matches / maxnummangas);

                if (results.Size() < 100)
                    break;
            }
            catch (QException &)
            {
                return false;
            }
        }

    this->mangaList = mangas;

    qDebug() << "mangas:" << mangas.size << "time:" << timer.elapsed();

    token->sendProgress(100);

    return true;
}

Result<MangaChapterCollection, QString> MangaDex::updateMangaInfoFinishedLoading(
    QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info)
{
    QRegularExpression bbrx(R"(\[.*?\])");

    MangaChapterCollection newchapters;

    try
    {
        Document doc;
        ParseResult res = doc.Parse(job->buffer.data());
        if (!res)
            return Err(QString("Couldn't parse manga infos."));

        auto &mangaObject = doc["data"]["attributes"];

        info->status = getStringSafe(mangaObject, "status");

        if (mangaObject.HasMember("year") && !mangaObject["year"].IsNull())
            info->releaseYear = QString::number(mangaObject["year"].GetInt());

        info->genres = getStringSafe(mangaObject, "publicationDemographic");

        info->summary = htmlToPlainText(getStringSafe(mangaObject["description"], "en")).remove(bbrx);

        auto rels = doc["data"]["relationships"].GetArray();

        auto mangaId = getStringSafe(doc["data"], "id");

        for (const auto &rel : rels)
        {
            if (getStringSafe(rel, "type") != "cover_art")
                continue;

            // Try inline attributes first (newer API responses include them)
            if (rel.HasMember("attributes") && !rel["attributes"].IsNull())
            {
                auto fileName = getStringSafe(rel["attributes"], "fileName");
                if (!fileName.isEmpty())
                {
                    info->coverUrl = QString("https://uploads.mangadex.org/covers/%1/%2.256.jpg")
                                         .arg(mangaId, fileName);
                    break;
                }
            }

            // Fallback: fetch cover details from API
            auto coverId = getStringSafe(rel, "id");
            if (coverId.isEmpty())
                continue;

            auto jobCover = networkManager->downloadAsString(
                apiUrl + "/cover/" + coverId, 8000);

            if (jobCover->await(8000))
            {
                try
                {
                    Document coverdoc;
                    if (coverdoc.Parse(jobCover->buffer.data()).HasParseError())
                        continue;
                    auto fileName = getStringSafe(coverdoc["data"]["attributes"], "fileName");
                    if (!fileName.isEmpty())
                    {
                        info->coverUrl = QString("https://uploads.mangadex.org/covers/%1/%2.256.jpg")
                                             .arg(mangaId, fileName);
                    }
                }
                catch (...)
                {
                }
            }
            break;
        }

        int totalchapters = 100;
        QStringList chapternumberlist;

        for (int offset = 0; offset < totalchapters; offset += 100)
        {
            auto params = QString("manga=%1&limit=100&offset=%2&translatedLanguage[]=en")
                              .arg(getStringSafe(doc["data"], "id"))
                              .arg(offset);
            auto jobChapters = networkManager->downloadAsString(apiUrl + "/chapter?" + params, -1);

            if (jobChapters->await(3000))
            {
                Document chaptersdoc;
                chaptersdoc.Parse(jobChapters->buffer.data());

                if (getStringSafe(chaptersdoc, "result") == "error")
                    return Err(QString("Couldn't parse chapter list."));

                auto results = chaptersdoc["data"].GetArray();

                totalchapters = chaptersdoc["total"].GetInt();

                for (const auto &r : results)
                {
                    auto chapterId = getStringSafe(r, "id");
                    const auto &attributes = r["attributes"];
                    auto externalUrl = getStringSafe(attributes, "externalUrl");

                    if (chapterId == "" || externalUrl != "")
                        continue;

                    QString numChapter = getStringSafe(attributes, "chapter");
                    if (numChapter == "")
                        numChapter = "0";

                    QString chapterTitle = "Ch. " + numChapter;

                    if (!r["attributes"]["title"].IsNull())
                        chapterTitle += " " + getStringSafe(attributes, "title");

                    MangaChapter mangaChapter(chapterTitle, chapterId);
                    mangaChapter.chapterNumber = padChapterNumber(numChapter);
                    newchapters.append(mangaChapter);
                    chapternumberlist.append(padChapterNumber(numChapter));
                }
                if (results.Size() < 100)
                    break;
            }
        }
        int size = newchapters.size();

        QVector<int> indices(size);
        QVector<int> indicesInv(size);
        for (int i = 0; i < size; ++i)
            indices[i] = i;

        std::sort(indices.begin(), indices.end(),
                  [&chapternumberlist](int a, int b) {
                      return QString::compare(chapternumberlist[a], chapternumberlist[b],
                                              Qt::CaseInsensitive) < 0;
                  });

        for (int i = 0; i < size; ++i)
            indicesInv[indices[i]] = i;

        for (int i = 0; i < size; i++)
            while (i != indicesInv[i])
            {
                int j = indicesInv[i];

                newchapters.swapItemsAt(i, j);
                indicesInv.swapItemsAt(i, j);
            }
    }
    catch (QException &)
    {
        return Err(QString("Couldn't parse manga infos."));
    }

    return Ok(newchapters);
}

Result<QStringList, QString> MangaDex::getPageList(const QString &chapterUrl)
{
    auto job = networkManager->downloadAsString(apiUrl + "/at-home/server/" + chapterUrl);

    if (!job->await(7000))
        return Err(job->errorString);

    QStringList imageUrls;

    try
    {
        Document chapterdoc;
        chapterdoc.Parse(job->buffer.data());

        if (getStringSafe(chapterdoc, "result") == "error")
            return Err(QString("Couldn't parse page list."));

        auto serverBaseUrl = getStringSafe(chapterdoc, "baseUrl");
        if (serverBaseUrl.isEmpty())
            serverBaseUrl = "https://uploads.mangadex.org";

        auto hash = getStringSafe(chapterdoc["chapter"], "hash");

        // Use full quality data first, with data-saver filenames stored for fallback
        auto pages = chapterdoc["chapter"]["data"].GetArray();
        auto saverPages = chapterdoc["chapter"]["dataSaver"].GetArray();

        for (int i = 0; i < (int)pages.Size(); i++)
        {
            // Primary: full quality URL
            imageUrls.append(serverBaseUrl + "/data/" + hash + "/" + pages[i].GetString());
        }

        // Store data-saver URLs as fallback metadata (appended with separator)
        // Format: each full URL has a matching saver URL accessible by replacing /data/ with /data-saver/
        // and the filename with the saver filename
        dataSaverHash = hash;
        dataSaverBaseUrl = serverBaseUrl;
        dataSaverFilenames.clear();
        for (int i = 0; i < (int)saverPages.Size(); i++)
            dataSaverFilenames.append(saverPages[i].GetString());
    }
    catch (QException &)
    {
        return Err(QString("Couldn't parse page list."));
    }

    return Ok(imageUrls);
}

QString MangaDex::getFallbackImageUrl(int pageIndex) const
{
    if (pageIndex >= 0 && pageIndex < dataSaverFilenames.size() &&
        !dataSaverHash.isEmpty() && !dataSaverBaseUrl.isEmpty())
    {
        return dataSaverBaseUrl + "/data-saver/" + dataSaverHash + "/" +
               dataSaverFilenames[pageIndex];
    }
    return {};
}
