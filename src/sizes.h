#ifndef SIZES_H
#define SIZES_H

#include <QApplication>
#include <QScreen>

#define SIZES DPIAwareSizes::get()
class DPIAwareSizes
{
public:
    static DPIAwareSizes &get()
    {
        static DPIAwareSizes instance;
        return instance;
    }

    // Screen-relative sizing: base everything off actual screen height
    // Kobo Libra H2O: 1680px tall. Desktop default: ~680px.
    // All sizes scale proportionally to screen height.
    int screenHeight() const
    {
        auto *screen = QApplication::primaryScreen();
        return screen ? screen->geometry().height() : 680;
    }

    int screenWidth() const
    {
        auto *screen = QApplication::primaryScreen();
        return screen ? screen->geometry().width() : 510;
    }

    // Scale factor relative to the 680px reference height
    float scale() const { return (float)screenHeight() / 680.0f; }

    // Scaled pixel value
    int sp(float basePx) const { return (int)(basePx * scale()); }

    const int screenDPI = 108;
    constexpr int mmToPx(float mm) { return (int)(mm * screenDPI * 0.0393701); }

    // All sizes use sp() for screen-proportional scaling
    int listSourcesHeight = sp(60);
    int mangasourceIconSize = sp(36);
    int mangasourceItemWidth = sp(52);
    int mangasourceItemHeight = sp(52);
    int mangasourceIconSpacing = sp(6);

    int buttonSize = sp(30);
    int buttonSizeToggleFavorite = sp(34);

    int numpadHeight = sp(150);

    int resourceIconSize = sp(18);
    int lightIconSize = sp(26);
    int batteryIconHeight = sp(10);
    int wifiIconSize = sp(20);
    int menuIconSize = sp(34);

    int coverHeight = sp(170);
    int coverWidth = (int)(coverHeight * 0.7f);

    int favoriteSectonHeight = sp(68);
    int favoriteCoverSize = sp(55);

    int frontlightSliderHandleHeight = sp(28);

    int errormessageWidgetHeight = sp(28);

    int downloadStatusDialogWidth = sp(270);
    int downloadStatusDialogHeight = sp(200);

    const float readerPageSideThreshold = 0.4f;
    const float readerBottomMenuThreshold = 0.1f;
};

#endif  // SIZES_H
