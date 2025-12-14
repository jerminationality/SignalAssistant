#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QUrl>

#include "AppController.h"

namespace {
QString locateTabPageQml() {
    const QString relativePath = QStringLiteral("qml/pages/TabPage.qml");

    const QList<QDir> probes = {
        QDir(QDir::currentPath()),
        QDir(QCoreApplication::applicationDirPath()),
        QDir(QCoreApplication::applicationDirPath() + "/.."),
        QDir(QCoreApplication::applicationDirPath() + "/../.."),
    };

    for (const QDir& dir : probes) {
        const QString candidate = dir.absoluteFilePath(relativePath);
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }

    return QString();
}
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    AppController controller;
    engine.rootContext()->setContextProperty("AppController", &controller);
    engine.rootContext()->setContextProperty("TabBridge", controller.tabBridge());

    const QString tabPage = locateTabPageQml();
    if (tabPage.isEmpty()) {
        qCritical() << "TabPagePreview: unable to locate qml/pages/TabPage.qml";
        return 1;
    }

    engine.addImportPath(QFileInfo(tabPage).dir().absolutePath());
    engine.load(QUrl::fromLocalFile(tabPage));

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "TabPagePreview: failed to load" << tabPage;
        return 1;
    }

    return app.exec();
}