#include "SidecarJson.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>

#include <iostream>

// Prints a .ipr as JSON: iprSidecarDump photo.CR3.ipr
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 2) {
        std::cerr << "Usage: iprSidecarDump FILE.ipr\n";
        return 2;
    }
    QFile file(arguments.at(1));
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Cannot read " << arguments.at(1).toStdString() << '\n';
        return 1;
    }
    QString error;
    const auto document = SidecarJson::fromBytes(file.readAll(), &error);
    if (!document) {
        std::cerr << arguments.at(1).toStdString() << ": " << error.toStdString() << '\n';
        return 1;
    }
    std::cout << QJsonDocument(*document).toJson(QJsonDocument::Indented).toStdString();
    return 0;
}
