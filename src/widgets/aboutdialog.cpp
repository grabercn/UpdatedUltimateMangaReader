#include "aboutdialog.h"

#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    ui->pushButtonClose->setFixedHeight(SIZES.buttonSize);
    ui->labelText->setText(aboutString);
}

void AboutDialog::open()
{
    if (parentWidget())
    {
        resize(parentWidget()->size());
        move(parentWidget()->pos());
        setFixedSize(parentWidget()->size());
    }
    QDialog::open();
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

void AboutDialog::on_pushButtonClose_clicked()
{
    close();
}
