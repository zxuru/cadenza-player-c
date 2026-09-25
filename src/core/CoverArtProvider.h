#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

/// Serves embedded cover art to QML as
/// `image://cover/<percent-encoded-path>`.
///
/// The id may carry a query string, which is how the interface asks for a
/// variant instead of the stored picture:
///
///     ?w=<px>&h=<px>   crop to that box, centred, never stretched
///     &blur=<px>       soften by that many output pixels
///     &round=<px>      cut the corners to that output radius
///
/// All three are baked into the image, which is why a rounded, blurred
/// backdrop needs no shader to draw: it arrives with the corners already in
/// its alpha channel, and it is cached like any other picture.
///
/// Qt calls `requestImage` on the scene-graph thread, so the decode cache is
/// guarded by a mutex. Decoding goes through TagLib, which reads the picture
/// out of FLAC blocks, ID3 APIC frames, MP4 `covr` atoms and Ogg
/// `METADATA_BLOCK_PICTURE` comments without loading the audio stream.
class CoverArtProvider : public QQuickImageProvider
{
public:
    CoverArtProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    /// `image://cover/...` for `path`. A zero `size`, `blur` or `round` leaves
    /// that step out, so `urlFor(path)` is the picture as stored.
    [[nodiscard]] static QString urlFor(const QString &path,
                                        QSize size = {},
                                        int blur = 0,
                                        int round = 0);

private:
    /// Decoded artwork, or a null image when the file carries none.
    /// Cached by path.
    [[nodiscard]] static QImage artwork(const QString &path);

    /// Percent-decodes the path half of the id.
    [[nodiscard]] static QString decodeId(const QString &id);
};
