#include "internetarchive.h"

#include <QUrl>

using namespace rapidjson;

InternetArchive::InternetArchive(NetworkManager *dm, const QString &sourceName,
                                 const QString &subjectFilter, ContentType type)
    : AbstractMangaSource(dm), subjectFilter(subjectFilter)
{
    name = sourceName;
    baseUrl = "https://archive.org";
    searchApiUrl = "https://archive.org/advancedsearch.php";
    metadataUrl = "https://archive.org/metadata/";
    downloadUrl = "https://archive.org/download/";
    contentType = type;
}

bool InternetArchive::updateMangaList(UpdateProgressToken *token)
{
    token->sendProgress(100);
    return true;
}

Result<MangaList, QString> InternetArchive::searchManga(const QString &query, int maxResults)
{
    MangaList results;
    results.absoluteUrls = false;

    if (query.isEmpty())
        return Ok(results);

    auto encodedQuery = QString::fromUtf8(QUrl::toPercentEncoding(query));
    auto url = searchApiUrl + "?q=" + encodedQuery;
    if (!subjectFilter.isEmpty())
        url += "+subject%3A(" + QString::fromUtf8(QUrl::toPercentEncoding(subjectFilter)) + ")";
    url += "+mediatype%3A(texts)"
           "&fl%5B%5D=identifier&fl%5B%5D=title"
           "&sort%5B%5D=downloads+desc"
           "&rows=" + QString::number(maxResults) +
           "&output=json";

    auto job = networkManager->downloadAsString(url, -1);

    if (!job->await(10000))
        return Err(job->errorString);

    try
    {
        Document doc;
        ParseResult res = doc.Parse(job->buffer.data());
        if (!res)
            return Err(QString("Couldn't parse search results."));

        if (!doc.HasMember("response") || !doc["response"].IsObject() ||
            !doc["response"].HasMember("docs") || !doc["response"]["docs"].IsArray())
            return Ok(results);
        auto &docs = doc["response"]["docs"];

        for (const auto &item : docs.GetArray())
        {
            if (!item.HasMember("identifier") || !item.HasMember("title"))
                continue;
            results.append(QString(item["title"].GetString()),
                           QString(item["identifier"].GetString()));
        }
    }
    catch (QException &)
    {
        return Err(QString("Couldn't parse search results."));
    }

    return Ok(results);
}

Result<QSharedPointer<MangaInfo>, QString> InternetArchive::getMangaInfo(const QString &mangaUrl,
                                                                         const QString &mangaTitle)
{
    auto identifier = mangaUrl;
    if (identifier.startsWith(baseUrl))
        identifier = identifier.mid(baseUrl.length());
    if (identifier.startsWith("/"))
        identifier = identifier.mid(1);

    auto fullUrl = metadataUrl + identifier;
    auto job = networkManager->downloadAsString(fullUrl, -1);

    auto info = QSharedPointer<MangaInfo>(new MangaInfo(nullptr));
    info->mangaSource = this;
    info->mangaSource = this;
    info->hostname = name;
    info->url = identifier;
    info->title = mangaTitle;

    if (!job->await(15000))
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

Result<MangaChapterCollection, QString> InternetArchive::updateMangaInfoFinishedLoading(
    QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info)
{
    MangaChapterCollection newchapters;

    try
    {
        Document doc;
        ParseResult res = doc.Parse(job->buffer.data());
        if (!res)
            return Err(QString("Couldn't parse Internet Archive metadata."));

        if (!doc.HasMember("metadata") || !doc["metadata"].IsObject())
            return Err(QString("No metadata in Internet Archive response."));
        auto &metadata = doc["metadata"];

        if (metadata.HasMember("creator") && !metadata["creator"].IsNull())
        {
            if (metadata["creator"].IsString())
                info->author = QString(metadata["creator"].GetString());
            else if (metadata["creator"].IsArray() && metadata["creator"].GetArray().Size() > 0)
                info->author = QString(metadata["creator"].GetArray()[0].GetString());
        }

        if (metadata.HasMember("description") && !metadata["description"].IsNull())
        {
            if (metadata["description"].IsString())
                info->summary = htmlToPlainText(QString(metadata["description"].GetString()));
        }

        if (metadata.HasMember("date") && !metadata["date"].IsNull())
            info->releaseYear = QString(metadata["date"].GetString()).left(4);

        if (metadata.HasMember("subject") && !metadata["subject"].IsNull())
        {
            if (metadata["subject"].IsString())
                info->genres = QString(metadata["subject"].GetString());
            else if (metadata["subject"].IsArray())
            {
                QStringList genres;
                for (const auto &g : metadata["subject"].GetArray())
                    if (g.IsString())
                        genres.append(QString(g.GetString()));
                info->genres = genres.join(" ");
            }
        }

        info->status = "Completed";

        if (!doc.HasMember("files") || !doc["files"].IsArray())
            return Err(QString("No files found."));

        auto files = doc["files"].GetArray();
        auto identifier = info->url;

        // Cover
        for (const auto &file : files)
        {
            if (!file.HasMember("name") || !file["name"].IsString())
                continue;
            auto fname = QString(file["name"].GetString());
            if (fname == "__ia_thumb.jpg" || fname.contains("cover", Qt::CaseInsensitive))
            {
                info->coverUrl = downloadUrl + identifier + "/" + fname;
                break;
            }
        }
        if (info->coverUrl.isEmpty())
            info->coverUrl = QString("https://archive.org/services/img/%1").arg(identifier);

        // Categorize files
        QStringList cbzFiles, pdfFiles, epubFiles, txtFiles, imageFiles;

        for (const auto &file : files)
        {
            if (!file.HasMember("name") || !file["name"].IsString())
                continue;
            auto fname = QString(file["name"].GetString());
            auto fnameLower = fname.toLower();

            if (fnameLower.endsWith(".cbz") || fnameLower.endsWith(".cbr"))
                cbzFiles.append(fname);
            else if (fnameLower.endsWith(".pdf"))
                pdfFiles.append(fname);
            else if (fnameLower.endsWith(".epub"))
                epubFiles.append(fname);
            else if (fnameLower.endsWith(".txt") && !fnameLower.contains("meta"))
                txtFiles.append(fname);
            else if ((fnameLower.endsWith(".jpg") || fnameLower.endsWith(".png") ||
                      fnameLower.endsWith(".jpeg")) && fname != "__ia_thumb.jpg")
                imageFiles.append(fname);
        }

        if (contentType == ContentLightNovel)
        {
            // For light novels: prefer EPUB > PDF > TXT
            QStringList &chapterFiles = !epubFiles.isEmpty() ? epubFiles
                                      : !pdfFiles.isEmpty()  ? pdfFiles
                                      : !txtFiles.isEmpty()  ? txtFiles
                                                             : cbzFiles;

            std::sort(chapterFiles.begin(), chapterFiles.end());

            for (const auto &fname : chapterFiles)
            {
                auto chapterUrl = identifier + "/" + fname;
                auto chapterTitle = fname;
                chapterTitle.remove(QRegularExpression(
                    R"(\.(epub|pdf|txt|cbz|cbr)$)", QRegularExpression::CaseInsensitiveOption));
                newchapters.append(MangaChapter(chapterTitle, chapterUrl));
            }
        }
        else
        {
            // For manga: prefer CBZ > PDF
            QStringList &chapterFiles = !cbzFiles.isEmpty() ? cbzFiles : pdfFiles;

            std::sort(chapterFiles.begin(), chapterFiles.end());

            for (const auto &fname : chapterFiles)
            {
                auto chapterUrl = identifier + "/" + fname;
                auto chapterTitle = fname;
                chapterTitle.remove(QRegularExpression(
                    R"(\.(cbz|cbr|pdf)$)", QRegularExpression::CaseInsensitiveOption));
                newchapters.append(MangaChapter(chapterTitle, chapterUrl));
            }
        }

        // Fallback: treat images as single chapter
        if (newchapters.isEmpty() && !imageFiles.isEmpty())
        {
            std::sort(imageFiles.begin(), imageFiles.end());
            newchapters.append(MangaChapter("Full", identifier + "/__images__"));
        }

        // If still empty and there are any readable files, add them
        if (newchapters.isEmpty())
        {
            QStringList allReadable;
            allReadable << epubFiles << pdfFiles << cbzFiles << txtFiles;
            std::sort(allReadable.begin(), allReadable.end());
            for (const auto &fname : allReadable)
            {
                auto chapterUrl = identifier + "/" + fname;
                auto chapterTitle = fname;
                chapterTitle.remove(QRegularExpression(
                    R"(\.\w{3,4}$)", QRegularExpression::CaseInsensitiveOption));
                newchapters.append(MangaChapter(chapterTitle, chapterUrl));
            }
        }
    }
    catch (QException &)
    {
        return Err(QString("Couldn't parse Internet Archive metadata."));
    }

    return Ok(newchapters);
}

Result<QStringList, QString> InternetArchive::getPageList(const QString &chapterUrl)
{
    QStringList imageUrls;

    if (chapterUrl.endsWith("__images__"))
    {
        auto identifier = chapterUrl.left(chapterUrl.indexOf("/__images__"));
        auto job = networkManager->downloadAsString(metadataUrl + identifier, -1);

        if (!job->await(10000))
            return Err(job->errorString);

        try
        {
            Document doc;
            ParseResult res = doc.Parse(job->buffer.data());
            if (!res || !doc.HasMember("files") || !doc["files"].IsArray())
                return Err(QString("Couldn't parse image list from Archive.org."));
            auto files = doc["files"].GetArray();
            for (const auto &file : files)
            {
                if (!file.HasMember("name") || !file["name"].IsString())
                    continue;
                auto fname = QString(file["name"].GetString());
                auto fl = fname.toLower();
                if ((fl.endsWith(".jpg") || fl.endsWith(".png") || fl.endsWith(".jpeg")) &&
                    fname != "__ia_thumb.jpg")
                    imageUrls.append(downloadUrl + identifier + "/" +
                                     QString::fromUtf8(QUrl::toPercentEncoding(fname, "/")));
            }
            std::sort(imageUrls.begin(), imageUrls.end());
        }
        catch (QException &)
        {
            return Err(QString("Couldn't parse image list."));
        }
    }
    else
    {
        // For CBZ/PDF, use IA's BookReader page image service
        auto parts = chapterUrl.split('/');
        if (parts.size() < 2)
            return Err(QString("Invalid chapter URL."));

        auto identifier = parts[0];

        if (!checkHasPageImages(identifier))
            return Err(QString("This PDF has no page images on Archive.org. Use download to save it."));

        // Try to get page count from page_numbers.json
        int pageCount = 0;
        auto metaJob = networkManager->downloadAsString(metadataUrl + identifier, -1);
        if (metaJob->await(15000))
        {
            try
            {
                Document doc;
                ParseResult pres = doc.Parse(metaJob->buffer.data());
                if (pres && doc.HasMember("files") && doc["files"].IsArray())
                {
                    for (const auto &file : doc["files"].GetArray())
                    {
                        if (!file.HasMember("name") || !file["name"].IsString())
                            continue;
                        auto fname = QString(file["name"].GetString());
                        if (fname.endsWith("_page_numbers.json"))
                        {
                            auto pnJob = networkManager->downloadAsString(
                                downloadUrl + identifier + "/" +
                                QString::fromUtf8(QUrl::toPercentEncoding(fname, "/")), -1);
                            if (pnJob->await(10000))
                            {
                                Document pnDoc;
                                pnDoc.Parse(pnJob->buffer.data());
                                if (pnDoc.IsArray())
                                    pageCount = pnDoc.GetArray().Size();
                            }
                            break;
                        }
                    }
                }
            }
            catch (...) {}
        }

        if (pageCount <= 0)
            pageCount = 300;  // fallback

        qDebug() << "IA page images available, page count:" << pageCount << "for" << identifier;

        for (int i = 0; i < pageCount; i++)
        {
            imageUrls.append(QString("https://archive.org/download/%1/page/n%2_medium.jpg")
                                 .arg(identifier).arg(i));
        }
    }

    return Ok(imageUrls);
}

Result<QString, QString> InternetArchive::getChapterText(const QString &chapterUrl)
{
    // For IA light novels, chapters are files (EPUB/PDF/TXT)
    // We can only easily read TXT files directly
    // For EPUB/PDF, direct the user to download instead
    auto parts = chapterUrl.split('/');
    if (parts.size() < 2)
        return Err(QString("Invalid chapter URL."));

    auto identifier = parts[0];
    auto filename = parts.mid(1).join('/');
    auto filenameLower = filename.toLower();

    if (filenameLower.endsWith(".txt"))
    {
        auto fullUrl = downloadUrl + chapterUrl;
        auto job = networkManager->downloadAsString(fullUrl, -1);

        if (!job->await(15000))
            return Err(job->errorString);

        auto text = job->bufferStr.trimmed();
        if (text.isEmpty())
            return Err(QString("Text file was empty."));

        return Ok(text);
    }

    // For PDF files, try to use image reader for items with page images
    if (filenameLower.endsWith(".pdf"))
    {
        if (checkHasPageImages(identifier))
            return Err(QString("__PDF_USE_IMAGE_READER__"));
        return Err(QString("This PDF cannot be read inline. Use the download button to save it."));
    }

    // For EPUB, we can't render inline easily
    return Err(QString("This chapter is an EPUB file. Use the download button to save it to your device."));
}

bool InternetArchive::checkHasPageImages(const QString &identifier) const
{
    if (hasPageImagesCache.contains(identifier))
        return hasPageImagesCache[identifier];

    bool result = false;
    auto metaJob = networkManager->downloadAsString(metadataUrl + identifier, -1);
    if (metaJob->await(15000))
    {
        try
        {
            Document doc;
            ParseResult pres = doc.Parse(metaJob->buffer.data());
            if (pres && doc.HasMember("files") && doc["files"].IsArray())
            {
                for (const auto &file : doc["files"].GetArray())
                {
                    if (!file.HasMember("name") || !file["name"].IsString())
                        continue;
                    if (QString(file["name"].GetString()).endsWith("_jp2.zip"))
                    {
                        result = true;
                        break;
                    }
                }
            }
        }
        catch (...) {}
    }

    hasPageImagesCache[identifier] = result;
    return result;
}

bool InternetArchive::isDownloadOnly(const QString &chapterUrl)
{
    auto parts = chapterUrl.split('/');
    if (parts.size() < 2)
        return true;

    auto identifier = parts[0];
    auto filename = parts.mid(1).join('/').toLower();

    // TXT files can always be read inline
    if (filename.endsWith(".txt"))
        return false;

    // PDFs with page images can be read inline
    if (filename.endsWith(".pdf") && checkHasPageImages(identifier))
        return false;

    // Everything else (raw PDFs, EPUBs) is download-only
    return true;
}
