#include <QApplication>
#include <QFile>
#include <QString>
#include "main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Astro Mount Control");
    app.setOrganizationName("AstroMount");

    // Load and apply the dark theme stylesheet
    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString style = QString::fromUtf8(styleFile.readAll());
        app.setStyleSheet(style);
        styleFile.close();
    } else {
        // Fallback: try relative path from executable
        QFile fallback("style.qss");
        if (fallback.open(QFile::ReadOnly | QFile::Text)) {
            QString style = QString::fromUtf8(fallback.readAll());
            app.setStyleSheet(style);
            fallback.close();
        }
    }

    MainWindow window;
    window.setWindowTitle("Astro Mount Control");
    window.resize(1200, 800);
    window.show();

    return app.exec();
}
