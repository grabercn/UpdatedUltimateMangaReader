#include "screensaverdialog.h"

#include <QPainter>
#include <QTime>

#include "ui_screensaverdialog.h"

ScreensaverDialog::ScreensaverDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ScreensaverDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Popup);
    ui->labelImage->setStyleSheet("QLabel { background-color : white; }");
}

ScreensaverDialog::~ScreensaverDialog()
{
    delete ui;
}

void ScreensaverDialog::setCurrentManga(QSharedPointer<MangaInfo> manga, int chapter, int page)
{
    currentManga = manga;
    currentChapter = chapter;
    currentPage = page;
}

void ScreensaverDialog::setBatteryLevel(int level, bool isCharging)
{
    battery = level;
    charging = isCharging;
}

bool ScreensaverDialog::event(QEvent *event)
{
    // Only forward power button / sleep cover key events to parent
    // Block ALL touch/mouse events so screensaver can't be dismissed by touch
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
    {
        if (parent())
            parent()->event(event);
        return true;
    }

    // Block touch/mouse during sleep
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::TouchBegin ||
        event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd)
        return true;  // Consume - don't close the dialog

    return QDialog::event(event);
}

void ScreensaverDialog::showRandomScreensaver()
{
    auto *parentWidget = qobject_cast<QWidget *>(this->parent());
    if (!parentWidget)
        return;

    this->resize(parentWidget->size());
    this->move(parentWidget->pos());

    int w = parentWidget->width();
    int h = parentWidget->height();

    QPixmap canvas(w, h);
    canvas.fill(Qt::white);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    bool hasManga = currentManga && !currentManga->title.isEmpty();

    if (hasManga && !currentManga->coverPath.isEmpty() && QFile::exists(currentManga->coverPath))
    {
        // Draw cover image centered, scaled to fit with margins
        QImage cover = loadQImageFast(currentManga->coverPath, false);
        if (!cover.isNull())
        {
            int coverW = w * 0.5;
            int coverH = h * 0.55;
            QImage scaled = cover.scaled(coverW, coverH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            int cx = (w - scaled.width()) / 2;
            int cy = h * 0.08;
            p.drawImage(cx, cy, scaled);

            // Border around cover
            p.setPen(QPen(Qt::black, 2));
            p.drawRect(cx - 1, cy - 1, scaled.width() + 1, scaled.height() + 1);
        }
    }

    // Text area below cover
    p.setPen(Qt::black);

    if (hasManga)
    {
        // Title
        QFont titleFont("sans-serif", 14, QFont::Bold);
        p.setFont(titleFont);
        QRect titleRect(20, h * 0.67, w - 40, h * 0.08);
        p.drawText(titleRect, Qt::AlignCenter | Qt::TextWordWrap, currentManga->title);

        // Reading progress
        QFont infoFont("sans-serif", 11);
        p.setFont(infoFont);
        p.setPen(QColor(80, 80, 80));

        QString progress = "Chapter " + QString::number(currentChapter + 1);
        if (currentPage > 0)
            progress += ", Page " + QString::number(currentPage + 1);
        if (currentManga->chapters.count() > 0)
            progress += " of " + QString::number(currentManga->chapters.count()) + " chapters";

        QRect progRect(20, h * 0.76, w - 40, h * 0.05);
        p.drawText(progRect, Qt::AlignCenter, progress);

        // Author
        if (!currentManga->author.isEmpty())
        {
            QFont smallFont("sans-serif", 9);
            p.setFont(smallFont);
            p.setPen(QColor(120, 120, 120));
            QRect authorRect(20, h * 0.81, w - 40, h * 0.04);
            p.drawText(authorRect, Qt::AlignCenter, "by " + currentManga->author);
        }
    }
    else
    {
        // No manga - show app name
        QFont titleFont("sans-serif", 18, QFont::Bold);
        p.setFont(titleFont);
        QRect titleRect(0, h * 0.4, w, h * 0.1);
        p.drawText(titleRect, Qt::AlignCenter, "Ultimate Manga Reader");
    }

    // Bottom: "Sleeping since" timestamp (static, no updates needed)
    p.setPen(QColor(140, 140, 140));
    QFont bottomFont("sans-serif", 9);
    p.setFont(bottomFont);

    QString sleepInfo = "Sleeping since " + QTime::currentTime().toString("h:mm AP");
    if (charging)
        sleepInfo += "  |  Charging";
    QRect timeRect(20, h - 35, w - 40, 25);
    p.drawText(timeRect, Qt::AlignCenter, sleepInfo);

    // Thin separator
    p.setPen(QColor(210, 210, 210));
    p.drawLine(40, h - 42, w - 40, h - 42);

    p.end();

    ui->labelImage->setPixmap(canvas);
    ui->labelImage->showFullScreen();
    open();
}
