#include "novelfull.h"

#include <QUrl>

NovelFull::NovelFull(NetworkManager *dm) : AbstractMangaSource(dm)
{
    name = "NovelFull";
    baseUrl = "https://novelfull.net";
    contentType = ContentLightNovel;
}

bool NovelFull::updateMangaList(UpdateProgressToken *token)
{
    // Search-only source — no full catalog crawl
    token->sendProgress(100);
    return true;
}

Result<MangaList, QString> NovelFull::searchManga(const QString &query, int maxResults)
{
    MangaList results;
    results.absoluteUrls = false;

    if (query.isEmpty())
        return Ok(results);

    auto searchUrl = baseUrl + "/search?keyword=" + QString::fromUtf8(QUrl::toPercentEncoding(query));
    auto job = networkManager->downloadAsString(searchUrl, 15000);

    if (!job->await(15000))
        return Err(job->errorString);

    if (job->bufferStr.isEmpty())
        return Err(QString("Empty response from NovelFull."));

    // NovelFull search results: <a href="/novel-name.html" title="Novel Title">
    // inside <div class="list-truyen"> or <div class="row">
    QRegularExpression mangarx(
        R"lit(<h3[^>]*>\s*<a href="(/[^"]*\.html)"\s*[^>]*>([^<]*)</a>)lit");

    int spos = job->bufferStr.indexOf(R"(class="list-truyen")");
    if (spos < 0)
        spos = job->bufferStr.indexOf(R"(class="col-truyen-main")");
    if (spos < 0)
        spos = 0;
    int epos = job->bufferStr.indexOf(R"(class="pagination")", spos);

    QSet<QString> seen;
    for (auto &match : getAllRxMatches(mangarx, job->bufferStr, spos, epos))
    {
        if (results.size >= maxResults)
            break;
        auto url = match.captured(1);
        auto title = htmlToPlainText(match.captured(2));
        if (title.isEmpty() || url.isEmpty())
            continue;
        if (url.contains("/genre/") || url.contains("/search") ||
            url.contains("/contact") || url == "/")
            continue;
        if (seen.contains(url))
            continue;
        seen.insert(url);
        results.append(title, url);
    }

    // Fallback: try broader pattern if no results
    if (results.size == 0)
    {
        QRegularExpression fallbackrx(
            R"lit(<a href="(/[^"]*\.html)"\s*title="([^"]*)")lit");

        for (auto &match : getAllRxMatches(fallbackrx, job->bufferStr, spos, epos))
        {
            if (results.size >= maxResults)
                break;
            auto url = match.captured(1);
            auto title = htmlToPlainText(match.captured(2));
            if (title.isEmpty() || url.isEmpty())
                continue;
            if (url.contains("/genre/") || url.contains("/search") ||
                url.contains("/contact") || url == "/" ||
                url.contains("/chapter-") || url.contains("/volume-"))
                continue;
            if (seen.contains(url))
                continue;
            seen.insert(url);
            results.append(title, url);
        }
    }

    return Ok(results);
}

Result<MangaChapterCollection, QString> NovelFull::updateMangaInfoFinishedLoading(
    QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info)
{
    // Metadata regex patterns
    QRegularExpression authorrx(R"(Author.*?<a[^>]*>([^<]*)</a>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression artistrx;
    QRegularExpression statusrx(R"(Status.*?<a[^>]*>([^<]*)</a>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression yearrx;
    QRegularExpression genresrx(R"(Genre.*?</h3>(.*?)</li>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression summaryrx(R"lit(<div class="desc-text"[^>]*>(.*?)</div>)lit",
                                 QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression coverrx(R"lit(<div class="book">\s*<img[^>]*src="([^"]*)")lit");

    fillMangaInfo(info, job->bufferStr, authorrx, artistrx, statusrx, yearrx,
                  genresrx, summaryrx, coverrx);

    // Fix relative cover URL
    if (!info->coverUrl.isEmpty() && !info->coverUrl.startsWith("http"))
        info->coverUrl = baseUrl + info->coverUrl;

    // Extract chapters — look for chapter list section
    QRegularExpression chapterrx(
        R"lit(<a[^>]*href="(/[^"]*\.html)"[^>]*title="([^"]*)"[^>]*>\s*<span[^>]*class="chapter-text")lit",
        QRegularExpression::DotMatchesEverythingOption);

    int spos = job->bufferStr.indexOf(R"(id="list-chapter")");
    if (spos < 0)
        spos = job->bufferStr.indexOf(R"(class="l-chapters")");
    if (spos < 0)
        spos = 0;

    MangaChapterCollection newchapters;
    QSet<QString> seen;

    for (auto &match : getAllRxMatches(chapterrx, job->bufferStr, spos))
    {
        auto url = match.captured(1);
        auto title = htmlToPlainText(match.captured(2)).trimmed();
        if (seen.contains(url))
            continue;
        seen.insert(url);
        newchapters.append(MangaChapter(title, baseUrl + url));
    }

    // Fallback: broader pattern without span.chapter-text
    if (newchapters.isEmpty())
    {
        QRegularExpression chapterrx2(
            R"lit(<a[^>]*href="(/[^"]*\.html)"[^>]*title="([^"]*)")lit");

        auto slug = info->url;
        slug.remove(".html");
        if (slug.startsWith("/"))
            slug = slug.mid(1);

        for (auto &match : getAllRxMatches(chapterrx2, job->bufferStr, spos))
        {
            auto url = match.captured(1);
            auto title = htmlToPlainText(match.captured(2)).trimmed();
            if (!url.contains(slug + "/"))
                continue;
            if (seen.contains(url))
                continue;
            seen.insert(url);
            newchapters.append(MangaChapter(title, baseUrl + url));
        }
    }

    // Detect chapter ordering (some sites list newest first)
    if (newchapters.size() > 1)
    {
        auto first = newchapters.first().chapterTitle.toLower();
        auto last = newchapters.last().chapterTitle.toLower();
        bool firstIsLater = first.contains("epilogue") || first.contains("final") ||
                            first.contains("afterword");
        bool lastIsEarlier = last.contains("prologue") || last.contains("chapter 1 ") ||
                             last.contains("chapter 1:") || last.endsWith("chapter 1");
        if (firstIsLater || lastIsEarlier)
            std::reverse(newchapters.begin(), newchapters.end());
    }

    return Ok(newchapters);
}

Result<QStringList, QString> NovelFull::getPageList(const QString &chapterUrl)
{
    QStringList pages;
    pages.append(chapterUrl);
    return Ok(pages);
}

Result<QString, QString> NovelFull::getChapterText(const QString &chapterUrl)
{
    try
    {
        auto job = networkManager->downloadAsString(chapterUrl, 15000);

        if (!job->await(15000))
            return Err(job->errorString);

        if (job->bufferStr.isEmpty())
            return Err(QString("Empty response from NovelFull."));

        // Extract chapter text — try multiple content div patterns
        QRegularExpression textrx(
            R"lit(<div[^>]*id="chapter-c"[^>]*>(.*?)</div>\s*<div)lit",
            QRegularExpression::DotMatchesEverythingOption);

        auto match = textrx.match(job->bufferStr);

        if (!match.hasMatch())
        {
            QRegularExpression textrx2(
                R"lit(<div[^>]*class="chapter-c[^"]*"[^>]*>(.*?)</div>\s*(?:<div|<script))lit",
                QRegularExpression::DotMatchesEverythingOption);
            match = textrx2.match(job->bufferStr);
        }

        if (!match.hasMatch())
        {
            QRegularExpression textrx3(
                R"lit(<div[^>]*id="chr-content"[^>]*>(.*?)</div>)lit",
                QRegularExpression::DotMatchesEverythingOption);
            match = textrx3.match(job->bufferStr);
        }

        if (!match.hasMatch())
        {
            QRegularExpression textrx4(
                R"lit(<div[^>]*class="[^"]*chapter[^"]*content[^"]*"[^>]*>(.*?)</div>)lit",
                QRegularExpression::DotMatchesEverythingOption);
            match = textrx4.match(job->bufferStr);
        }

        if (!match.hasMatch())
            return Err(QString("Couldn't extract chapter text."));

        QString rawHtml = match.captured(1);

        // Remove ads, scripts, styles
        rawHtml.remove(QRegularExpression(R"(<script[^>]*>.*?</script>)",
                                           QRegularExpression::DotMatchesEverythingOption));
        rawHtml.remove(QRegularExpression(R"(<style[^>]*>.*?</style>)",
                                           QRegularExpression::DotMatchesEverythingOption));
        rawHtml.remove(QRegularExpression(R"(<div class="ads[^>]*>.*?</div>)",
                                           QRegularExpression::DotMatchesEverythingOption));

        // Convert to plain text
        QString text = rawHtml;
        text.replace(QRegularExpression(R"(<br\s*/?>)"), "\n");
        text.replace(QRegularExpression(R"(</p>)"), "\n\n");
        text.replace(QRegularExpression(R"(<p[^>]*>)"), "");
        text.replace(QRegularExpression(R"(<[^>]*>)"), "");

        text = htmlToPlainText("<span>" + text + "</span>");
        text.replace(QRegularExpression(R"(\n{3,})"), "\n\n");
        text = text.trimmed();

        if (text.isEmpty())
            return Err(QString("Chapter text was empty."));

        return Ok(text);
    }
    catch (...)
    {
        return Err(QString("Error parsing NovelFull chapter."));
    }
}
