#include "downloadmangachaptersdialog.h"

#include <QPushButton>

#include "ui_downloadmangachaptersdialog.h"

DownloadMangaChaptersDialog::DownloadMangaChaptersDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::DownloadMangaChaptersDialog)
{
    ui->setupUi(this);
    adjustUI();
    setWindowFlags(Qt::Popup);

    // Add "Export to Device" button next to OK
    auto *exportButton = new QPushButton(" Export to Kobo ", this);
    exportButton->setObjectName("pushButtonExportToDevice");
    exportButton->setFocusPolicy(Qt::NoFocus);
    exportButton->setFixedHeight(SIZES.buttonSize);
    ui->horizontalLayout->insertWidget(1, exportButton);
    connect(exportButton, &QPushButton::clicked, this,
            &DownloadMangaChaptersDialog::exportToDeviceClicked);

    ui->spinBoxFrom->installEventFilter(this);
    ui->spinBoxTo->installEventFilter(this);
}

void DownloadMangaChaptersDialog::adjustUI()
{
    ui->labelTitle->setStyleSheet("font-size: 12pt");
    ui->pushButtonConfirm->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonCancel->setFixedHeight(SIZES.buttonSize);
    ui->spinBoxFrom->setFixedHeight(SIZES.buttonSize);
    ui->spinBoxTo->setFixedHeight(SIZES.buttonSize);
    ui->numpadWidget->setMinimumHeight(SIZES.numpadHeight);
}

DownloadMangaChaptersDialog::~DownloadMangaChaptersDialog()
{
    delete ui;
}

void DownloadMangaChaptersDialog::show(QSharedPointer<MangaInfo> mangaInfo, int chapterFromDefault,
                                       bool exportOnly)
{
    this->setMaximumWidth(static_cast<QWidget *>(this->parent())->width());

    this->mangaInfo = mangaInfo;
    ui->spinBoxFrom->setRange(1, mangaInfo->chapters.size());
    ui->spinBoxTo->setRange(1, mangaInfo->chapters.size());
    ui->spinBoxFrom->setValue(chapterFromDefault + 1);
    ui->spinBoxTo->setValue(mangaInfo->chapters.size());

    if (exportOnly)
    {
        ui->labelTitle->setText("Export " + mangaInfo->title);
        ui->pushButtonConfirm->hide();
        // Hide the spacer so Export + Cancel fill the width
        if (ui->horizontalLayout->count() > 0)
        {
            auto *spacer = ui->horizontalLayout->itemAt(0)->spacerItem();
            if (spacer)
                ui->horizontalLayout->itemAt(0)->widget() ? (void)0 :
                    spacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        ui->horizontalLayout->invalidate();
    }
    else
    {
        ui->labelTitle->setText("Download " + mangaInfo->title);
        ui->pushButtonConfirm->show();
        // Restore spacer
        if (ui->horizontalLayout->count() > 0)
        {
            auto *spacer = ui->horizontalLayout->itemAt(0)->spacerItem();
            if (spacer)
                spacer->changeSize(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        }
        ui->horizontalLayout->invalidate();
    }

    open();
    ui->spinBoxTo->setFocus();
}

void DownloadMangaChaptersDialog::on_pushButtonCancel_clicked()
{
    mangaInfo.clear();
    close();
}

bool DownloadMangaChaptersDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        auto box = static_cast<QSpinBox *>(obj);
        QTimer::singleShot(0, box, &QSpinBox::selectAll);
        return false;
    }
    else if (event->type() == QEvent::RequestSoftwareInputPanel)
    {
        return true;
    }
    else if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Enter) || (key->key() == Qt::Key_Return))
        {
            on_pushButtonConfirm_clicked();
        }
    }

    return QObject::eventFilter(obj, event);
}

void DownloadMangaChaptersDialog::on_pushButtonConfirm_clicked()
{
    int from = ui->spinBoxFrom->value() - 1;
    int to = ui->spinBoxTo->value() - 1;

    if (from <= to)
    {
        close();
        emit downloadConfirmed(mangaInfo, from, to);
        mangaInfo.clear();
    }
}

void DownloadMangaChaptersDialog::exportToDeviceClicked()
{
    int from = ui->spinBoxFrom->value() - 1;
    int to = ui->spinBoxTo->value() - 1;

    if (from <= to)
    {
        close();
        emit exportToDeviceConfirmed(mangaInfo, from, to);
        mangaInfo.clear();
    }
}
