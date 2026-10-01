/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QVariantMap>
#include <QWindow>
#include <memory>
#include <vector>

class QBackingStore;
class QScreen;
class ScreenGrab;
class FastCapture;

struct ScreenImage {
    QRectF geometry;
    QImage image;
};

// Pixel endpoints are exclusive; reverse drags and fractional scales have the
// same bounds. A single-screen crop preserves that screen's native pixel scale.
QImage cropScreens(const QList<ScreenImage> &screens, const QRectF &selection);

class RegionWindow final : public QWindow
{
    Q_OBJECT
public:
    RegionWindow(FastCapture *capture, QScreen *screen);
    ~RegionWindow() override;
    void present(const QImage &image);
    void releaseImage();
    void render();

protected:
    bool event(QEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    FastCapture *m_capture;
    QImage m_image;
    std::unique_ptr<QBackingStore> m_store;
};

class FastCapture final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Spectacle.Fast")
public:
    explicit FastCapture(bool freeze = false, QObject *parent = nullptr);
    ~FastCapture() override;
    QRectF selection() const;
    void press(const QPointF &position);
    void move(const QPointF &position);
    void release(const QPointF &position);
    void framePresented();
    QList<RegionWindow *> windows() const;

public Q_SLOTS:
    void Capture();
    void Cancel();
    QVariantMap State() const;

Q_SIGNALS:
    void ready();
    void completed(bool copied);
    void failed(const QString &message);

private:
    void showImages();
    void copyImage(QImage image);
    void cleanup();
    void redraw();
    QList<ScreenImage> m_images;
    QList<QPointer<ScreenGrab>> m_grabs;
    std::vector<std::unique_ptr<RegionWindow>> m_windows;
    QPointF m_anchor;
    QRectF m_selection;
    QElapsedTimer m_timer;
    quint64 m_generation = 0;
    int m_pending = 0;
    bool m_active = false;
    bool m_dragging = false;
    bool m_frameReported = false;
    bool m_freeze = false;
};
