/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QImage>
#include <QMimeData>
#include <QPointer>
#include <QTimer>
#include <functional>

// Keep one compressed copy while owning the clipboard. Qt image consumers can
// still request a QImage, decoded on demand without retaining a second bitmap.
class ImageMimeData final : public QMimeData
{
public:
    explicit ImageMimeData(const QByteArray &png, QObject *cleanupContext = nullptr, std::function<void()> afterRead = {})
        : m_context(cleanupContext)
        , m_afterRead(std::move(afterRead))
    {
        setData(QStringLiteral("image/png"), png);
        setData(QStringLiteral("x-kde-force-image-copy"), {});
    }

    QStringList formats() const override
    {
        auto result = QMimeData::formats();
        result.append(QStringLiteral("application/x-qt-image"));
        return result;
    }

    bool hasFormat(const QString &mimeType) const override
    {
        return mimeType == QStringLiteral("application/x-qt-image") || QMimeData::hasFormat(mimeType);
    }

protected:
    QVariant retrieveData(const QString &mimeType, QMetaType type) const override
    {
        if (mimeType == QStringLiteral("application/x-qt-image")) {
            // A paste can happen long after capture cleanup. Yield heap left by
            // temporary decode/conversion buffers once the request completes.
            if (m_context && m_afterRead) {
                QTimer::singleShot(0, m_context, m_afterRead);
                QTimer::singleShot(200, m_context, m_afterRead);
            }
            return QImage::fromData(QMimeData::data(QStringLiteral("image/png")), "PNG");
        }
        return QMimeData::retrieveData(mimeType, type);
    }

private:
    QPointer<QObject> m_context;
    std::function<void()> m_afterRead;
};
