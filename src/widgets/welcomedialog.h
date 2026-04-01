#ifndef WELCOMEDIALOG_H
#define WELCOMEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class WelcomeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WelcomeDialog(QWidget *parent = nullptr);

    static bool shouldShow();
    static void markShown();

private:
    QLabel *contentLabel;
    QLabel *pageIndicator;
    QPushButton *prevBtn;
    QPushButton *nextBtn;
    QPushButton *actionBtn;
    int currentPage;
    int totalPages;

    void showPage(int page);
    QStringList pages;
};

#endif  // WELCOMEDIALOG_H
