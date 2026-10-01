/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FastCapture.h"
#include "ImageMimeData.h"

#include <KSystemClipboard>
#include <QBackingStore>
#include <QBuffer>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSocketNotifier>
#include <QSurfaceFormat>
#include <QTimer>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

using namespace Qt::StringLiterals;

static void trimHeap()
{
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

QImage cropScreens(const QList<ScreenImage> &screens, const QRectF &selection)
{
    const QRectF normalized = selection.normalized();
    QList<ScreenImage> intersecting;
    QRectF bounds;
    qreal scale = 1;
    for (const auto &screen : screens) {
        const QRectF intersection = normalized.intersected(screen.geometry);
        if (intersection.isEmpty() || screen.image.isNull()) {
            continue;
        }
        bounds = bounds.united(intersection);
        scale = std::max(scale, screen.image.devicePixelRatio());
        intersecting.append(screen);
    }
    if (intersecting.isEmpty()) {
        return {};
    }
    if (intersecting.size() == 1) {
        const auto &screen = intersecting.first();
        const qreal dpr = screen.image.devicePixelRatio();
        const QRectF local = bounds.translated(-screen.geometry.topLeft());
        const int x = qRound(local.left() * dpr);
        const int y = qRound(local.top() * dpr);
        const int right = qRound(local.right() * dpr);
        const int bottom = qRound(local.bottom() * dpr);
        auto result = screen.image.copy(QRect(x, y, right - x, bottom - y).intersected(screen.image.rect()));
        // Clipboard consumers expect pixel dimensions rather than a HiDPI icon.
        result.setDevicePixelRatio(1);
        return result;
    }
    QImage result(QSize(qRound(bounds.width() * scale), qRound(bounds.height() * scale)), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto &screen : intersecting) {
        const QRectF part = bounds.intersected(screen.geometry);
        const QRectF local = part.translated(-screen.geometry.topLeft());
        const qreal dpr = screen.image.devicePixelRatio();
        const QRectF source(local.topLeft() * dpr, local.size() * dpr);
        const QRectF destination((part.topLeft() - bounds.topLeft()) * scale, part.size() * scale);
        painter.drawImage(destination, screen.image, source);
    }
    return result;
}

// KWin's ScreenShot2 transport, as used by Spectacle's ImagePlatformKWin.
// Read directly into the final QImage without a worker thread or extra copy.
class ScreenGrab final : public QObject
{
    Q_OBJECT
public:
    ScreenGrab(const QString &method, const QVariantList &arguments, QObject *parent)
        : QObject(parent)
    {
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            finish({}, u"KWin screenshot timed out"_s);
        });
        m_timeout.start(5000);
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) {
            QTimer::singleShot(0, this, [this] {
                finish({}, u"Could not open screenshot pipe"_s);
            });
            return;
        }
        m_fd = fds[0];
        // The kernel may default to an 8 KiB pipe. A frame is tens of MiB;
        // a larger bounded pipe avoids thousands of producer/consumer wakeups.
        // This is optional: kernels/user quotas may reject the resize.
        fcntl(m_fd, F_SETPIPE_SZ, 1024 * 1024);
        fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
        auto message = QDBusMessage::createMethodCall(u"org.kde.KWin.ScreenShot2"_s, u"/org/kde/KWin/ScreenShot2"_s, u"org.kde.KWin.ScreenShot2"_s, method);
        auto args = arguments;
        args.append(QVariantMap{{u"native-resolution"_s, true}, {u"include-cursor"_s, false}, {u"hide-caller-windows"_s, true}});
        args.append(QVariant::fromValue(QDBusUnixFileDescriptor(fds[1])));
        message.setArguments(args);
        auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 4000), this);
        close(fds[1]);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                finish({}, reply.error().message());
                return;
            }
            const auto metadata = reply.value();
            if (qEnvironmentVariableIsSet("SPECTACLE_FAST_TRACE")) {
                qInfo() << "SPECTACLE_FAST_IMAGE_METADATA" << metadata;
            }
            const int width = metadata.value(u"width"_s).toInt();
            const int height = metadata.value(u"height"_s).toInt();
            const int format = metadata.value(u"format"_s).toInt();
            const qreal scale = metadata.value(u"scale"_s, 1).toReal();
            if (metadata.value(u"type"_s).toString() != u"raw" || width <= 0 || height <= 0 || qint64(width) * height > 128 * 1024 * 1024
                || format <= QImage::Format_Invalid || format >= QImage::NImageFormats || !std::isfinite(scale) || scale <= 0) {
                finish({}, u"Invalid screenshot metadata"_s);
                return;
            }
            m_image = QImage(width, height, static_cast<QImage::Format>(format));
            if (m_image.isNull()) {
                finish({}, u"Could not allocate screenshot image"_s);
                return;
            }
            if (metadata.value(u"stride"_s, m_image.bytesPerLine()).toLongLong() != m_image.bytesPerLine()) {
                finish({}, u"Unsupported screenshot row stride"_s);
                return;
            }
            m_image.setDevicePixelRatio(scale);
            m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated, this, [this] {
                readPixels();
            });
            readPixels();
        });
    }
    ~ScreenGrab() override
    {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }
Q_SIGNALS:
    void finished(const QImage &image, const QString &error);

private:
    void readPixels()
    {
        while (m_read < m_image.sizeInBytes()) {
            const auto count = ::read(m_fd, m_image.bits() + m_read, m_image.sizeInBytes() - m_read);
            if (count > 0) {
                m_read += count;
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            } else {
                finish({}, u"Incomplete screenshot data from KWin"_s);
                return;
            }
        }
        finish(m_image, {});
    }
    void finish(const QImage &image, const QString &error)
    {
        if (m_done) {
            return;
        }
        m_done = true;
        m_timeout.stop();
        if (m_notifier) {
            m_notifier->setEnabled(false);
        }
        Q_EMIT finished(image, error);
        deleteLater();
    }
    QImage m_image;
    QTimer m_timeout;
    QSocketNotifier *m_notifier = nullptr;
    qsizetype m_read = 0;
    int m_fd = -1;
    bool m_done = false;
};

RegionWindow::RegionWindow(FastCapture *capture, QScreen *screen)
    : m_capture(capture)
{
    setScreen(screen);
    setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setSurfaceType(QSurface::RasterSurface);
    auto format = requestedFormat();
    format.setAlphaBufferSize(8);
    setFormat(format);
    setTitle(u"Spectacle Fast — Select a region"_s);
    setCursor(Qt::CrossCursor);
}

void RegionWindow::present(const QImage &image)
{
    m_image = image;
    setGeometry(screen()->geometry());
    m_store = std::make_unique<QBackingStore>(this);
    showFullScreen();
    requestActivate();
}

RegionWindow::~RegionWindow() = default;

void RegionWindow::releaseImage()
{
    hide();
    m_image = {};
    m_store.reset();
    // Returning Wayland SHM buffers matters more than merely hiding the UI.
    destroy();
}

void RegionWindow::render()
{
    if (!isExposed() || !m_store) {
        return;
    }
    m_store->resize(size());
    const QRect rect(QPoint(), size());
    m_store->beginPaint(rect);
    if (qEnvironmentVariableIsSet("SPECTACLE_FAST_TRACE")) {
        qInfo() << "SPECTACLE_FAST_SURFACE" << devicePixelRatio() << m_store->paintDevice()->width() << m_store->paintDevice()->height();
    }
    QPainter painter(m_store->paintDevice());
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    if (m_image.isNull()) {
        painter.fillRect(rect, Qt::transparent);
    } else {
        painter.drawImage(QRectF(rect), m_image);
    }
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QRectF selected = m_capture->selection().translated(-screen()->geometry().topLeft());
    QRegion mask(rect);
    if (!selected.isEmpty()) {
        mask -= selected.toAlignedRect();
    }
    painter.setClipRegion(mask);
    painter.fillRect(rect, QColor(0, 0, 0, 90));
    painter.setClipping(false);
    if (!selected.isEmpty()) {
        painter.setPen(QPen(Qt::white, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selected);
    }
    painter.end();
    m_store->endPaint();
    m_store->flush(rect);
    m_capture->framePresented();
}

bool RegionWindow::event(QEvent *event)
{
    if (event->type() == QEvent::UpdateRequest) {
        render();
        return true;
    }
    if (event->type() == QEvent::Close) {
        m_capture->Cancel();
        return true;
    }
    return QWindow::event(event);
}

void RegionWindow::exposeEvent(QExposeEvent *)
{
    render();
}

void RegionWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_capture->Cancel();
    } else if (event->button() == Qt::LeftButton) {
        m_capture->press(event->position() + screen()->geometry().topLeft());
    }
}

void RegionWindow::mouseMoveEvent(QMouseEvent *event)
{
    m_capture->move(event->position() + screen()->geometry().topLeft());
}

void RegionWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_capture->release(event->position() + screen()->geometry().topLeft());
    }
}

void RegionWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_capture->Cancel();
    }
}

FastCapture::FastCapture(bool freeze, QObject *parent)
    : QObject(parent)
    , m_freeze(freeze)
{
    // Start clipboard integration before the first capture, without loading
    // SpectacleCore, QML, OCR, OpenCV or the annotation renderer.
    KSystemClipboard::instance();
    // API v5 excludes the caller's windows, including closing animations, from
    // CaptureArea. Earlier KWin versions safely fall back to a frozen snapshot.
    auto request =
        QDBusMessage::createMethodCall(u"org.kde.KWin.ScreenShot2"_s, u"/org/kde/KWin/ScreenShot2"_s, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    request.setArguments({u"org.kde.KWin.ScreenShot2"_s, u"Version"_s});
    const auto reply = QDBusConnection::sessionBus().call(request);
    const int version =
        reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() ? reply.arguments().first().value<QDBusVariant>().variant().toInt() : 0;
    m_freeze |= version < 5;
    const auto watchScreen = [this](QScreen *screen) {
        connect(screen, &QScreen::geometryChanged, this, [this] {
            Cancel();
        });
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] {
            Cancel();
        });
    };
    for (auto *screen : qGuiApp->screens()) {
        watchScreen(screen);
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this, watchScreen](QScreen *screen) {
        Cancel();
        watchScreen(screen);
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        Cancel();
        m_windows.clear();
    });
}

FastCapture::~FastCapture() = default;

void FastCapture::Capture()
{
    if (m_active) {
        // Repeated shortcut presses cancel instead of creating overlapping grabs.
        Cancel();
        return;
    }
    m_timer.start();
    m_frameReported = false;
    m_active = true;
    m_selection = {};
    m_images.clear();
    m_windows.clear();
    const auto screens = qGuiApp->screens();
    if (screens.isEmpty()) {
        Cancel();
        Q_EMIT failed(u"No displays are available"_s);
        return;
    }
    const quint64 generation = ++m_generation;
    m_pending = m_freeze ? screens.size() : 0;
    for (auto *screen : screens) {
        m_images.append({QRectF(screen->geometry()), {}});
        m_windows.push_back(std::make_unique<RegionWindow>(this, screen));
        const int index = m_images.size() - 1;
        if (!m_freeze) {
            continue;
        }
        auto *grab = new ScreenGrab(u"CaptureScreen"_s, {screen->name()}, this);
        m_grabs.append(grab);
        connect(grab, &ScreenGrab::finished, this, [this, generation, index](const QImage &image, const QString &error) {
            if (generation != m_generation || !m_active) {
                return;
            }
            if (!error.isEmpty()) {
                Cancel();
                Q_EMIT failed(error);
                return;
            }
            m_images[index].image = image;
            if (--m_pending == 0) {
                showImages();
            }
        });
    }
    if (!m_freeze) {
        showImages();
    }
}

void FastCapture::showImages()
{
    for (size_t i = 0; i < m_windows.size(); ++i) {
        m_windows[i]->present(m_images[i].image);
    }
    Q_EMIT ready();
}

QRectF FastCapture::selection() const
{
    return m_selection;
}

void FastCapture::press(const QPointF &position)
{
    if (!m_active || m_pending != 0) {
        return;
    }
    m_anchor = position;
    m_dragging = true;
    m_selection = {};
    redraw();
}

void FastCapture::move(const QPointF &position)
{
    if (m_dragging) {
        m_selection = QRectF(m_anchor, position).normalized();
        redraw();
    }
}

void FastCapture::release(const QPointF &position)
{
    if (!m_dragging) {
        return;
    }
    move(position);
    m_dragging = false;
    if (m_selection.width() < 1 || m_selection.height() < 1) {
        m_selection = {};
        redraw();
        return;
    }
    if (!m_freeze) {
        for (const auto &window : m_windows) {
            window->releaseImage();
        }
        const auto rect = m_selection.toAlignedRect();
        if (qEnvironmentVariableIsSet("SPECTACLE_FAST_TRACE")) {
            qInfo() << "SPECTACLE_FAST_CAPTURE_RECT" << rect;
        }
        const quint64 generation = m_generation;
        m_pending = 1;
        auto *grab = new ScreenGrab(u"CaptureArea"_s, {rect.x(), rect.y(), quint32(rect.width()), quint32(rect.height())}, this);
        m_grabs.append(grab);
        connect(grab, &ScreenGrab::finished, this, [this, generation](const QImage &image, const QString &error) {
            if (generation != m_generation || !m_active) {
                return;
            }
            if (!error.isEmpty()) {
                Cancel();
                Q_EMIT failed(error);
            } else {
                copyImage(image);
            }
        });
        return;
    }
    for (const auto &window : m_windows) {
        window->hide();
    }
    copyImage(cropScreens(m_images, m_selection));
}

void FastCapture::copyImage(QImage image)
{
    if (qEnvironmentVariableIsSet("SPECTACLE_FAST_TRACE")) {
        qInfo() << "SPECTACLE_FAST_COPY_SIZE" << image.size();
    }
    for (const auto &window : m_windows) {
        window->hide();
    }
    if (image.isNull()) {
        Cancel();
        return;
    }
    image.setDevicePixelRatio(1);
    // Retain PNG only; ImageMimeData decodes for Qt consumers on demand.
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        Cancel();
        Q_EMIT failed(u"Could not encode screenshot for the clipboard"_s);
        return;
    }
    KSystemClipboard::instance()->setMimeData(new ImageMimeData(png,
                                                                this,
                                                                [this] {
                                                                    if (!m_active)
                                                                        trimHeap();
                                                                }),
                                              QClipboard::Clipboard);
    cleanup();
    Q_EMIT completed(true);
}

void FastCapture::Cancel()
{
    if (!m_active) {
        return;
    }
    cleanup();
    Q_EMIT completed(false);
}

void FastCapture::cleanup()
{
    ++m_generation;
    m_active = false;
    m_dragging = false;
    m_pending = 0;
    m_selection = {};
    for (const auto &window : m_windows) {
        window->releaseImage();
    }
    m_images.clear();
    for (const auto &grab : std::as_const(m_grabs)) {
        if (grab) {
            grab->deleteLater();
        }
    }
    m_grabs.clear();
    // Run after screenshot transports and Wayland buffers have been destroyed.
    QTimer::singleShot(0, this, [this] {
        if (!m_active)
            trimHeap();
    });
    QTimer::singleShot(200, this, [this] {
        if (!m_active)
            trimHeap();
    });
}

void FastCapture::redraw()
{
    for (const auto &window : m_windows) {
        window->requestUpdate();
    }
}

void FastCapture::framePresented()
{
    if (!m_frameReported) {
        m_frameReported = true;
        if (qEnvironmentVariableIsSet("SPECTACLE_FAST_TRACE")) {
            qInfo() << "SPECTACLE_FAST_FRAME_MS" << m_timer.nsecsElapsed() / 1000000.0;
        }
    }
}

QList<RegionWindow *> FastCapture::windows() const
{
    QList<RegionWindow *> result;
    for (const auto &window : m_windows) {
        result.append(window.get());
    }
    return result;
}

QVariantMap FastCapture::State() const
{
    return {{u"active"_s, m_active}, {u"pending"_s, m_pending}, {u"frozen"_s, m_freeze}};
}

#include "FastCapture.moc"
