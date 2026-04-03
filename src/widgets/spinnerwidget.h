#ifndef SPINNERWIDGET_H
#define SPINNERWIDGET_H

#include <QPainter>
#include <QWidget>

class SpinnerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpinnerWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setFixedSize(parent ? parent->size() : QSize(400, 400));
    }

    void start()
    {
        if (parentWidget())
            setFixedSize(parentWidget()->size());
        raise();
        show();
        // Single repaint - no animation, no timer, no flashing
        repaint();
    }

    void stop()
    {
        hide();
    }

    void setMessage(const QString &msg) { message = msg; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);

        // White overlay
        p.fillRect(rect(), QColor(255, 255, 255, 230));

        // Static text in center - no animation
        p.setPen(QColor(80, 80, 80));
        auto f = p.font();
        f.setBold(true);
        p.setFont(f);

        QString text = message.isEmpty() ? "Searching..." : message;
        p.drawText(rect(), Qt::AlignCenter, text);

        // Simple static dots below text (not animated)
        int cx = rect().center().x();
        int cy = rect().center().y() + 24;
        int dotR = 3;
        p.setBrush(QColor(60, 60, 60));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(cx - 16, cy), dotR, dotR);
        p.drawEllipse(QPoint(cx, cy), dotR, dotR);
        p.drawEllipse(QPoint(cx + 16, cy), dotR, dotR);
    }

private:
    QString message;
};

#endif  // SPINNERWIDGET_H
