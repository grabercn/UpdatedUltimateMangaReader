#include "mangacontroller.h"

#include "mangadex.h"
#include "settings.h"

MangaController::MangaController(NetworkManager *networkManager, QObject *parent)
    : QObject(parent),
      currentIndex(nullptr, 0, 0),
      networkManager(networkManager),
      preloadQueue(networkManager, {}, 1, false)
{
    QObject::connect(&preloadQueue, &DownloadQueue::singleDownloadCompleted, this,
                     &MangaController::completedImagePreload);
}

void MangaController::setCurrentManga(QSharedPointer<MangaInfo> mangaInfo)
{
    cancelAllPreloads();

    if (currentManga.get())
        QObject::disconnect(currentManga.get(), &MangaInfo::chaptersMoved, this,
                            &MangaController::chaptersMoved);

    currentManga.clear();
    currentManga = mangaInfo;
    currentIndex = MangaIndexTraverser(currentManga, 0, 0);
    deserializeProgress();

    QObject::connect(currentManga.get(), &MangaInfo::chaptersMoved, this, &MangaController::chaptersMoved);

    emit currentMangaChanged(mangaInfo);

    auto res = assurePagesLoaded();
    if (res.isOk())
        currentIndexChangedInternal(false);
    else
        emit error(res.unwrapErr());

    emit activity();
}

void MangaController::chaptersMoved(QList<QPair<int, int>>)
{
    deserializeProgress();
    currentIndexChangedInternal(false);
}

Result<void, QString> MangaController::assurePagesLoaded()
{
    if (!currentManga || !currentManga->mangaSource)
        return Err(QString("No manga selected."));

    if (currentManga->chapters.count() == 0)
        return Err(QString("No chapters available."));

    if (currentIndex.chapter >= currentManga->chapters.count() || currentIndex.chapter < 0)
        currentIndex.chapter = qMax(0, currentManga->chapters.count() - 1);

    // Light novels don't need page lists loaded
    if (currentManga->mangaSource->contentType == ContentLightNovel)
        return Ok();

    try
    {
        auto &chapter = currentIndex.currentChapter();
        bool needsLoad = !chapter.pagesLoaded ||
                         chapter.imageUrlList.isEmpty() ||
                         (currentIndex.page < chapter.imageUrlList.count() &&
                          chapter.imageUrlList[currentIndex.page].isEmpty());

        if (needsLoad)
        {
            auto res = currentManga->mangaSource->updatePageList(currentManga, currentIndex.chapter);
            if (!res.isOk())
                return res;
            currentManga->serialize();
        }
    }
    catch (...)
    {
        return Err(QString("Error loading page list."));
    }

    if (currentIndex.chapter >= currentManga->chapters.count() || currentIndex.chapter < 0)
        return Err(QString("Chapter number out of bounds."));

    if (currentIndex.page >= currentIndex.currentChapter().pageUrlList.count())
        currentIndex.page = 0;

    return Ok();
}

void MangaController::setCurrentIndex(const MangaIndex &index)
{
    auto res = currentIndex.setChecked(index.chapter, index.page);
    if (res.isOk())
        currentIndexChangedInternal(true);
    else
        emit error(res.unwrapErr());
}

Result<QString, QString> MangaController::getImageUrl(const MangaIndex &index)
{
    if (!currentManga || !currentManga->mangaSource)
        return Err(QString("No manga selected."));

    if (index.chapter < 0 || index.chapter >= currentManga->chapters.count())
        return Err(QString("Chapter number out of bounds."));

    // Light novels don't have image pages
    if (currentManga->mangaSource->contentType == ContentLightNovel)
        return Err(QString("Light novel - use text reader."));

    if (index.chapter < 0 || index.chapter >= currentManga->chapters.count())
        return Err(QString("Chapter index out of bounds."));

    if (!currentManga->chapters[index.chapter].pagesLoaded)
    {
        currentManga->mangaSource->updatePageList(currentManga, index.chapter);
        currentManga->serialize();
    }

    if (index.page < 0 || index.page >= currentManga->chapters[index.chapter].imageUrlList.count())
        return Err(QString("Page index out of bounds."));

    if (currentManga->chapters[index.chapter].imageUrlList[index.page] == "")
    {
        if (index.page >= currentManga->chapters[index.chapter].pageUrlList.count())
            return Err(QString("Page URL index out of bounds."));
        auto res = currentManga->mangaSource->getImageUrl(
            currentManga->chapters[index.chapter].pageUrlList.at(index.page));
        if (!res.isOk())
            return Err(res.unwrapErr());
        currentManga->chapters[index.chapter].imageUrlList[index.page] = res.unwrap();
    }

    return Ok(currentManga->chapters[index.chapter].imageUrlList[index.page]);
}
void MangaController::currentIndexChangedInternal(bool preload)
{
    if (!currentManga || !currentManga->mangaSource)
        return;

    int pageCount = 1;
    if (currentManga->mangaSource->contentType != ContentLightNovel &&
        currentIndex.chapter >= 0 && currentIndex.chapter < currentManga->chapters.count() &&
        currentManga->chapters[currentIndex.chapter].pagesLoaded)
    {
        pageCount = static_cast<int>(currentIndex.currentChapter().pageUrlList.count());
    }

    emit currentIndexChanged(
        {currentIndex, static_cast<int>(currentManga->chapters.count()), pageCount});

    updateCurrentImage();

    serializeProgress();

    if (preload && currentManga->mangaSource->contentType != ContentLightNovel)
    {
        // Check if preloading is enabled (settings accessed via mangaSource->networkManager)
        QTimer::singleShot(50, this, &MangaController::preloadNeighbours);
    }

    emit activity();
}

void MangaController::updateCurrentImage()
{
    if (!currentManga || !currentManga->mangaSource)
    {
        emit error("No manga selected.");
        return;
    }

    // Check if this is a light novel source
    if (currentManga->mangaSource->contentType == ContentLightNovel)
    {
        if (currentIndex.chapter < 0 || currentIndex.chapter >= currentManga->chapters.count())
        {
            emit error("Chapter out of bounds.");
            return;
        }

        try
        {
            auto chapterUrl = currentManga->chapters[currentIndex.chapter].chapterUrl;
            auto textResult = currentManga->mangaSource->getChapterText(chapterUrl);

            if (textResult.isOk())
            {
                auto title = currentManga->chapters[currentIndex.chapter].chapterTitle;
                emit currentTextChanged(textResult.unwrap(), title);
                return;
            }
            else if (textResult.unwrapErr() == "__PDF_USE_IMAGE_READER__")
            {
                // PDF chapter - load page list and fall through to the image reader
                qDebug() << "PDF chapter detected, using image reader";
                auto pageRes = currentManga->mangaSource->updatePageList(currentManga, currentIndex.chapter);
                if (!pageRes.isOk())
                {
                    emit error("Failed to load PDF pages: " + pageRes.unwrapErr());
                    return;
                }
                currentManga->serialize();
                // Fall through to image reader below
            }
            else
            {
                emit error(textResult.unwrapErr());
                return;
            }
        }
        catch (...)
        {
            emit error("Failed to load chapter text.");
            return;
        }
    }

    // Try up to 3 times - on failure, try CDN fallback then invalidate cached URL
    for (int attempt = 0; attempt < 3; attempt++)
    {
        auto imageUrl = getImageUrl(currentIndex);

        if (!imageUrl.isOk())
        {
            if (attempt >= 2)
            {
                emit currentImageChanged("error");
                emit error(imageUrl.unwrapErr());
            }
            return;
        }

        auto dd = DownloadImageDescriptor(imageUrl.unwrap(), currentManga->title,
                                          currentIndex.chapter, currentIndex.page);

        auto imagePath = currentManga->mangaSource->downloadAwaitImage(dd);

        if (imagePath.isOk())
        {
            autoRetryCount = 0;
            emit currentImageChanged(imagePath.unwrap());
            return;
        }

        // On first failure, try MangaDex data-saver CDN fallback before full retry
        if (attempt == 0)
        {
            auto *mangadex = dynamic_cast<MangaDex *>(currentManga->mangaSource);
            if (mangadex)
            {
                auto fallbackUrl = mangadex->getFallbackImageUrl(currentIndex.page);
                if (!fallbackUrl.isEmpty())
                {
                    qDebug() << "Trying MangaDex data-saver fallback for page" << currentIndex.page;
                    auto fallbackDd = DownloadImageDescriptor(fallbackUrl, currentManga->title,
                                                              currentIndex.chapter, currentIndex.page);
                    auto fallbackPath = currentManga->mangaSource->downloadAwaitImage(fallbackDd);
                    if (fallbackPath.isOk())
                    {
                        emit currentImageChanged(fallbackPath.unwrap());
                        return;
                    }
                }
            }
        }

        // Failed - invalidate the cached image URL so next attempt gets a fresh one
        if (currentIndex.chapter >= 0 && currentIndex.chapter < currentManga->chapters.count())
        {
            auto &ch = currentManga->chapters[currentIndex.chapter];
            if (currentIndex.page < ch.imageUrlList.count())
                ch.imageUrlList[currentIndex.page] = "";
            // Also invalidate the page list so getPageList re-fetches from the server
            if (attempt >= 1)
                ch.pagesLoaded = false;
        }

        qDebug() << "Image load failed, retry" << (attempt + 1);
    }

    emit currentImageChanged("error");

    // Auto-retry up to 3 times with increasing delay
    if (autoRetryCount < 3)
    {
        autoRetryCount++;
        int delay = autoRetryCount * 5000;  // 5s, 10s, 15s
        emit error(QString("Image failed to load. Retry %1/3 in %2s...")
                       .arg(autoRetryCount).arg(delay / 1000));

        QTimer::singleShot(delay, this, [this]() {
            if (!currentManga || !currentManga->mangaSource)
                return;
            qDebug() << "Auto-retry" << autoRetryCount << "for page" << currentIndex.page;
            updateCurrentImage();
        });
    }
    else
    {
        emit error("Image failed after all retries. Tap to try again.");
        autoRetryCount = 0;
    }
}

void MangaController::advanceMangaPage(PageTurnDirection direction)
{
    if (!currentManga)
    {
        emit indexMovedOutOfBounds();
        return;
    }

    bool inbound = false;
    if (direction == Forward)
    {
        auto res = currentIndex.increment();
        if (res.isOk())
        {
            inbound = res.unwrap();
        }
        else
        {
            emit error(res.unwrapErr());
            return;
        }
    }
    else  // if (direction == backward)
    {
        auto res = currentIndex.decrement();
        if (res.isOk())
        {
            inbound = res.unwrap();
        }
        else
        {
            emit error(res.unwrapErr());
            return;
        }
    }

    if (inbound)
        currentIndexChangedInternal(true);
    else
        emit indexMovedOutOfBounds();
}

void MangaController::preloadImage(const MangaIndex &index)
{
    if (!currentManga || !currentManga->mangaSource)
        return;

    // Don't preload images for light novel sources
    if (currentManga->mangaSource->contentType == ContentLightNovel)
        return;

    auto imageUrl = getImageUrl(index);

    if (!imageUrl.isOk())
    {
        emit error(imageUrl.unwrapErr());
        return;
    }

    DownloadImageDescriptor imageinfo(imageUrl.unwrap(), currentManga->title, index.chapter, index.page);
    auto path = currentManga->mangaSource->getImagePath(imageinfo);

    if (QFile::exists(path))
        return;

    preloadQueue.appendDownload(FileDownloadDescriptor(imageUrl.unwrap(), path));
}

void MangaController::preloadPopular()
{
    if (!currentManga || !currentManga->mangaSource)
        return;
    if (currentManga->mangaSource->contentType == ContentLightNovel)
        return;
    if (currentManga->chapters.count() == 0)
        return;

    if (currentManga->chapters.count() > 1 && currentIndex.chapter != currentManga->chapters.count() - 1)
        preloadImage({static_cast<int>(currentManga->chapters.count() - 1), 0});
}

void MangaController::preloadNeighbours()
{
    if (settings && !settings->preloadEnabled)
        return;

    int forward = settings ? settings->preloadPages : CONF.forwardPreloads;
    int backward = CONF.backwardPreloads;

    MangaIndexTraverser forwardindex(currentIndex);
    MangaIndexTraverser backwardindex(currentIndex);

    for (int i = 0; i < qMax(forward, backward); i++)
    {
        if (i < forward)
        {
            auto res = forwardindex.increment();
            if (res.isOk() && res.unwrap())
                preloadImage(forwardindex);
        }

        if (i < backward)
        {
            auto res = backwardindex.decrement();
            if (res.isOk() && res.unwrap())
                preloadImage(backwardindex);
        }
    }

    // Auto-download: pre-fetch page lists for next N chapters
    int chaptersAhead = settings ? settings->preloadChapters : 2;
    if (currentManga && currentManga->mangaSource &&
        currentManga->mangaSource->contentType != ContentLightNovel)
    {
        for (int c = currentIndex.chapter + 1;
             c <= currentIndex.chapter + chaptersAhead && c < currentManga->chapters.count(); c++)
        {
            if (!currentManga->chapters[c].pagesLoaded)
            {
                try
                {
                    currentManga->mangaSource->updatePageList(currentManga, c);
                }
                catch (...) {}
            }
        }
    }
}

void MangaController::cancelAllPreloads()
{
    preloadQueue.clearQuene();
}

void MangaController::completedImagePreload(const QString &, const QString &path)
{
    emit completedImagePreloadSignal(path);
}

void MangaController::serializeProgress()
{
    ReadingProgress c(currentIndex, currentManga->chapters.count(),
                      currentIndex.currentChapter().pageUrlList.count());
    c.serialize(currentManga->hostname, currentManga->title);
}

void MangaController::deserializeProgress()
{
    ReadingProgress progress(currentManga->hostname, currentManga->title);
    currentIndex.chapter = progress.index.chapter;
    currentIndex.page = progress.index.page;
}
