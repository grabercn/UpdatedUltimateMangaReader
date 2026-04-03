#include "welcomedialog.h"

#include <QFile>
#include <QScreen>

#include "sizes.h"
#include "staticsettings.h"

WelcomeDialog::WelcomeDialog(QWidget *parent)
    : QDialog(parent), currentPage(0)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    if (parent)
        setGeometry(parent->geometry());
    else
    {
        auto *screen = QApplication::primaryScreen();
        if (screen)
            setGeometry(screen->geometry());
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    contentLabel = new QLabel(this);
    contentLabel->setWordWrap(true);
    contentLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    contentLabel->setStyleSheet("padding: 6px;");
    layout->addWidget(contentLabel, 1);

    pageIndicator = new QLabel(this);
    pageIndicator->setAlignment(Qt::AlignCenter);
    pageIndicator->setStyleSheet("color: #888;");
    layout->addWidget(pageIndicator);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    prevBtn = new QPushButton("< Back", this);
    prevBtn->setFixedHeight(SIZES.buttonSize);
    connect(prevBtn, &QPushButton::clicked, this, [this]()
    {
        if (currentPage > 0)
            showPage(currentPage - 1);
    });

    nextBtn = new QPushButton("Next >", this);
    nextBtn->setFixedHeight(SIZES.buttonSize);
    connect(nextBtn, &QPushButton::clicked, this, [this]()
    {
        if (currentPage < totalPages - 1)
            showPage(currentPage + 1);
    });

    actionBtn = new QPushButton("Get Started", this);
    actionBtn->setFixedHeight(SIZES.buttonSize);
    actionBtn->setStyleSheet("font-weight: bold;");
    connect(actionBtn, &QPushButton::clicked, this, [this]()
    {
        markShown();
        accept();
    });

    btnRow->addWidget(prevBtn);
    btnRow->addStretch();
    btnRow->addWidget(nextBtn);
    btnRow->addWidget(actionBtn);
    layout->addLayout(btnRow);

    // Build pages
    pages.append(
        "<h2 style='text-align:center;'>Welcome to<br>Ultimate Manga Reader</h2>"
        "<p style='text-align:center; color:#555;'>Your all-in-one manga and light novel reader</p>"
        "<p>Designed for <b>Kobo e-readers</b> and desktop, this app brings "
        "thousands of manga and light novels to your fingertips.</p>"
        "<p>Read online, download for offline reading, and track your progress "
        "across all your devices.</p>"
    );

    pages.append(
        "<h3>Sources</h3>"
        "<p>Search across multiple sources simultaneously:</p>"
        "<p><b>Manga:</b> MangaDex, MangaPlus, MangaTown, Internet Archive</p>"
        "<p><b>Light Novels:</b> AllNovel, Internet Archive</p>"
        "<p>Results are ranked by relevance and searched in both "
        "English and Japanese names.</p>"
    );

    pages.append(
        "<h3>Reading</h3>"
        "<p><b>Manga:</b> Tap left/right sides of the screen or use "
        "the Kobo page buttons to turn pages.</p>"
        "<p><b>Light Novels:</b> Paginated text reader with "
        "a bottom bar for navigation. Use the Menu button or "
        "arrow keys to navigate.</p>"
        "<p>Your reading progress is saved automatically.</p>"
    );

    pages.append(
        "<h3>AniList Integration</h3>"
        "<p>Connect your AniList account in Settings to:</p>"
        "<p>- Track reading progress automatically<br>"
        "- See your reading list on the home screen<br>"
        "- Sync progress across devices<br>"
        "- Auto-mark manga as completed</p>"
        "<p>Go to <b>Settings > AniList</b> to connect.</p>"
    );

    pages.append(
        "<h3>Downloads & Offline</h3>"
        "<p>Download chapters for offline reading on the go. "
        "The app preloads upcoming pages and chapters as you read.</p>"
        "<p><b>Export to Kobo:</b> Save manga as image folders or "
        "light novels as HTML files readable by the native Kobo reader.</p>"
        "<p>Manage downloads from the download icon in the top bar.</p>"
    );

    pages.append(
        "<h3>Tips</h3>"
        "<p>- <b>Favorites:</b> Star manga to quickly find them later<br>"
        "- <b>Bookmarks:</b> Save a specific page while reading<br>"
        "- <b>Auto-sleep:</b> Configurable in Settings > Power<br>"
        "- <b>History:</b> Access from the menu to see stats and bookmarks<br>"
        "- <b>Color mode:</b> Enable in Settings for color e-readers. "
        "Color images may load slightly slower than greyscale.<br>"
        "- <b>Updates:</b> Check for updates in Settings</p>"
        "<br>"
        "<p style='text-align:center; font-size:12pt;'><b>Ready to start reading?</b></p>"
    );

    totalPages = pages.size();
    showPage(0);
}

void WelcomeDialog::showPage(int page)
{
    currentPage = qBound(0, page, totalPages - 1);
    contentLabel->setText(pages[currentPage]);
    pageIndicator->setText(QString("%1 / %2").arg(currentPage + 1).arg(totalPages));

    prevBtn->setVisible(currentPage > 0);
    nextBtn->setVisible(currentPage < totalPages - 1);
    actionBtn->setVisible(currentPage == totalPages - 1);
}

bool WelcomeDialog::shouldShow()
{
    return !QFile::exists(CONF.cacheDir + "welcome_shown");
}

void WelcomeDialog::markShown()
{
    QDir().mkpath(CONF.cacheDir);
    QFile file(CONF.cacheDir + "welcome_shown");
    file.open(QIODevice::WriteOnly);
    file.write("1");
    file.close();
}
