#include "menudialog.h"

#include <QFrame>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui_menudialog.h"

MenuDialog::MenuDialog(QWidget *parent) : QDialog(parent), ui(new Ui::MenuDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Popup);
    setFixedWidth(180);

    ui->toolButtonMenu->setFixedSize(QSize(SIZES.menuIconSize, SIZES.menuIconSize));
    ui->toolButtonMenu->setIconSize(QSize(SIZES.menuIconSize, SIZES.menuIconSize));

    // Hide ALL original .ui widgets except the close button
    ui->pushButtonExit->hide();
    ui->pushButtonSettings->hide();
    ui->pushButtonClearDownloads->hide();
    ui->pushButtonUpdateMangaList->hide();
    ui->pushButtonAbout->hide();

    // Hide all QFrame separators from the .ui
    for (auto *child : this->findChildren<QFrame *>())
    {
        if (child->frameShape() == QFrame::HLine)
            child->hide();
    }

    // Remove spacers from the layout
    auto *topLayout = qobject_cast<QVBoxLayout *>(this->layout());
    if (topLayout)
    {
        for (int i = topLayout->count() - 1; i >= 0; i--)
        {
            auto *item = topLayout->itemAt(i);
            if (item && item->spacerItem())
            {
                topLayout->removeItem(item);
                delete item;
            }
        }
    }

    // Get the main layout and add our items after the close icon
    auto *mainLayout = qobject_cast<QVBoxLayout *>(this->layout());
    if (!mainLayout)
        return;

    auto addBtn = [&](const QString &text, MenuButton action)
    {
        auto *btn = new QPushButton(text, this);
        btn->setFixedHeight(38);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(
            "QPushButton { background: white; border: none; text-align: left; "
            "padding: 0 16px; font-size: 12pt; }"
            "QPushButton:pressed { background: #e8e8e8; }");
        connect(btn, &QPushButton::clicked, this, [this, action]() { done(action); });
        mainLayout->addWidget(btn);
    };

    auto addSep = [&]()
    {
        auto *line = new QFrame(this);
        line->setFixedHeight(1);
        line->setStyleSheet("background: #ddd; margin: 4px 14px;");
        mainLayout->addWidget(line);
    };

    addBtn("History", HistoryButton);
    addBtn("AniList", AniListButton);
    addSep();
    addBtn("Settings", SettingsButton);
    addBtn("About", AboutButton);
    addSep();
    addBtn("Exit", ExitButton);

    // Push items up
    mainLayout->addStretch();
}

MenuDialog::~MenuDialog()
{
    delete ui;
}

void MenuDialog::on_toolButtonMenu_clicked() { close(); }
void MenuDialog::on_pushButtonExit_clicked() { done(ExitButton); }
void MenuDialog::on_pushButtonSettings_clicked() { done(SettingsButton); }
void MenuDialog::on_pushButtonClearDownloads_clicked() { done(ClearDownloadsButton); }
void MenuDialog::on_pushButtonUpdateMangaList_clicked() { done(UpdateMangaListsButton); }
void MenuDialog::on_pushButtonAbout_clicked() { done(AboutButton); }
