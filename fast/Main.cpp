/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FastCapture.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QTimer>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    app.setApplicationName(u"spectacle-fast"_s);
    app.setOrganizationDomain(u"org.kde"_s);
    app.setApplicationVersion(u"6.7.5-fast1"_s);
    app.setDesktopFileName(u"org.kde.spectacle.fast"_s);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setQuitLockEnabled(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Select a screen region and copy it on mouse release. Escape cancels."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({u"daemon"_s, u"Wait for Capture calls instead of taking a screenshot immediately."_s});
    parser.addOption({u"freeze"_s, u"Freeze the desktop before selection instead of capturing on release."_s});
    parser.process(app);

    if (!QGuiApplication::platformName().startsWith(u"wayland")) {
        qCritical() << "spectacle-fast requires a Plasma Wayland session. Use Spectacle's regular UI on X11.";
        return 1;
    }

    auto bus = QDBusConnection::sessionBus();
    const QString service = u"org.kde.Spectacle.Fast"_s;
    if (!bus.registerService(service)) {
        if (bus.interface()->isServiceRegistered(service)) {
            if (parser.isSet(u"daemon"_s)) {
                return 0;
            }
            const auto request = QDBusMessage::createMethodCall(service, u"/org/kde/Spectacle/Fast"_s, service, u"Capture"_s);
            const auto reply = bus.call(request);
            return reply.type() == QDBusMessage::ErrorMessage ? 1 : 0;
        }
        qCritical() << "Could not register screenshot service:" << bus.lastError().message();
        return 1;
    }
    FastCapture capture(parser.isSet(u"freeze"_s));
    bus.registerObject(u"/org/kde/Spectacle/Fast"_s, &capture, QDBusConnection::ExportAllSlots);
    QObject::connect(&capture, &FastCapture::failed, &app, [](const QString &message) {
        qWarning().noquote() << message;
    });
    if (!parser.isSet(u"daemon"_s)) {
        QTimer::singleShot(0, &capture, &FastCapture::Capture);
    }
    return app.exec();
}
