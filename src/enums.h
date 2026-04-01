#ifndef ENUMS_H
#define ENUMS_H

enum PageTurnDirection
{
    Forward,
    Backward
};

enum AdvancePageGestureDirection
{
    Left,
    Right
};

enum AdvancePageHWButton
{
    Down,
    Up
};

enum WidgetTab
{
    HomeTab,
    MangaInfoTab,
    FavoritesTab,
    MangaReaderTab,
    DownloadsTab
};

enum ClearDownloadCacheLevel
{
    ClearImages = 3,
    ClearInfos,
    ClearAll
};

enum MangaOrderMethod
{
    OrderByTitle,
    OrderByPopularity

};

enum DitheringMode
{
    NoDithering = 0,
    HWDithering = 1,
    SWDithering = 2
};

enum DoublePageMode
{
    DoublePageNoRotation,
    DoublePage90CW,
    DoublePage90CCW
};

enum ContentType
{
    ContentManga = 0,
    ContentLightNovel
};

#endif  // ENUMS_H
