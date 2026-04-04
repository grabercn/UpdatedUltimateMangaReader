#include "clearcachedialog.h"

#include "ui_clearcachedialog.h"

ClearCacheDialog::ClearCacheDialog(QWidget* parent) : QDialog(parent), ui(new Ui::ClearCacheDialog)
{
    ui->setupUi(this);
    adjustUI();
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
}

ClearCacheDialog::~ClearCacheDialog()
{
    delete ui;
}

void ClearCacheDialog::adjustUI()
{
    ui->pushButtonCancel->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonClear1->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonClear2->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonClear3->setFixedHeight(SIZES.buttonSize);

    ui->pushButtonCancel->setProperty("type", "borderless");
    ui->pushButtonClear1->setProperty("type", "borderless");
    ui->pushButtonClear2->setProperty("type", "borderless");
    ui->pushButtonClear3->setProperty("type", "borderless");

    ui->labelTitle->setStyleSheet("font-size: 15pt");
}

void ClearCacheDialog::open()
{
    if (parentWidget())
    {
        resize(parentWidget()->size());
        move(parentWidget()->pos());
        setFixedSize(parentWidget()->size());
    }

    QString str = QString("Calculating... \n%1 MB of free space remaining.")

                      .arg(getFreeSpace());
    ui->labelCacheSize->setText(str);

    QTimer::singleShot(0, this, &ClearCacheDialog::updateLabel);

    QDialog::open();
}

void ClearCacheDialog::updateLabel()
{
    QString str = QString("Downloads take up %1 MB. \n%2 MB of free space remaining.")
                      .arg(getCacheSize())
                      .arg(getFreeSpace());
    ui->labelCacheSize->setText(str);
}

void ClearCacheDialog::on_pushButtonCancel_clicked()
{
    close();
}

void ClearCacheDialog::on_pushButtonClear1_clicked()
{
    done(ClearImages);
}

void ClearCacheDialog::on_pushButtonClear2_clicked()
{
    done(ClearInfos);
}

void ClearCacheDialog::on_pushButtonClear3_clicked()
{
    done(ClearAll);
}
