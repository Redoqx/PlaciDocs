#include <QApplication>
#include <QStringList>

#include "gui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PlaciDocs"));
    QApplication::setApplicationName(QStringLiteral("PlaciDocs"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    placi::gui::MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) w.openFile(args.at(1));
    w.show();
    return QApplication::exec();
}
