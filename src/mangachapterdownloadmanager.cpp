#include "mangachapterdownloadmanager.h"

#include <QStorageInfo>

#include "staticsettings.h"

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
    QString title = currentManga ? currentManga->title : QString();

    cancelled = true;
    running = false;
    downloadQueue.clearQuene();
    downloadJobs.clear();

    // Clean up partially downloaded images for the current manga
    if (currentManga && !currentManga->title.isEmpty())
    {
        auto imgDir = CONF.mangaimagesdir(currentManga->hostname, currentManga->title);
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

    emit downloadCancelled(title);
}

void MangaChapterDownloadManager::downloadQueueJobsCompleted()
{
    QString title = currentManga ? currentManga->title : QString();

    if (failedImages > 0)
        emit error(QString("Download finished with %1 failed images.").arg(failedImages));

    running = false;
    currentManga.clear();
    downloadQueue.resetJobCount();

    emit downloadCompleted(title);
    processNextJob();
}

void MangaChapterDownloadManager::processNextJob()
{
    if (cancelled || downloadJobs.empty() || running)
        return;

    // Check available storage before starting
    QStorageInfo storage(CONF.cacheDir);
    qint64 freeMB = storage.bytesAvailable() / (1024 * 1024);
    if (freeMB < 50)
    {
        emit error(QString("Low storage! Only %1 MB free. Free up space before downloading.").arg(freeMB));
        downloadJobs.clear();
        return;
    }

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

    // Run the actual processing in a single shot timer to return control to the event loop immediately
    QTimer::singleShot(0, this, [this, mangaInfo, fromChapter, toChapterInclusive]() {
        if (cancelled) { running = false; return; }

        // Phase 1: Fetch all page lists
        for (int c = fromChapter; c <= toChapterInclusive && c < mangaInfo->chapters.count() && !cancelled; c++)
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
                    break;
                }
            }
            qApp->processEvents(); // Keep UI alive between chapters
        }

        if (cancelled) { running = false; return; }
        mangaInfo->serialize();

        // Phase 2: Fetch all image URLs
        QList<FileDownloadDescriptor> imageDescriptors;
        for (int c = fromChapter; c <= toChapterInclusive && c < mangaInfo->chapters.count() && !cancelled; c++)
        {
            if (!mangaInfo->chapters[c].pagesLoaded)
                continue;

            // Ensure imageUrlList matches pageUrlList size
            while (mangaInfo->chapters[c].imageUrlList.count() < mangaInfo->chapters[c].pageUrlList.count())
                mangaInfo->chapters[c].imageUrlList.append("");

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
                
                // Process events every few pages to keep UI snappy
                if (p % 10 == 0) qApp->processEvents();
            }
        }

        if (cancelled) { running = false; return; }

        mangaInfo->serialize();
        if (imageDescriptors.isEmpty())
            downloadQueueJobsCompleted();
        else
            downloadQueue.appendDownloads(imageDescriptors);
    });
}

void MangaChapterDownloadManager::downloadMangaChapters(QSharedPointer<MangaInfo> mangaInfo,
                                                         int fromChapter, int toChapterInclusive)
{
    downloadJobs.append(MangaChapterRange(mangaInfo, fromChapter, toChapterInclusive));
    cancelled = false;
    processNextJob();
}
