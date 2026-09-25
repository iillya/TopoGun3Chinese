// Hidden test process: no interaction with the user's TopoGun window.
#include <windows.h>
#include "../hotkey_config.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QLabel>
#include <QtCore/QTimer>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtGui/QPixmap>
#include <QtGui/QKeyEvent>
#include <iostream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    if(argc!=3) return 2;
    const QString output=QString::fromLocal8Bit(argv[2]);
    QFile style(QDir::homePath()+"/AppData/Roaming/TopoGun3/ui/style.dat");
    if(style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
    const QString dll=QString::fromLocal8Bit(argv[1]);
    if(!LoadLibraryW(reinterpret_cast<LPCWSTR>(dll.utf16()))) return 3;
    QTimer::singleShot(1000,[&] {
        const QString original=QStringLiteral("The scene is not saved, quit anyway?");
        auto* box=new QMessageBox(QMessageBox::Question,"TopoGun",original,QMessageBox::Yes|QMessageBox::No);
        box->setAttribute(Qt::WA_DontShowOnScreen);
        box->show();
        QTimer::singleShot(300,[&,box,original] {
            auto* label=box->findChild<QLabel*>("qt_msgbox_label");
            if(!label || label->text()!=original || !box->grab().save(output+"/message_zh.png")) {
                app.exit(4); return;
            }
            const int key=ChineseHotkeyConfig::load(ChineseHotkeyConfig::path(QStringLiteral("TopoGunChinese")));
            QKeyEvent toggle(QEvent::KeyPress,key,Qt::NoModifier);
            QApplication::sendEvent(box,&toggle);
            QTimer::singleShot(100,[&,box,label,original] {
                if(label->text()!=original || !box->grab().save(output+"/message_en.png")) {
                    app.exit(5); return;
                }
                std::cout<<"PASS: message screenshots saved; source text preserved across toggle\n";
                app.exit(0);
            });
        });
    });
    return app.exec();
}
