#include "loginwindow.h"

#include <QApplication>
#include <QDir>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("IMClient");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("MyCompany");

    QDir configDir;
    if (!configDir.exists("config")) {
        configDir.mkdir("config");
    }

    LoginWindow window;
    window.show();

    return app.exec();
}
