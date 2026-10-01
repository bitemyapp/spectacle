/* SPDX-License-Identifier: GPL-2.0-or-later */
// Test executable only. No test input/clipboard API is exported by the installed
// selector. A temporary desktop entry authorizes this exact test binary in KWin.
#include "FastCapture.h"
#include <KSystemClipboard>
#include <QClipboard>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>

using namespace Qt::StringLiterals;

class LiveProbe final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Spectacle.Fast.Test")
public:
    explicit LiveProbe(FastCapture &capture)
        : m_capture(capture)
    {
    }
public Q_SLOTS:
    bool WindowActive()
    {
        for (auto *window : m_capture.windows()) {
            if (window->isActive()) {
                return true;
            }
        }
        return false;
    }
    void Drag(double x, double y, double width, double height)
    {
        auto windows = m_capture.windows();
        if (windows.isEmpty()) {
            return;
        }
        auto *window = windows.first();
        const QPointF start = QPointF(x, y) - window->screen()->geometry().topLeft();
        const QPointF end = start + QPointF(width, height);
        auto send = [window](QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, point + window->screen()->geometry().topLeft(), button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &event);
        };
        send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    }
    void Escape()
    {
        for (auto *window : m_capture.windows()) {
            if (window->isVisible()) {
                QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &event);
                break;
            }
        }
    }
    QString ClipboardInfo()
    {
        const auto *mime = KSystemClipboard::instance()->mimeData(QClipboard::Clipboard);
        const auto image = mime ? qvariant_cast<QImage>(mime->imageData()) : QImage();
        int visible = 0;
        for (auto *window : m_capture.windows()) {
            visible += window->isVisible();
        }
        bool uniform = !image.isNull();
        int minimumAlpha = 255;
        QPoint firstDifferent(-1, -1);
        QColor differentColor;
        const QColor center = image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
        for (int y = 0; uniform && y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                minimumAlpha = qMin(minimumAlpha, pixel.alpha());
                if (pixel.rgb() != center.rgb()) {
                    uniform = false;
                    firstDifferent = {x, y};
                    differentColor = image.pixelColor(x, y);
                    break;
                }
            }
        }
        return QString::fromUtf8(QJsonDocument(QJsonObject{{u"width"_s, image.width()},
                                                           {u"height"_s, image.height()},
                                                           {u"center_rgb"_s, center.name()},
                                                           {u"uniform"_s, uniform},
                                                           {u"visible_windows"_s, visible},
                                                           {u"minimum_alpha"_s, minimumAlpha},
                                                           {u"different_x"_s, firstDifferent.x()},
                                                           {u"different_y"_s, firstDifferent.y()},
                                                           {u"different_rgb"_s, differentColor.name()},
                                                           {u"png_available"_s, mime && mime->hasFormat(u"image/png"_s)}})
                                     .toJson(QJsonDocument::Compact));
    }

private:
    FastCapture &m_capture;
};

int main(int argc, char **argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    app.setApplicationName(u"spectacle-fast-probe"_s);
    app.setDesktopFileName(u"org.kde.spectacle.fast.test"_s);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setQuitLockEnabled(false);
    FastCapture capture(app.arguments().contains(u"--freeze"_s));
    LiveProbe probe(capture);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(u"org.kde.Spectacle.Fast"_s)) {
        return 1;
    }
    bus.registerObject(u"/org/kde/Spectacle/Fast"_s, &capture, QDBusConnection::ExportAllSlots);
    bus.registerObject(u"/probe"_s, &probe, QDBusConnection::ExportAllSlots);
    QObject::connect(&capture, &FastCapture::failed, [](const QString &error) {
        qWarning().noquote() << error;
    });
    return app.exec();
}

#include "LiveProbe.moc"
