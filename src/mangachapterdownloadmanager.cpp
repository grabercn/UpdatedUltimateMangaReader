#include "mangachapterdownloadmanager.h"

MangaChapterDownloadManager::MangaChapterDownloadManager(NetworkManager *networkManager, QObject *parent)
    : QObject(parent),
      cancelled(false),
      running(false),
      downloadJobs(),
      networkManager(networkManager),
      downloadQueue(networkManager, {}, 2, false)
{
    connect(&downloadQueue, &DownloadQueue::progress,
            [this](int c, int t, int e) { emit downloadImagesProgress(c, t, e); });

    // Don't spam individual image errors - just count them
    connect(&downloadQueue, &DownloadQueue::singleDownloadFailed,
            [this](const QString &, const QString &) { failedImages++; });

    connect(&downloadQueue, &DownloadQueue::allCompleted, this,
            &MangaChapterDownloadManager::downloadQueueJobsCompleted);
}

void MangaChapterDownloadManager::cancelDownloads()
{
    cancelled = true;
    running = false;
    downloadQueue.clearQuene();
    downloadJobs.clear();

    // Clean up partially downloaded images for the current manga
    if (currentManga && !currentManga->title.isEmpty())
    {
        auto imgDir = CONF.mangaimagesdir(currentManga->hostname, currentManga->title);
        // Only delete images from the chapters we were downloading
        QDir dir(imgDir);
        if (dir.exists())
        {
            for (int c = currentFromChapter; c <= currentToChapter; c++)
            {
                for (const auto &f : dir.entryList(QDir::Files))
                {
                    if (f.startsWith(QString::number(c) + "_"))
                        QFile::remove(dir.absoluteFilePath(f));
                }
            }
        }
        currentManga.clear();
    }

    emit downloadCompleted();
}

void MangaChapterDownloadManager::downloadQueueJobsCompleted()
{
    if (failedImages > 0)
        emit error(QString("Download finished with %1 failed images.").arg(failedImages));

    emit downloadCompleted();
    downloadQueue.resetJobCount();
    running = false;
    currentManga.clear();
    processNextJob();
}

void MangaChapterDownloadManager::processNextJob()
{
    if (cancelled || downloadJobs.empty() || running)
        return;
    running = true;
    failedImages = 0;

    auto job = downloadJobs.dequeue();
    auto mangaInfo = job.mangaInfo;
    auto fromChapter = job.fromChapter;
    auto toChapterInclusive = job.toChapterInclusive;

    currentManga = mangaInfo;
    currentFromChapter = fromChapter;
    currentToChapter = toChapterInclusive;

    emit downloadStart(mangaInfo->title);

    for (int c = fromChapter; c <= toChapterInclusive && !cancelled; c++)
    {
        for (int i = 0; i < 3; i++)
        {
            auto res = mangaInfo->mangaSource->updatePageList(mangaInfo, c);
            if (res.isOk())
            {
                emit downloadPagelistProgress(c + 1 - fromChapter,
                                               toChapterInclusive + 1 - fromChapter);
                break;
            }
            else if (i >= 2)
            {
                emit error(QString("Couldn't get page list for chapter %1").arg(c + 1));
                // Continue to next chapter instead of aborting
                break;
            }
        }
    }

    if (cancelled)
    {
        running = false;
        return;
    }

    mangaInfo->serialize();

    QList<FileDownloadDescriptor> imageDescriptors;
    for (int c = fromChapter; c <= toChapterInclusive && !cancelled; c++)
    {
        if (!mangaInfo->chapters[c].pagesLoaded)
            continue;

        for (int p = 0; p < mangaInfo->chapters[c].pageUrlList.count() && !cancelled; p++)
        {
            if (mangaInfo->chapters[c].imageUrlList[p] == "")
            {
                auto res = mangaInfo->mangaSource->getImageUrl(
                    mangaInfo->chapters.at(c).pageUrlList.at(p));
                if (res.isOk())
                    mangaInfo->chapters[c].imageUrlList[p] = res.unwrap();
                else
                    failedImages++;
            }

            auto &imageUrl = mangaInfo->chapters[c].imageUrlList[p];
            if (imageUrl != "")
            {
                emit downloadPagesProgress(c + 1 - fromChapter,
                                            toChapterInclusive + 1 - fromChapter, failedImages);

                DownloadImageDescriptor imageinfo(imageUrl, mangaInfo->title, c, p);
                auto path = mangaInfo->mangaSource->getImagePath(imageinfo);
                imageDescriptors.append(FileDownloadDescriptor(imageUrl, path));
            }
        }
    }

    if (cancelled)
    {
        running = false;
        return;
    }

    mangaInfo->serialize();
    downloadQueue.appendDownloads(imageDescriptors);
}

void MangaChapterDownloadManager::downloadMangaChapters(QSharedPointer<MangaInfo> mangaInfo,
                                                         int fromChapter, int toChapterInclusive)
{
    downloadJobs.append(MangaChapterRange(mangaInfo, fromChapter, toChapterInclusive));
    cancelled = false;
    processNextJob();
}
