#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("SEC PC Client");
    app.setOrganizationName("sec");

    secpc::MainWindow w;
    w.show();
    return app.exec();
}
