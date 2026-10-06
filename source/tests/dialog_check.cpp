#include "../hook.cpp"
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QCommonStyle>
#include <QtWidgets/QTableView>
#include <QtCore/QTemporaryDir>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QStandardItemModel>
#include <iostream>

static QString drawnText;
static bool unrelatedCacheRetainsSource = true;
static void __fastcall captureDrawText(QPainter*, int, int, const QString& text) {
    drawnText = text;
}

class WidgetPaintProbe : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        hookedDrawXY(&painter, 0, 10, QStringLiteral("File"));
    }
};

class StylePaintProbe : public QCommonStyle {
public:
    mutable QString drawnText;
    void drawControl(ControlElement, const QStyleOption*, QPainter* painter,
                     const QWidget*) const override {
        QImage unrelated(128, 32, QImage::Format_ARGB32);
        QPainter exportPainter(&unrelated);
        hookedDrawXY(&exportPainter, 0, 12, QStringLiteral("File"));
        proxy()->drawItemText(&exportPainter, unrelated.rect(), Qt::AlignLeft,
                             QPalette(), true, QStringLiteral("File"));
        unrelatedCacheRetainsSource = unrelatedCacheRetainsSource &&
            ::drawnText == QStringLiteral("File") && drawnText == QStringLiteral("File");
        hookedDrawXY(painter, 0, 12, QStringLiteral("File"));
        proxy()->drawItemText(painter, QRect(0, 0, 128, 32), Qt::AlignLeft,
                             QPalette(), true, QStringLiteral("File"));
    }
    void drawItemText(QPainter*, const QRect&, int, const QPalette&, bool,
                      const QString& text, QPalette::ColorRole) const override {
        drawnText = text;
    }
};

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    QFile hostStyle(QDir::homePath() + "/AppData/Roaming/TopoGun3/ui/style.dat");
    if(hostStyle.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(hostStyle.readAll()));
    QWidget host;
    QMenuBar menu(&host);
    menu.setFont(QFont("Segoe UI", 10));
    menu.ensurePolished();
    QTemporaryDir dir;
    int key=Qt::Key_F5; bool open=false;
    ToggleHotkeyDialog dialog(&host,key,open,dir.path()+"/hotkeys.json",&menu);
    dialog.setAttribute(Qt::WA_DeleteOnClose,false);
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.show();
    app.processEvents();
    if(dialog.font()!=menu.font()) return 2;
    for(auto* label:dialog.findChildren<QLabel*>())
        if(label->font().pointSizeF()!=menu.font().pointSizeF()) return 3;
    if(dialog.windowFlags() & Qt::WindowContextHelpButtonHint) return 4;
    g_translations["File"] = QStringLiteral("文件").toUtf8().toStdString();
    installFontMeasurementHooks();
    if (!g_fmHAdv || !g_fontMeasureReady) return 6;
    const QFontMetrics metrics(menu.font());
    if (hookedFontHAdv(&metrics, QStringLiteral("File"), -1) !=
        g_fmHAdv(&metrics, QStringLiteral("文件"), -1)) return 7;
    // Host measurement imports are not patched: only Qt5Widgets' UI layout
    // calls see translated metrics. This remains true with translation enabled.
    if (metrics.horizontalAdvance(QStringLiteral("File")) !=
        g_fmHAdv(&metrics, QStringLiteral("File"), -1)) return 8;

    g_drawXY = captureDrawText;
    QImage exportImage(128, 32, QImage::Format_ARGB32_Premultiplied);
    QPainter exportPainter(&exportImage);
    hookedDrawXY(&exportPainter, 0, 12, QStringLiteral("File"));
    if (drawnText != QStringLiteral("File")) return 9;
    exportPainter.end();
    QPixmap offscreenPixmap(128, 32);
    QPainter pixmapPainter(&offscreenPixmap);
    hookedDrawXY(&pixmapPainter, 0, 12, QStringLiteral("File"));
    if (drawnText != QStringLiteral("File")) return 10;
    pixmapPainter.end();

    WidgetPaintProbe ui;
    ui.setAttribute(Qt::WA_DontShowOnScreen);
    ui.show();
    ui.repaint();
    if (drawnText != QStringLiteral("文件")) return 11;

    // A visible widget owner distinguishes its style cache from unscoped export.
    // Merely calling QStyle does not prove an image is UI output.
    auto* baseStyle = new StylePaintProbe;
    ChineseProxyStyle proxy(baseStyle);
    QPainter uiCachePainter(&exportImage);
    proxy.drawItemText(&uiCachePainter, QRect(0, 0, 128, 32), Qt::AlignLeft,
        app.palette(), true, QStringLiteral("File"));
    if (baseStyle->drawnText != QStringLiteral("File")) return 12;
    drawnText.clear();
    QStyleOption cacheOption;
    proxy.drawControl(QStyle::CE_PushButton, &cacheOption, &uiCachePainter, &ui);
    if (drawnText != QStringLiteral("文件") || baseStyle->drawnText != QStringLiteral("文件")) return 15;
    proxy.drawControl(QStyle::CE_PushButton, &cacheOption, &uiCachePainter, nullptr);
    if (drawnText != QStringLiteral("File") || baseStyle->drawnText != QStringLiteral("File")) return 16;
    uiCachePainter.end();
    if (!unrelatedCacheRetainsSource) return 17;

    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), QStringLiteral("File"));
    QTableView view;
    view.setModel(&model);
    applyWidget(&view);
    auto* delegate = dynamic_cast<TranslateDelegate*>(view.itemDelegate());
    if (!delegate || delegate->displayText(model.index(0, 0).data(), QLocale()) !=
        QStringLiteral("文件")) return 13;
    if (model.index(0, 0).data().toString() != QStringLiteral("File") ||
        QCoreApplication::translate("TopoGunBoundaryProbe", "File") !=
            QStringLiteral("File")) return 14;
    std::cout<<"PASS UI font layout and paint, unchanged host metrics/model/tr, unchanged offscreen export text; size "
             <<dialog.width()<<"x"<<dialog.height()<<"\n";
    return 0;
}
