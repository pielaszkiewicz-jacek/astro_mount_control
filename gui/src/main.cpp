#include <QApplication>
#include "main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Astro Mount Control");
    app.setOrganizationName("AstroMount");

    MainWindow window;
    window.setWindowTitle("Astro Mount Control");
    window.resize(1200, 800);
    window.show();

    return app.exec();
}
