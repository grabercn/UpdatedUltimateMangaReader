#include "mangatown.h"

#include <QUrl>

MangaTown::MangaTown(NetworkManager *networkManager) : AbstractMangaSource(networkManager)
{
    name = "MangaTown";
    baseUrl = "https://www.mangatown.com";
    dictionaryUrl = "https://www.mangatown.com/directory/";

    networkManager->addSetCustomRequestHeader("mangatown.com", "Referer", R"(https://www.mangatown.com/)");
    networkManager->addSetCustomRequestHeader("mangahere.org", "Referer", R"(https://www.mangatown.com/)");
    networkManager->addSetCustomRequestHeader("mangahere.com", "Referer", R"(https://www.mangatown.com/)");
}

Result<MangaList, QString> MangaTown::searchManga(const QString &query, int maxResults)
{
    MangaList results;
    results.absoluteUrls = false;

    if (query.isEmpty())
        return Ok(results);

    auto searchUrl = baseUrl + "/search?name=" + QUrl::toPercentEncoding(query);
    auto job = networkManager->downloadAsString(searchUrl);

    if (!job->await(7000))
        return Err(job->errorString);

    QRegularExpression mangarx(
        R"lit(<a class="manga_cover" href="(/manga/[^"]*?)" title="([^"]*?)")lit");

    for (auto &match : getAllRxMatches(mangarx, job->bufferStr))
    {
        if (results.size >= maxResults)
            break;
        results.append(htmlToPlainText(match.captured(2)), match.captured(1));
    }

    return Ok(results);
}

bool MangaTown::updateMangaList(UpdateProgressToken *token)
{
    QRegularExpression mangarx(R"lit(<a class="manga_cover" href="(/manga/[^"]*?)" title="([^"]*?)")lit");

    QRegularExpression numpagesrx(R"(\.\.\.<a href="/directory/(\d{3,4}).htm")");

    auto job = networkManager->downloadAsString(dictionaryUrl + "1.htm");

    if (!job->await(15000))
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
    qDebug() << "pages:" << pages;

    const int matchesPerPage = 30;
    auto lambda = [&](QSharedPointer<DownloadStringJob> job)
    {
        int matches = 0;
        for (auto &match : getAllRxMatches(mangarx, job->bufferStr))
        {
            auto title = htmlToPlainText(match.captured(2));
            auto url = match.captured(1);
            mangas.append(title, url);
            matches++;
        }

        token->sendProgress(10 + 90 * (mangas.size / matchesPerPage) / pages);
        qDebug() << "matches:" << matches;
        if (matches < matchesPerPage)
            qDebug() << "          Incomplete match in page:" << job->url;
    };

    lambda(job);

    QList<QString> urls;
    for (int i = 2; i <= pages; i++)
        urls.append(dictionaryUrl + QString::number(i) + ".htm");

    DownloadQueue queue(networkManager, urls, CONF.parallelDownloadsHigh, lambda, true);
    queue.setCancellationToken(&token->canceled);
    queue.start();
    if (!queue.awaitCompletion())
    {
        token->sendError(queue.lastErrorMessage);
        return false;
    }
    this->mangaList = mangas;

    qDebug() << "mangas:" << mangas.size << "time:" << timer.elapsed();

    token->sendProgress(100);

    return true;
}

Result<MangaChapterCollection, QString> MangaTown::updateMangaInfoFinishedLoading(
    QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info)
{
    QRegularExpression authorrx(R"(<b>Author\(s\):</b>(.*?)<li>)");
    QRegularExpression artistrx(R"(<b>Artist\(s\):</b>(.*?)<li>)");
    QRegularExpression statusrx(R"(<b>Status\(s\):</b>(.*?)(?:<a|<li))");
    QRegularExpression yearrx;
    QRegularExpression genresrx(R"(<b>Genre\(s\):</b>(.*?)</li>)");

    QRegularExpression summaryrx(R"lit(<span id="show"[^>]*?>([^<]*?)<)lit");

    QRegularExpression coverrx(R"lit(<img src="([^"]*?)" onerror="this.src)lit");

    QRegularExpression chapterrx(R"lit(<a href="(/manga/[^"]*?)"[^>]*?>([^<]*))lit");

    fillMangaInfo(info, job->bufferStr, authorrx, artistrx, statusrx, yearrx, genresrx, summaryrx, coverrx);

    int spos = job->bufferStr.indexOf(R"(<ul class="chapter_list">)");
    int epos = job->bufferStr.indexOf(R"(<div class="comment_content">)", spos);

    MangaChapterCollection newchapters;
    for (auto &chapterrxmatch : getAllRxMatches(chapterrx, job->bufferStr, spos, epos))
        newchapters.insert(0, MangaChapter(chapterrxmatch.captured(2), baseUrl + chapterrxmatch.captured(1)));

    return Ok(newchapters);
}

Result<QStringList, QString> MangaTown::getPageList(const QString &chapterUrl)
{
    try
    {
        auto job = networkManager->downloadAsString(chapterUrl);

        if (!job->await(7000))
            return Err(job->errorString);

        if (job->bufferStr.isEmpty())
            return Err(QString("Empty response from MangaTown."));

        // Check if licensed/unavailable
        if (job->bufferStr.contains("not available in MangaTown") ||
            job->bufferStr.contains("has been licensed"))
            return Err(QString("This manga is licensed and not available on MangaTown."));

        // Extract total_pages from JavaScript variable
        QRegularExpression totalPagesRx(R"(var\s+total_pages\s*=\s*(\d+))");
        auto totalPagesMatch = totalPagesRx.match(job->bufferStr);

        if (!totalPagesMatch.hasMatch())
        {
            // Fallback: try the old select/option method
            QRegularExpression numPagesRx(
                R"lit(>(\d+)</option>\s*?(?:<option[^>]*>Featured</option>)?\s*?</select>)lit");
            auto numPagesRxMatch = numPagesRx.match(job->bufferStr);
            if (!numPagesRxMatch.hasMatch())
                return Err(QString("Couldn't process pagelist."));

            int numPages = numPagesRxMatch.captured(1).toInt();
            if (numPages <= 0 || numPages > 1000)
                return Err(QString("Invalid page count: %1").arg(numPages));

            QStringList imageUrls;
            for (int i = 1; i <= numPages; i++)
                imageUrls.append(QString("%1%2.html").arg(chapterUrl).arg(i));
            return Ok(imageUrls);
        }

        int numPages = totalPagesMatch.captured(1).toInt();
        if (numPages <= 0 || numPages > 1000)
            return Err(QString("Invalid page count: %1").arg(numPages));

        // Ensure chapter URL ends with /
        QString baseChapterUrl = chapterUrl;
        if (!baseChapterUrl.endsWith('/'))
            baseChapterUrl += '/';

        QStringList imageUrls;
        for (int i = 1; i <= numPages; i++)
            imageUrls.append(QString("%1%2.html").arg(baseChapterUrl).arg(i));

        return Ok(imageUrls);
    }
    catch (...)
    {
        return Err(QString("Error parsing MangaTown page list."));
    }
}

Result<QString, QString> MangaTown::getImageUrl(const QString &pageUrl)
{
    try
    {
    auto job = networkManager->downloadAsString(pageUrl);

    if (!job->await(6000))
        return Err(job->errorString);

    if (job->bufferStr.isEmpty())
        return Err(QString("Empty page response."));

    // Try JavaScript-based image URL first (newpicurl or similar)
    QRegularExpression jsImgRx(R"lit((?:src|url)\s*[:=]\s*['"]([^'"]*?mangahere\.com[^'"]*?\.(?:jpg|png|webp))['"]\s*[,;])lit");
    auto jsMatch = jsImgRx.match(job->bufferStr);

    if (!jsMatch.hasMatch())
    {
        // Fallback: try the standard img#image tag
        QRegularExpression imgUrlRx(R"lit(<img[^>]*id="image"[^>]*src="([^"]*?)")lit");
        jsMatch = imgUrlRx.match(job->bufferStr);
    }

    if (!jsMatch.hasMatch())
    {
        // Try any large image from mangahere CDN
        QRegularExpression cdnRx(R"lit(["']((?:https?:)?//[^"']*?mangahere[^"']*?\.(?:jpg|png|webp))[^"']*?["'])lit");
        jsMatch = cdnRx.match(job->bufferStr);
    }

    if (!jsMatch.hasMatch())
        return Err(QString("Couldn't process pages/images."));

    auto imageUrl = jsMatch.captured(1);
    if (imageUrl.startsWith("//"))
        imageUrl = "https:" + imageUrl;

    return Ok(imageUrl);
    }
    catch (...)
    {
        return Err(QString("Error parsing MangaTown image URL."));
    }
}
