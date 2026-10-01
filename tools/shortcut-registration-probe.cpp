// SPDX-License-Identifier: GPL-2.0-or-later
#include "component.h"
#include "dummy.h"
#include "kglobalacceld.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTest>

Q_IMPORT_PLUGIN(KGlobalAccelImpl)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString mode = app.arguments().value(1);
    if (mode != QStringLiteral("unsafe") && mode != QStringLiteral("safe") && mode != QStringLiteral("native")) {
        fprintf(stderr, "Use dbus-run-session -- env QT_QPA_PLATFORM=offscreen registrationprobe {unsafe|safe|native}\n");
        return 64;
    }
    if (!QDBusConnection::sessionBus().isConnected()
        || QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel"))) {
        fprintf(stderr, "Refusing an existing shortcut service. Use a private dbus-run-session.\n");
        return 65;
    }
    QTemporaryDir files;
    Q_ASSERT(files.isValid());
    for (const auto &pair : {std::pair{"XDG_DATA_HOME", "/data"}, {"XDG_CONFIG_HOME", "/config"}, {"XDG_STATE_HOME", "/state"}}) {
        qputenv(pair.first, (files.path() + QString::fromLatin1(pair.second)).toUtf8());
    }
    qputenv("XDG_DATA_DIRS", (files.path() + QStringLiteral("/empty")).toUtf8());
    qputenv("KGLOBALACCELD_PLATFORM", "dummy");
    KGlobalAccelD daemon;
    if (!daemon.init()) {
        return 2;
    }
    const QString id = QStringLiteral("probe-region.desktop");
    const QString directory = files.path() + QStringLiteral("/data/kglobalaccel");
    QDir().mkpath(directory);
    QFile desktop(directory + QLatin1Char('/') + id);
    if (!desktop.open(QIODevice::WriteOnly)) {
        return 3;
    }
    desktop.write("[Desktop Entry]\nType=Application\nName=Registration Probe\nExec=/usr/bin/true\nStartupNotify=false\nX-KDE-Shortcuts=Alt+$\n");
    desktop.close();
    const QStringList action{id, QStringLiteral("_launch"), QStringLiteral("Registration Probe"), QStringLiteral("Launch")};
    const QStringList bootstrap{id, QStringLiteral("_setup_load_only"), QStringLiteral("Registration Probe"), QStringLiteral("Load")};
    const int key = (Qt::AltModifier | Qt::Key_Dollar).toCombined();
    const bool safe = app.arguments().contains(QStringLiteral("safe"));
    if (app.arguments().contains(QStringLiteral("native"))) {
        auto *backend = KGlobalAccelImpl::instance();
        const QStringList nativeAction{QStringLiteral("spectacle-fast"),
                                       QStringLiteral("CaptureRegion"),
                                       QStringLiteral("Spectacle Fast"),
                                       QStringLiteral("Copy")};
        int count = 0;
        for (int restart = 0; restart < 5; ++restart) {
            QAction action(QStringLiteral("Copy"), &app);
            action.setObjectName(nativeAction[1]);
            action.setProperty("componentName", nativeAction[0]);
            action.setAutoRepeat(false);
            QObject::connect(&action, &QAction::triggered, &app, [&count] {
                ++count;
            });
            if (!KGlobalAccel::setGlobalShortcut(&action, QKeySequence(key))) {
                return 10;
            }
            daemon.setShortcut(nativeAction, {key}, 4); // The new setup script.
            for (int press = 0; press < 100; ++press) {
                const int before = count;
                backend->checkKeyEvent(Qt::Key_Alt, ShortcutKeyState::Pressed);
                backend->checkKeyEvent((Qt::AltModifier | Qt::Key_Shift).toCombined(), ShortcutKeyState::Pressed);
                // KWin's XKB translation consumes Shift when producing '$'.
                const int shifted = key;
                if (!backend->checkKeyEvent(shifted, ShortcutKeyState::Pressed)) {
                    return 11;
                }
                backend->checkKeyEvent(shifted, ShortcutKeyState::Repeated);
                backend->checkKeyEvent(shifted, ShortcutKeyState::Released);
                backend->checkKeyEvent((Qt::AltModifier | Qt::Key_Shift).toCombined(), ShortcutKeyState::Released);
                backend->checkKeyEvent(Qt::Key_Alt, ShortcutKeyState::Released);
                QElapsedTimer delivery;
                delivery.start();
                while (count < before + 1 && delivery.elapsed() < 500) {
                    QTest::qWait(5);
                }
                QTest::qWait(5);
                if (count != before + 1) {
                    fprintf(stderr, "restart=%d press=%d expected=%d actual=%d\n", restart, press, before + 1, count);
                    return 12;
                }
            }
            daemon.setShortcut(nativeAction, {}, 4);
            if (backend->checkKeyEvent(key, ShortcutKeyState::Pressed)) {
                return 13;
            }
            backend->checkKeyEvent(key, ShortcutKeyState::Released);
        }
        fprintf(stdout, "Native QAction: 500 Alt+Shift+4 press/release cycles, auto-repeat suppression, 5 client restarts and key disabling passed.\n");
        return 0;
    }
    if (safe) {
        // Loading a service component itself registers _launch. Registering a
        // distinct inactive action does not replace that existing shortcut.
        daemon.doRegister(bootstrap);
        daemon.doRegister(action);
        daemon.unregister(id, bootstrap[1]);
    } else {
        daemon.doRegister(action);
    }
    // Mirror the first version of our setup and its later migration.
    daemon.setShortcut(action, {key}, 4);
    qInfo() << "Registered; visible shortcuts:" << daemon.shortcut(action);
    daemon.unregister(id, action[1]);
    QTest::qWait(700); // Allow the settings write to destroy an empty component.
    qInfo() << "After unregister/removal; pressing Alt+$ in dummy backend";
    auto *backend = KGlobalAccelImpl::instance();
    const bool handledAfterRemoval = backend->checkKeyEvent(key, ShortcutKeyState::Pressed);
    backend->checkKeyEvent(key, ShortcutKeyState::Released);
    if (handledAfterRemoval) {
        qCritical() << "Removed key still grabbed";
        return 4;
    }
    if (safe) {
        // Also exercise a fresh installation, repeated setup and actual key
        // press/release, rather than Component::invokeShortcut alone.
        for (int i = 0; i < 3; ++i) {
            daemon.doRegister(bootstrap);
            daemon.doRegister(action);
            daemon.setShortcut(action, {key}, 6);
            daemon.unregister(id, bootstrap[1]);
            if (!backend->checkKeyEvent(key, ShortcutKeyState::Pressed)) {
                return 5;
            }
            backend->checkKeyEvent(key, ShortcutKeyState::Released);
            QTest::qWait(100);
        }
        daemon.setShortcut(action, {}, 4);
        if (backend->checkKeyEvent(key, ShortcutKeyState::Pressed)) {
            return 6;
        }
        backend->checkKeyEvent(key, ShortcutKeyState::Released);
    }
    qInfo() << "Registration/removal/key-event probe passed";
    fprintf(stdout, "Registration/removal/key-event probe passed.\n");
    return 0;
}
