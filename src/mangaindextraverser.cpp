#include "mangaindextraverser.h"

MangaIndexTraverser::MangaIndexTraverser(QSharedPointer<MangaInfo> mangainfo, int chapter, int page)
    : MangaIndex(chapter, page), mangaInfo(mangainfo)
{
}

Result<bool, QString> MangaIndexTraverser::increment()
{
    if (!mangaInfo || mangaInfo->chapters.isEmpty())
        return Ok(false);

    if (chapter < 0 || chapter >= mangaInfo->chapters.count())
        return Ok(false);

    // For light novels (1 page per chapter), always go to next chapter
    if (mangaInfo->mangaSource && mangaInfo->mangaSource->contentType == ContentLightNovel)
    {
        if (chapter + 1 < mangaInfo->chapters.count())
        {
            this->chapter = chapter + 1;
            this->page = 0;
            return Ok(true);
        }
        return Ok(false);
    }

    if (page + 1 < mangaInfo->chapters.at(chapter).pageUrlList.count())
    {
        return setChecked(chapter, page + 1);
    }
    else if (chapter + 1 < mangaInfo->chapters.count())
    {
        return setChecked(chapter + 1, 0);
    }
    return Ok(false);
}

Result<bool, QString> MangaIndexTraverser::decrement()
{
    if (!mangaInfo || mangaInfo->chapters.isEmpty())
        return Ok(false);

    // For light novels, always go to prev chapter
    if (mangaInfo->mangaSource && mangaInfo->mangaSource->contentType == ContentLightNovel)
    {
        if (chapter > 0)
        {
            this->chapter = chapter - 1;
            this->page = 0;
            return Ok(true);
        }
        return Ok(false);
    }

    if (page > 0)
    {
        return setChecked(chapter, page - 1);
    }
    else if (chapter > 0)
    {
        try
        {
            if (!mangaInfo->chapters.at(chapter - 1).pagesLoaded)
            {
                auto res = mangaInfo->mangaSource->updatePageList(mangaInfo, chapter - 1);
                if (!res.isOk())
                    return Err(res.unwrapErr());
            }
            return setChecked(chapter - 1,
                              qMax(0, mangaInfo->chapters.at(chapter - 1).pageUrlList.count() - 1));
        }
        catch (...)
        {
            return Err(QString("Error loading previous chapter."));
        }
    }
    return Ok(false);
}

Result<bool, QString> MangaIndexTraverser::setChecked(int chapter, int page)
{
    if (!mangaInfo)
        return Ok(false);

    if (chapter < 0 || page < 0 || chapter >= mangaInfo->chapters.count() ||
        (chapter == this->chapter && page == this->page))
        return Ok(false);

    try
    {
        if (!mangaInfo->chapters.at(chapter).pagesLoaded)
        {
            if (!mangaInfo->mangaSource)
                return Err(QString("No manga source."));
            auto res = mangaInfo->mangaSource->updatePageList(mangaInfo, chapter);
            if (!res.isOk())
                return Err(res.unwrapErr());
        }

        if (page >= mangaInfo->chapters.at(chapter).pageUrlList.count())
            return Ok(false);
    }
    catch (...)
    {
        return Err(QString("Error navigating to chapter."));
    }

    this->chapter = chapter;
    this->page = page;

    return Ok(true);
}
