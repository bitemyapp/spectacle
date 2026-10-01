/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FastCapture.h"
#include "ImageMimeData.h"
#include <QBuffer>
#include <QPainter>
#include <QTest>

class GeometryTests : public QObject
{
    Q_OBJECT
private:
    static ScreenImage screen(const QRectF &rect, qreal scale, QColor color)
    {
        QImage image(QSize(qRound(rect.width() * scale), qRound(rect.height() * scale)), QImage::Format_ARGB32_Premultiplied);
        image.fill(color);
        image.setDevicePixelRatio(scale);
        return {rect, image};
    }
private Q_SLOTS:
    void compressedClipboardSupportsQtAndPng()
    {
        QImage original(300, 200, QImage::Format_ARGB32);
        original.fill(Qt::red);
        original.setPixelColor(23, 27, Qt::blue);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(original.save(&buffer, "PNG"));
        const ImageMimeData mime(png);
        QVERIFY(mime.hasImage());
        QVERIFY(mime.hasFormat(QStringLiteral("image/png")));
        QVERIFY(mime.hasFormat(QStringLiteral("x-kde-force-image-copy")));
        QCOMPARE(qvariant_cast<QImage>(mime.imageData()), original);
        QCOMPARE(QImage::fromData(mime.data(QStringLiteral("image/png"))), original);
        QCOMPARE(mime.data(QStringLiteral("image/png")), png);
    }
    void nativePixelsAndReverseDrag()
    {
        const QList<ScreenImage> screens{screen({0, 0, 200, 100}, 2, Qt::red)};
        const auto image = cropScreens(screens, QRectF(QPointF(60, 50), QPointF(10, 20)));
        QCOMPARE(image.size(), QSize(100, 60));
        QCOMPARE(image.devicePixelRatio(), 1);
        QCOMPARE(image.pixelColor(99, 59), QColor(Qt::red));
    }
    void negativeOriginAndClipping()
    {
        const QList<ScreenImage> screens{screen({-200, -100, 200, 100}, 1, Qt::blue)};
        const auto image = cropScreens(screens, {-250, -150, 100, 100});
        QCOMPARE(image.size(), QSize(50, 50));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::blue));
        QVERIFY(cropScreens(screens, {20, 20, 10, 10}).isNull());
        QVERIFY(cropScreens(screens, {-10, -10, 0, 5}).isNull());
    }
    void fractionalPixelBounds()
    {
        const QList<ScreenImage> screens{screen({0, 0, 100, 100}, 1.5, Qt::green)};
        const auto image = cropScreens(screens, {1, 1, 10, 10});
        QCOMPARE(image.size(), QSize(15, 15));
        QCOMPARE(image.pixelColor(14, 14), QColor(Qt::green));
    }
    void mixedScalesAcrossDisplays()
    {
        const QList<ScreenImage> screens{screen({-100, 0, 100, 100}, 1, Qt::red), screen({0, 0, 100, 100}, 2, Qt::blue)};
        const auto image = cropScreens(screens, {-20, 10, 40, 20});
        QCOMPARE(image.size(), QSize(80, 40));
        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(image.pixelColor(70, 10), QColor(Qt::blue));
    }
    void gapsRemainTransparent()
    {
        const QList<ScreenImage> screens{screen({0, 0, 20, 20}, 1, Qt::red), screen({40, 0, 20, 20}, 1, Qt::blue)};
        const auto image = cropScreens(screens, {0, 0, 60, 20});
        QCOMPARE(image.size(), QSize(60, 20));
        QCOMPARE(image.pixelColor(30, 10).alpha(), 0);
    }
};

QTEST_GUILESS_MAIN(GeometryTests)
#include "Tests.moc"
