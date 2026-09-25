#include "../hotkey_settings.h"
#include <QtWidgets/QMenuBar>
#include <QtCore/QTemporaryDir>
#include <QtGui/QPixmap>
#include <iostream>
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
    if(!dialog.grab().save(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("dialog_check.png"))) return 5;
    std::cout<<"PASS font inheritance and no help button; size "<<dialog.width()<<"x"<<dialog.height()<<"\n";
    return 0;
}
