#include "mangareaderwidget.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QVBoxLayout>

#include "ui_mangareaderwidget.h"

#ifdef KOBO
#include "koboplatformfunctions.h"
#endif

MangaReaderWidget::MangaReaderWidget(QWidget *parent)
    : QWidget(parent), ui(new Ui::MangaReaderWidget), imgcache(),
      isTextMode(false), textCurrentPage(0), settings(nullptr)
{
    ui->setupUi(this);
    adjustUI();

    setAttribute(Qt::WA_AcceptTouchEvents, true);

    ui->readerFrontLightBar->setVisible(false);
    ui->readerNavigationBar->setVisible(false);

    // Paginated text reader for light novels
    textReader = new QTextBrowser(this);
    textReader->setStyleSheet(
        "QTextBrowser { background-color: white; color: #111; "
        "font-family: 'Georgia','Noto Serif',serif; "
        "padding: 10px 12px; border: none; }");
    textReader->setOpenExternalLinks(false);
    textReader->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textReader->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textReader->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    textReader->setTextInteractionFlags(Qt::NoTextInteraction);
    textReader->hide();

    // Bottom bar: progress + page info + menu button
    textBottomBar = new QWidget(this);
    textBottomBar->setFixedHeight(SIZES.buttonSize + 4);
    textBottomBar->hide();

    auto *barLayout = new QHBoxLayout(textBottomBar);
    barLayout->setSpacing(10);
    barLayout->setContentsMargins(12, 0, 12, 0);

    textProgressBar = new QProgressBar(textBottomBar);
    textProgressBar->setRange(0, 100);
    textProgressBar->setValue(0);
    textProgressBar->setFixedHeight(SIZES.batteryIconHeight / 2);
    textProgressBar->setTextVisible(false);
    textProgressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #bbb; border-radius: 3px; background: #eee; }"
        "QProgressBar::chunk { background: #333; border-radius: 2px; }");

    textPageLabel = new QLabel("1/1", textBottomBar);
    textPageLabel->setStyleSheet("color: #444;");
    textPageLabel->setFixedWidth(SIZES.buttonSize * 2);
    textPageLabel->setAlignment(Qt::AlignCenter);

    textMenuBtn = new QPushButton("Menu", textBottomBar);
    textMenuBtn->setFixedHeight(SIZES.buttonSize);
    textMenuBtn->setFocusPolicy(Qt::NoFocus);
    connect(textMenuBtn, &QPushButton::clicked, this, [this]()
    {
        qDebug() << "Menu button clicked. NavBar visible:" << ui->readerNavigationBar->isVisible()
                 << "NavBar parent:" << ui->readerNavigationBar->parentWidget()
                 << "isTextMode:" << isTextMode;
        showMenuBar(!ui->readerNavigationBar->isVisible());
        qDebug() << "After showMenuBar. NavBar visible:" << ui->readerNavigationBar->isVisible()
                 << "NavBar geometry:" << ui->readerNavigationBar->geometry();
    });

    barLayout->addWidget(textProgressBar, 1);
    barLayout->addWidget(textPageLabel);
    barLayout->addWidget(textMenuBtn);

    gotodialog = new GotoDialog(this);

    // Add Bookmark button to reader nav bar
    auto *navLayout = qobject_cast<QHBoxLayout *>(ui->labelReaderChapter->parentWidget()->layout());
    if (navLayout)
    {
        auto *bmBtn = new QPushButton("Bookmark", this);
        bmBtn->setFixedHeight(SIZES.buttonSize);
        bmBtn->setProperty("type", "borderless");
        bmBtn->setFocusPolicy(Qt::NoFocus);
        bmBtn->setStyleSheet("");
        connect(bmBtn, &QPushButton::clicked, this, [this]() { emit bookmarkRequested(); });
        navLayout->addWidget(bmBtn);
    }

    connect(ui->toolButtonLessLight, &QToolButton::clicked,
            [this]() { ui->horizontalSliderLight->setValue(ui->horizontalSliderLight->value() - 1); });
    connect(ui->toolButtonMoreLight, &QToolButton::clicked,
            [this]() { ui->horizontalSliderLight->setValue(ui->horizontalSliderLight->value() + 1); });
    connect(ui->toolButtonLessComfLight, &QToolButton::clicked,
            [this]()
            { ui->horizontalSliderComfLight->setValue(ui->horizontalSliderComfLight->value() - 1); });
    connect(ui->toolButtonMoreComfLight, &QToolButton::clicked,
            [this]()
            { ui->horizontalSliderComfLight->setValue(ui->horizontalSliderComfLight->value() + 1); });

#ifdef DESKTOP
    QGestureRecognizer::registerRecognizer(new TapGestureRecognizer());
#endif

    QGestureRecognizer::registerRecognizer(new SwipeGestureRecognizer());
    QGestureRecognizer::registerRecognizer(new LongPressGestureRecognizer());
    grabGesture(Qt::GestureType::TapGesture);
    grabGesture(Qt::GestureType::SwipeGesture);
    grabGesture(Qt::GestureType::TapAndHoldGesture);
}

MangaReaderWidget::~MangaReaderWidget()
{
    imgcache.clear();
    delete ui;
}

void MangaReaderWidget::adjustUI()
{
    ui->pushButtonReaderBack->setProperty("type", "borderless");
    ui->pushButtonReaderFavorites->setProperty("type", "borderless");
    ui->pushButtonReaderHome->setProperty("type", "borderless");

    ui->pushButtonReaderBack->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonReaderFavorites->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonReaderHome->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonReaderGoto->setFixedHeight(SIZES.buttonSize);

    // Reorder buttons: Back | Home | Favorites (matching bottom bar everywhere else)
    auto *btnLayout = qobject_cast<QHBoxLayout *>(ui->pushButtonReaderHome->parentWidget()->layout());
    if (btnLayout)
    {
        // Remove all buttons and separators
        QList<QWidget *> widgets;
        while (btnLayout->count() > 0)
        {
            auto *item = btnLayout->takeAt(0);
            if (item->widget())
                widgets.append(item->widget());
            delete item;
        }
        // Re-add in correct order: Back, separator, Home, separator, Favorites
        btnLayout->addWidget(ui->pushButtonReaderBack);
        for (auto *w : widgets)
            if (qobject_cast<QFrame *>(w) && static_cast<QFrame *>(w)->frameShape() == QFrame::VLine)
                { btnLayout->addWidget(w); break; }
        btnLayout->addWidget(ui->pushButtonReaderHome);
        for (auto *w : widgets)
            if (qobject_cast<QFrame *>(w) && static_cast<QFrame *>(w)->frameShape() == QFrame::VLine
                && w->parent() != nullptr)
                { btnLayout->addWidget(w); break; }
        btnLayout->addWidget(ui->pushButtonReaderFavorites);
    }

    ui->horizontalSliderLight->setFixedHeight(SIZES.resourceIconSize);
    ui->horizontalSliderComfLight->setFixedHeight(SIZES.resourceIconSize);

    ui->labelTime->setStyleSheet("");

    ui->toolButtonLessLight->setFixedSize(QSize(SIZES.lightIconSize, SIZES.lightIconSize));
    ui->toolButtonMoreLight->setFixedSize(QSize(SIZES.lightIconSize, SIZES.lightIconSize));
    ui->toolButtonLessComfLight->setFixedSize(QSize(SIZES.lightIconSize, SIZES.lightIconSize));
    ui->toolButtonMoreComfLight->setFixedSize(QSize(SIZES.lightIconSize, SIZES.lightIconSize));

    ui->horizontalSliderComfLight->setFixedHeight(SIZES.frontlightSliderHandleHeight);
    ui->horizontalSliderLight->setFixedHeight(SIZES.frontlightSliderHandleHeight);

    ui->horizontalSliderComfLight->setInvertedAppearance(true);
}

bool MangaReaderWidget::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture)
        return gestureEvent(static_cast<QGestureEvent *>(event));
    else if (event->type() == QEvent::KeyPress)
        return buttonPressEvent(static_cast<QKeyEvent *>(event));
    return QWidget::event(event);
}

bool MangaReaderWidget::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    Q_UNUSED(event);
    return false;
}

bool MangaReaderWidget::buttonPressEvent(QKeyEvent *event)
{
    if (isTextMode)
    {
        if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_Down ||
            event->key() == Qt::Key_Right)
        {
            textPageForward();
        }
        else if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_Up ||
                 event->key() == Qt::Key_Left)
        {
            textPageBack();
        }
        else
            return false;

        return true;
    }

    // Manga mode: page buttons and arrow keys advance pages
    if (!settings)
        return false;
    if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_Left)
        emit advancPageClicked(
            conditionalReverse(Forward, settings->buttonAdvance != AdvancePageHWButton::Up));
    else if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_Right)
        emit advancPageClicked(
            conditionalReverse(Forward, settings->buttonAdvance != AdvancePageHWButton::Down));
    else
        return false;

    return true;
}

bool MangaReaderWidget::gestureEvent(QGestureEvent *event)
{
    if (!settings)
        return false;

    if (QGesture *gesture = event->gesture(Qt::SwipeGesture))
    {
        if (gesture->state() != Qt::GestureFinished)
            return true;

        auto swipe = static_cast<QSwipeGesture *>(gesture);

        auto pos = this->mapFromGlobal(gesture->hotSpot().toPoint());
        auto angle = swipe->swipeAngle();

        // Improved swipe detection: require a minimum "fast" movement
        // (velocity logic handled in recognizer, here we just filter by angle)

        if (ui->readerNavigationBar->isVisible())
        {
            if (pos.y() > this->height() * SIZES.readerBottomMenuThreshold * 2)
                showMenuBar(false);
        }
        else if (angle > 155 && angle < 205)
        {
            emit advancPageClicked(
                conditionalReverse(Forward, settings->swipeAdvance != AdvancePageGestureDirection::Left));
        }
        else if (angle > 335 || angle < 25)
        {
            emit advancPageClicked(
                conditionalReverse(Forward, settings->swipeAdvance != AdvancePageGestureDirection::Right));
        }
        else if (swipe->hotSpot().y() < this->height() * SIZES.readerBottomMenuThreshold && angle > 245 &&
                 angle < 295)
        {
            showMenuBar(true);
        }
    }
    else if (QGesture *gesture = event->gesture(Qt::TapGesture))
    {
        auto pos = this->mapFromGlobal(gesture->hotSpot().toPoint());

        if (gesture->state() != Qt::GestureFinished)
            return true;

        // In text mode, don't intercept taps on the bottom bar - let buttons handle them
        if (isTextMode)
        {
            int bottomBarTop = this->height() - textBottomBar->height();
            if (pos.y() >= bottomBarTop)
                return false;  // Let the button get the click

            // In text mode, taps on the nav bar area should dismiss it
            if (ui->readerNavigationBar->isVisible())
            {
                showMenuBar(false);
                return true;
            }

            // Taps in text area advance pages: left half = back, right half = forward
            auto tabSide = pos.x() < this->width() / 2 ? Left : Right;
            emit advancPageClicked(conditionalReverse(Forward, settings->tabAdvance != tabSide));
            return true;
        }

        // Manga mode gesture handling
        if (ui->readerNavigationBar->isVisible())
        {
            if (pos.y() > this->height() * SIZES.readerBottomMenuThreshold * 2)
                showMenuBar(false);
        }
        else if (pos.y() < this->height() * SIZES.readerBottomMenuThreshold ||
                 pos.y() > this->height() * (1.0 - SIZES.readerBottomMenuThreshold) ||
                 (pos.x() > this->width() * SIZES.readerPageSideThreshold &&
                  pos.x() < this->width() * (1 - SIZES.readerPageSideThreshold)))
        {
            showMenuBar(true);
        }
        else
        {
            auto tabSide = pos.x() < this->width() / 2 ? Left : Right;

            emit advancPageClicked(conditionalReverse(Forward, settings->tabAdvance != tabSide));
        }
    }
    else if (QGesture *gesture = event->gesture(Qt::TapAndHoldGesture))
    {
        if (gesture->state() == Qt::GestureFinished)
        {
            // Long press = toggle bookmark
            qDebug() << "Long press detected - toggling bookmark";
            emit bookmarkRequested();
        }
    }

    return true;
}

void MangaReaderWidget::updateMenuBar()
{
    QTime now = QTime::currentTime();
    ui->labelTime->setText(now.toString("hh:mm"));

    ui->batteryIcon->updateIcon();
}

void MangaReaderWidget::updateCurrentIndex(const ReadingProgress &progress)
{
    ui->labelReaderChapter->setText("Ch. " + QString::number(progress.index.chapter + 1) + "/" +
                                    QString::number(progress.numChapters));

    if (isTextMode)
    {
        int tp = textPageOffsets.size();
        ui->labelReaderPage->setText("Page: " + QString::number(textCurrentPage + 1) + "/" +
                                     QString::number(qMax(1, tp)));
    }
    else
    {
        ui->labelReaderPage->setText("Page: " + QString::number(progress.index.page + 1) + "/" +
                                     QString::number(progress.numPages));
    }

    gotodialog->setup(progress);
}

void MangaReaderWidget::on_pushButtonReaderHome_clicked()
{
    emit changeView(HomeTab);
}

void MangaReaderWidget::on_pushButtonReaderBack_clicked()
{
    emit back();
}

void MangaReaderWidget::on_pushButtonReaderFavorites_clicked()
{
    emit changeView(FavoritesTab);
}

void MangaReaderWidget::setTextMode(bool textMode)
{
    isTextMode = textMode;

    // Always reparent nav bars to this widget (not mangaImageWidget)
    // mangaImageWidget has a custom paintEvent that draws over its children
    if (ui->readerNavigationBar->parentWidget() != this)
    {
        ui->readerNavigationBar->setParent(this);
        ui->readerFrontLightBar->setParent(this);
    }

    if (textMode)
    {
        ui->mangaImageWidget->hide();

        int barH = textBottomBar->height();
        int w = this->width();
        int h = this->height();

        textReader->setGeometry(0, 0, w, h - barH);
        textBottomBar->setGeometry(0, h - barH, w, barH);

        textReader->show();
        textBottomBar->show();
        textBottomBar->raise();
    }
    else
    {
        textReader->hide();
        textBottomBar->hide();
        ui->mangaImageWidget->show();
    }
}

void MangaReaderWidget::showText(const QString &text, const QString &chapterTitle)
{
    showMenuBar(false);
    textCurrentPage = 0;
    textPageOffsets.clear();

    QString html = "<html><head><style>"
                   "body { font-family: 'Georgia','Noto Serif',serif; "
                   "line-height: 1.8; margin: 0; padding: 8px 5px; color: #111; }"
                   "h2 { text-align: center; margin: 8px 0 16px 0; "
                   "padding-bottom: 8px; border-bottom: 1px solid #ccc; }"
                   "p { text-indent: 2em; margin: 5px 0; }"
                   "img { max-width: 100%; height: auto; display: block; margin: 12px auto; }"
                   "</style></head><body>";

    html += "<h2>" + chapterTitle.toHtmlEscaped() + "</h2>";

    bool hasImages = text.contains("<img", Qt::CaseInsensitive);
    if (hasImages)
    {
        QString rich = text;
        rich.remove(QRegularExpression(R"(<script[^>]*>.*?</script>)",
                                        QRegularExpression::DotMatchesEverythingOption));
        html += rich;
    }
    else
    {
        for (const auto &p : text.split("\n\n", Qt::SkipEmptyParts))
        {
            auto t = p.trimmed();
            if (!t.isEmpty())
                html += "<p>" + t.toHtmlEscaped().replace("\n", "<br>") + "</p>";
        }
    }
    html += "</body></html>";

    setTextMode(true);
    textReader->setHtml(html);
    textReader->verticalScrollBar()->setValue(0);

    QTimer::singleShot(100, this, [this]() { paginateText(); });
}

void MangaReaderWidget::paginateText()
{
    textPageOffsets.clear();
    textPageOffsets.append(0);

    int viewH = textReader->viewport()->height();
    auto *sb = textReader->verticalScrollBar();
    int maxScroll = sb->maximum();

    if (viewH <= 0 || maxScroll <= 0)
    {
        textGoToPage(0);
        return;
    }

    for (int pos = viewH; pos <= maxScroll; pos += viewH)
        textPageOffsets.append(pos);

    if (!textPageOffsets.isEmpty() && textPageOffsets.last() < maxScroll)
        textPageOffsets.append(maxScroll);

    textGoToPage(0);
}

void MangaReaderWidget::updateTextBottomBar()
{
    int total = qMax(1, textPageOffsets.size());
    int pct = total > 1 ? (textCurrentPage * 100) / (total - 1) : 100;

    textPageLabel->setText(QString("%1/%2").arg(textCurrentPage + 1).arg(total));
    textProgressBar->setValue(pct);
}

void MangaReaderWidget::textGoToPage(int page)
{
    if (textPageOffsets.isEmpty())
        return;
    page = qBound(0, page, textPageOffsets.size() - 1);
    textCurrentPage = page;
    textReader->verticalScrollBar()->setValue(textPageOffsets[page]);
    updateTextBottomBar();
}

void MangaReaderWidget::textPageForward()
{
    if (textCurrentPage + 1 < textPageOffsets.size())
        textGoToPage(textCurrentPage + 1);
    else
        emit advancPageClicked(Forward);
}

void MangaReaderWidget::textPageBack()
{
    if (textCurrentPage > 0)
        textGoToPage(textCurrentPage - 1);
    else
        emit advancPageClicked(Backward);
}

void MangaReaderWidget::showImage(const QString &path)
{
    setTextMode(false);
    showMenuBar(false);

    if (QFile::exists(path))
    {
        int i = searchCache(path);

        if (i != -1)
        {
            qDebug() << "Cachehit:" << i;
            ui->mangaImageWidget->setImage(imgcache[i].first);
        }
        else
        {
            if (addImageToCache(path, false))
            {
                i = searchCache(path);
                if (i != -1)
                    ui->mangaImageWidget->setImage(imgcache[i].first);
                else
                    ui->mangaImageWidget->showErrorImage();
            }
            else
            {
                ui->mangaImageWidget->showErrorImage();
            }
        }
    }
    else
    {
        ui->mangaImageWidget->showErrorImage();
    }
}

void MangaReaderWidget::checkMem()
{
    // Be more aggressive on e-ink devices with limited RAM
    // If free memory is below 100MB, clear cache until only the current image remains
    while (imgcache.size() > 1 && (!enoughFreeSystemMemory() || getFreeSystemMemory() < 100 * 1024 * 1024))
    {
        qDebug() << "Low memory detected, clearing reader cache. Current size:" << imgcache.size();
        imgcache.removeLast();
    }
}

bool MangaReaderWidget::addImageToCache(const QString &path, QSharedPointer<QImage> img)
{
    //    QElapsedTimer t;
    //    t.start();

    int i = searchCache(path);
    if (i != -1)
    {
        imgcache.move(i, 0);
    }
    else
    {
        if (!img || img->isNull())
            return false;

        imgcache.insert(0, {img, path});

        if (imgcache.count() > CONF.imageCacheSize)
            imgcache.removeLast();
    }

    checkMem();

    //    qDebug() << "Load image1:" << t.elapsed() << path.split('/').last();

    return true;
}

bool MangaReaderWidget::addImageToCache(const QString &path, bool isPreload)
{
    //    QElapsedTimer t;
    //    t.start();

    int i = searchCache(path);
    if (i != -1)
    {
        imgcache.move(i, 0);
    }
    else if (isPreload && !enoughFreeSystemMemory())
    {
        return false;
    }
    else
    {
        auto img =
            QSharedPointer<QImage>(new QImage(loadQImageFast(path, settings->ditheringMode >= SWDithering)));

        if (!img || img->isNull())
            return false;

        imgcache.insert(0, {img, path});

        if (imgcache.count() > CONF.imageCacheSize)
            imgcache.removeLast();
    }

    checkMem();

    //    qDebug() << "Load image2:" << t.elapsed() << path.split('/').last();

    return true;
}

int MangaReaderWidget::searchCache(const QString &path) const
{
    for (int i = 0; i < imgcache.size(); i++)
        if (imgcache[i].second == path)
            return i;

    return -1;
}

void MangaReaderWidget::clearCache()
{
    imgcache.clear();
    ui->mangaImageWidget->clearImage();
}

void MangaReaderWidget::setSettings(Settings *settings)
{
    this->settings = settings;
}

void MangaReaderWidget::setFrontLightPanelState(int lightmin, int lightmax, int light, int comflightmin,
                                                int comflightmax, int comflight)
{
    // Block signals while setting slider values to prevent the signal cascade:
    // setValue(light) would fire valueChanged with the OLD comfort value,
    // writing stale data to settings and hardware
    QSignalBlocker blockLight(ui->horizontalSliderLight);
    QSignalBlocker blockComf(ui->horizontalSliderComfLight);

    ui->horizontalSliderLight->setMinimum(lightmin);
    ui->horizontalSliderLight->setMaximum(lightmax);
    ui->horizontalSliderLight->setPageStep((lightmax - lightmin) / 20);
    ui->horizontalSliderLight->setValue(light);

    if (comflightmin != comflightmax)
    {
        ui->horizontalSliderComfLight->setMinimum(comflightmin);
        ui->horizontalSliderComfLight->setMaximum(comflightmax);
        ui->horizontalSliderComfLight->setPageStep((comflightmax - comflightmin) / 20);
        ui->horizontalSliderComfLight->setValue(comflight);
    }
    else
    {
        ui->horizontalSliderComfLight->hide();
        ui->toolButtonLessComfLight->hide();
        ui->toolButtonMoreComfLight->hide();
    }
}

void MangaReaderWidget::setFrontLightPanelState(int light, int comflight)
{
    QSignalBlocker blockLight(ui->horizontalSliderLight);
    QSignalBlocker blockComf(ui->horizontalSliderComfLight);

    ui->horizontalSliderLight->setValue(light);
    ui->horizontalSliderComfLight->setValue(comflight);
}

void MangaReaderWidget::on_horizontalSliderLight_valueChanged(int value)
{
    emit frontlightchanged(value, ui->horizontalSliderComfLight->value());
}

void MangaReaderWidget::on_horizontalSliderComfLight_valueChanged(int value)
{
    emit frontlightchanged(ui->horizontalSliderLight->value(), value);
}

void MangaReaderWidget::on_pushButtonReaderGoto_clicked()
{
    if (gotodialog->exec() == QDialog::Accepted)
    {
        showMenuBar(false);

        emit gotoIndex(gotodialog->selectedindex);
    }
}

void MangaReaderWidget::showMenuBar(bool show)
{
    if (!show && ui->readerNavigationBar->isVisible())
    {
        ui->readerFrontLightBar->setVisible(false);
        ui->readerNavigationBar->setVisible(false);
    }
    else if (show && !ui->readerNavigationBar->isVisible())
    {
        updateMenuBar();

        // Always reparent bars to this widget so they render above mangaImageWidget
        // (mangaImageWidget's paintEvent draws over its own children)
        if (ui->readerNavigationBar->parentWidget() != this)
        {
            ui->readerNavigationBar->setParent(this);
            ui->readerFrontLightBar->setParent(this);
        }

        // Position bars manually since they're reparented out of the layout
        {
            int w = this->width();
            int h = this->height();

            // Nav bar at the bottom
            int bottomOffset = (isTextMode && textBottomBar) ? textBottomBar->height() : 0;
            int navH = qMax(ui->readerNavigationBar->sizeHint().height(), SIZES.buttonSize * 3);
            ui->readerNavigationBar->setGeometry(0, h - navH - bottomOffset, w, navH);

            // Front light bar at the top
            int flH = qMax(ui->readerFrontLightBar->sizeHint().height(), SIZES.buttonSize * 4);
            ui->readerFrontLightBar->setGeometry(0, 0, w, flH);
        }

        ui->readerNavigationBar->setVisible(true);
        ui->readerFrontLightBar->setVisible(true);
        ui->readerNavigationBar->raise();
        ui->readerFrontLightBar->raise();
    }
}
