#include "mangafire.h"

#include <QUrl>

MangaFire::MangaFire(NetworkManager *dm) : AbstractMangaSource(dm)
{
    name = "MangaFire";
    baseUrl = "https://mangafire.to";
    filterUrl = baseUrl + "/filter?keyword=&type%5B%5D=manga&sort=most_viewed&page=";

    networkManager->addSetCustomRequestHeader("mangafire.to", "Referer", R"(https://mangafire.to/)");
}

Result<MangaList, QString> MangaFire::searchManga(const QString &query, int maxResults)
{
    MangaList results;
    results.absoluteUrls = false;

    if (query.isEmpty())
        return Ok(results);

    auto searchUrl = baseUrl + "/filter?keyword=" + QUrl::toPercentEncoding(query) + "&page=1";
    auto job = networkManager->downloadAsString(searchUrl);

    if (!job->await(7000))
        return Err(job->errorString);

    QRegularExpression mangarx(R"lit(<a href="/manga/([^"]*)"[^>]*>([^<]*)</a>)lit");

    int spos = job->bufferStr.indexOf(R"(class="unit)");
    int epos = job->bufferStr.indexOf(R"(<ul class="pagination)", spos);

    for (auto &match : getAllRxMatches(mangarx, job->bufferStr, spos, epos))
    {
        if (results.size >= maxResults)
            break;
        auto title = htmlToPlainText(match.captured(2));
        auto url = "/manga/" + match.captured(1);
        results.append(title, url);
    }

    return Ok(results);
}

bool MangaFire::updateMangaList(UpdateProgressToken *token)
{
    QRegularExpression mangarx(
        R"lit(<a href="/manga/([^"]*)"[^>]*>([^<]*)</a>)lit");

    QRegularExpression numpagesrx(R"(page=(\d+)[^>]*>\s*(?:<i[^>]*></i>\s*)?</a>\s*</li>\s*</ul>)");

    auto job = networkManager->downloadAsString(filterUrl + "1");

    if (!job->await(7000))
    {
        token->sendError(job->errorString);
        return false;
    }

    token->sendProgress(10);

    QElapsedTimer timer;
    timer.start();

    auto numpagesrxmatch = numpagesrx.match(job->bufferStr);

    MangaList mangas;
    mangas.absoluteUrls = false;

    int pages = 1;
    if (numpagesrxmatch.hasMatch())
        pages = numpagesrxmatch.captured(1).toInt();
    // Cap pages to avoid extremely long updates
    if (pages > 500)
        pages = 500;

    qDebug() << "MangaFire pages:" << pages;

    const int matchesPerPage = 30;
    auto lambda = [&](QSharedPointer<DownloadStringJob> job)
    {
        int spos = job->bufferStr.indexOf(R"(class="unit)");
        int epos = job->bufferStr.indexOf(R"(<ul class="pagination)", spos);

        int matches = 0;
        for (auto &match : getAllRxMatches(mangarx, job->bufferStr, spos, epos))
        {
            auto title = htmlToPlainText(match.captured(2));
            auto url = "/manga/" + match.captured(1);
            mangas.append(title, url);
            matches++;
        }

        token->sendProgress(10 + 90 * (mangas.size / matchesPerPage) / pages);
        qDebug() << "MangaFire matches:" << matches;
    };

    lambda(job);

    QList<QString> urls;
    for (int i = 2; i <= pages; i++)
        urls.append(filterUrl + QString::number(i));

    DownloadQueue queue(networkManager, urls, CONF.parallelDownloadsLow, lambda, true);
    queue.setCancellationToken(&token->canceled);
    queue.start();
    if (!queue.awaitCompletion())
    {
        token->sendError(queue.lastErrorMessage);
        return false;
    }
    this->mangaList = mangas;

    qDebug() << "MangaFire mangas:" << mangas.size << "time:" << timer.elapsed();

    token->sendProgress(100);

    return true;
}

Result<QSharedPointer<MangaInfo>, QString> MangaFire::getMangaInfo(const QString &mangaUrl,
                                                                   const QString &mangaTitle)
{
    // mangaUrl is relative like /manga/one-piecee.dkw, prepend baseUrl
    auto fullUrl = mangaUrl.startsWith("http") ? mangaUrl : baseUrl + mangaUrl;
    auto job = networkManager->downloadAsString(fullUrl, 2000);

    auto info = QSharedPointer<MangaInfo>(new MangaInfo(this));
    info->mangaSource = this;
    info->hostname = name;
    info->url = mangaUrl;
    info->title = mangaTitle;

    if (!job->await(7000))
        return Err(job->errorString);

    int oldnumchapters = info->chapters.count();
    auto res = updateMangaInfoFinishedLoading(job, info);
    if (res.isErr())
        return Err(res.unwrapErr());

    auto newChapters = res.unwrap();
    auto moveMapping = info->chapters.mergeChapters(newChapters);

    if (!moveMapping.empty())
    {
        info->updated = true;
        removeChapterPages(info, moveMapping);
        reorderChapterPages(info, moveMapping);
    }

    info->updateCompeted(info->chapters.count() > oldnumchapters, moveMapping);
    downloadCoverAsync(info);

    return Ok(info);
}

Result<MangaChapterCollection, QString> MangaFire::updateMangaInfoFinishedLoading(
    QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info)
{
    QRegularExpression authorrx(R"(Author:</span>\s*<span>\s*<a[^>]*>([^<]*)</a>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression artistrx;
    QRegularExpression statusrx(R"(<span>Status:</span>\s*<span>([^<]*)</span>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression yearrx(R"(Published:</span>\s*<span>\s*([A-Za-z]+ \d+, \d+))",
                              QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression genresrx(R"(Genres:</span>\s*<span>(.*?)</span>\s*</div>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression summaryrx(R"lit(<div class="modal-body">\s*<p>(.*?)</p>)lit",
                                 QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression coverrx(R"lit(<img[^>]*class="poster"[^>]*src="([^"]*)")lit");

    QRegularExpression chapterrx(
        R"lit(<a[^>]*href="(/read/[^"]*)"[^>]*>\s*<span>([^<]*)</span>)lit",
        QRegularExpression::DotMatchesEverythingOption);

    fillMangaInfo(info, job->bufferStr, authorrx, artistrx, statusrx, yearrx, genresrx, summaryrx, coverrx);

    // Extract year from published date
    QRegularExpression yearExtract(R"((\d{4}))");
    auto yearMatch = yearExtract.match(info->releaseYear);
    if (yearMatch.hasMatch())
        info->releaseYear = yearMatch.captured(1);

    int spos = job->bufferStr.indexOf(R"(id="chapter-list")");
    int epos = job->bufferStr.indexOf(R"(<div class="sidebar")", spos);
    if (epos < 0)
        epos = job->bufferStr.indexOf(R"(id="disqus_thread")", spos);

    MangaChapterCollection newchapters;
    for (auto &chapterrxmatch : getAllRxMatches(chapterrx, job->bufferStr, spos, epos))
        newchapters.insert(0, MangaChapter(chapterrxmatch.captured(2), baseUrl + chapterrxmatch.captured(1)));

    return Ok(newchapters);
}

Result<QStringList, QString> MangaFire::getPageList(const QString &chapterUrl)
{
    // MangaFire loads images via AJAX. The chapter page has a syncData JSON block
    // with manga_id. Images are loaded from /ajax/read/{manga_slug}/en/chapter-{n}
    // However, the actual image URLs require JS execution.
    // Alternative: parse the page source for embedded image data
    QRegularExpression imagerx(R"lit(data-src="([^"]*(?:\.jpg|\.png|\.webp)[^"]*)")lit");

    auto job = networkManager->downloadAsString(chapterUrl);

    if (!job->await(7000))
        return Err(job->errorString);

    QStringList imageUrls;

    // Try to find images in the page
    for (auto &match : getAllRxMatches(imagerx, job->bufferStr))
    {
        auto url = match.captured(1);
        if (!url.contains("logo") && !url.contains("favicon") && !url.contains("avatar"))
            imageUrls.append(url);
    }

    // If no data-src images found, try regular img src with CDN URLs
    if (imageUrls.isEmpty())
    {
        QRegularExpression cdnImgRx(R"lit(<img[^>]*src="(https?://[^"]*(?:mfcdn|cdn)[^"]*(?:\.jpg|\.png|\.webp)[^"]*)")lit");
        for (auto &match : getAllRxMatches(cdnImgRx, job->bufferStr))
        {
            auto url = match.captured(1);
            if (!url.contains("logo") && !url.contains("favicon"))
                imageUrls.append(url);
        }
    }

    if (imageUrls.isEmpty())
        return Err(QString("Couldn't find page images. MangaFire may require JavaScript."));

    return Ok(imageUrls);
}
