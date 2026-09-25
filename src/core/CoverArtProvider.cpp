#include "CoverArtProvider.h"

#include "Track.h"

#include <QByteArray>
#include <QCache>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QRect>
#include <QStringList>
#include <QUrl>

#include <taglib/aifffile.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/mp4file.h>
#include <taglib/mp4item.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/opusfile.h>
#include <taglib/tbytevector.h>
#include <taglib/tlist.h>
#include <taglib/tstring.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavfile.h>
#include <taglib/xiphcomment.h>

namespace {

/// A decoded cover costs a few hundred kilobytes, so only the visible window
/// plus the now-playing pane is kept.
constexpr int kCacheEntries = 64;

/// Qt calls `requestImage` on the scene-graph render thread while the library
/// list builds its thumbnails on the GUI thread, so the cache is shared behind
/// a mutex.
struct ArtworkCache
{
    QMutex mutex;
    QCache<QString, QImage> entries{kCacheEntries};
};

/// A function-local static is initialised under a guard, which a namespace
/// scope object would not be.
ArtworkCache &artworkCache()
{
    static ArtworkCache cache;
    return cache;
}

/// The cached image for `path`, or a null image. Takes the lock once.
QImage cachedArtwork(const QString &path)
{
    ArtworkCache &cache = artworkCache();
    QMutexLocker locker(&cache.mutex);

    if (const QImage *entry = cache.entries.object(path))
        return *entry;

    return QImage();
}

/// A backdrop is the size of the window, so the variant cache is bounded by
/// bytes rather than by entries: a couple of them fit, sixty would not.
constexpr int kVariantCacheBytes = 48 * 1024 * 1024;

struct VariantCache
{
    QMutex mutex;
    QCache<QString, QImage> entries{kVariantCacheBytes};
};

VariantCache &variantCache()
{
    static VariantCache cache;
    return cache;
}

/// The transforms a URL asks for. Zero means that step is left out.
struct Variant
{
    QString path;
    QSize size;
    int blur = 0;
    int round = 0;

    [[nodiscard]] bool transformed() const
    {
        return size.isValid() || blur > 0 || round > 0;
    }
};

/// Splits `image://cover/<encoded path>?w=&h=&blur=&round=` into its parts.
/// The path is percent-encoded, so a `?` in a file name arrives as `%3F` and
/// cannot be mistaken for the query.
Variant splitId(const QString &id)
{
    Variant variant;

    const int query = id.indexOf(QLatin1Char('?'));
    variant.path = QUrl::fromPercentEncoding((query < 0 ? id : id.left(query)).toUtf8());

    if (query < 0)
        return variant;

    const QStringList params = id.mid(query + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString &param : params) {
        const int equals = param.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;

        const QString key = param.left(equals);
        const int value = param.mid(equals + 1).toInt();

        if (key == QLatin1String("w"))
            variant.size.setWidth(value);
        else if (key == QLatin1String("h"))
            variant.size.setHeight(value);
        else if (key == QLatin1String("blur"))
            variant.blur = value;
        else if (key == QLatin1String("round"))
            variant.round = value;
    }

    return variant;
}

QImage cachedVariant(const QString &id)
{
    VariantCache &cache = variantCache();
    QMutexLocker locker(&cache.mutex);

    if (const QImage *entry = cache.entries.object(id))
        return *entry;

    return QImage();
}

void cacheVariant(const QString &id, const QImage &image)
{
    VariantCache &cache = variantCache();
    QMutexLocker locker(&cache.mutex);
    cache.entries.insert(id, new QImage(image), int(image.sizeInBytes()));
}

/// Scales `source` to fill `target` and crops the middle of the overflow, so
/// the result is exactly `target` and the picture is never stretched.
QImage coverCropped(const QImage &source, const QSize &target)
{
    QImage scaled = source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.size() == target)
        return scaled;

    const QPoint offset((scaled.width() - target.width()) / 2,
                        (scaled.height() - target.height()) / 2);
    return scaled.copy(QRect(offset, target));
}

/// One box pass along a line of `length` pixels `step` apart, summed over a
/// moving window: O(length) whatever the radius, which is what lets a
/// window-sized backdrop be blurred while a resize is still running.
void boxPass(const QRgb *source, QRgb *target, int length, int step, int radius)
{
    const int window = 2 * radius + 1;
    int red = 0;
    int green = 0;
    int blue = 0;

    for (int i = -radius; i <= radius; ++i) {
        const QRgb pixel = source[qBound(0, i, length - 1) * step];
        red += qRed(pixel);
        green += qGreen(pixel);
        blue += qBlue(pixel);
    }

    for (int i = 0; i < length; ++i) {
        target[i * step] = qRgb(red / window, green / window, blue / window);

        const QRgb leaving = source[qBound(0, i - radius, length - 1) * step];
        const QRgb entering = source[qBound(0, i + radius + 1, length - 1) * step];
        red += qRed(entering) - qRed(leaving);
        green += qGreen(entering) - qGreen(leaving);
        blue += qBlue(entering) - qBlue(leaving);
    }
}

/// Three box passes, which are close enough to a gaussian at this scale. They
/// run on a quarter-size copy that is scaled back up afterwards: at a blur
/// this wide the difference cannot be seen, and the cost per backdrop stays
/// near a millisecond.
QImage blurred(const QImage &image, int radius)
{
    QImage work = image
                      .scaled(QSize(qMax(8, image.width() / 4), qMax(8, image.height() / 4)),
                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_RGB32);
    QImage scratch(work.size(), QImage::Format_RGB32);

    const int passRadius = qMax(1, radius / 4);
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < work.height(); ++y) {
            boxPass(reinterpret_cast<const QRgb *>(work.constScanLine(y)),
                    reinterpret_cast<QRgb *>(scratch.scanLine(y)),
                    work.width(), 1, passRadius);
        }
        work.swap(scratch);

        for (int x = 0; x < work.width(); ++x) {
            boxPass(reinterpret_cast<const QRgb *>(work.constScanLine(0)) + x,
                    reinterpret_cast<QRgb *>(scratch.scanLine(0)) + x,
                    work.height(), work.width(), passRadius);
        }
        work.swap(scratch);
    }

    return work.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/// Keeps the picture only where a rounded rectangle has alpha, so the corners
/// end up in the image itself. That is what lets a rounded pane be drawn
/// without masking the item on top of it.
///
/// The mask is drawn first and the picture composited into it, rather than the
/// other way round: a composition mode only reaches the pixels the drawn shape
/// covers, so `DestinationIn` over the picture would keep the picture's own
/// corners exactly as they were.
QImage rounded(const QImage &image, int radius)
{
    if (radius <= 0)
        return image;

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    // Half a pixel in: a shape sitting exactly on the edge of the image would
    // leave its outermost row and column half transparent.
    painter.drawRoundedRect(QRectF(out.rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.drawImage(0, 0, image);

    return out;
}

/// The finished variant. Cropped, softened, rounded, in that order: the blur
/// has to run before the corners exist or it would smear them.
QImage variantImage(const QImage &source, const Variant &variant)
{
    if (source.isNull())
        return source;

    QImage image = coverCropped(source, variant.size.isValid() ? variant.size : source.size());

    if (variant.blur > 0)
        image = blurred(image, variant.blur);

    return rounded(image, variant.round);
}

/// TagLib hands the picture back as raw file bytes in every format below.
QImage imageFrom(const TagLib::ByteVector &data)
{
    if (data.isEmpty())
        return QImage();

    return QImage::fromData(QByteArray(data.data(), int(data.size())));
}

/// ID3v2 keeps embedded art in an APIC frame; MP3, WAV and AIFF all use it.
QImage imageFromId3v2(TagLib::ID3v2::Tag *tag)
{
    if (!tag || tag->frameList("APIC").isEmpty())
        return QImage();

    auto *frame = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(tag->frameList("APIC").front());
    if (!frame)
        return QImage();

    return imageFrom(frame->picture());
}

QImage decode(const QString &path)
{
    // TagLib keeps the pointer it is handed, so the encoded name outlives
    // every file object below.
    const QByteArray fileName = QFile::encodeName(path);
    const QString extension = audioExtension(path);

    if (extension == QStringLiteral(".flac"))
    {
        TagLib::FLAC::File file(fileName.constData(), false);
        if (!file.isValid())
            return QImage();

        const TagLib::List<TagLib::FLAC::Picture *> pictures = file.pictureList();
        if (pictures.isEmpty())
            return QImage();

        return imageFrom(pictures.front()->data());
    }

    if (extension == QStringLiteral(".mp3"))
    {
        TagLib::MPEG::File file(fileName.constData(), false);
        if (!file.isValid())
            return QImage();

        return imageFromId3v2(file.ID3v2Tag());
    }

    if (extension == QStringLiteral(".m4a") || extension == QStringLiteral(".mp4"))
    {
        TagLib::MP4::File file(fileName.constData(), false);
        if (!file.isValid())
            return QImage();

        TagLib::MP4::Tag *tag = file.tag();
        if (!tag)
            return QImage();

        const TagLib::MP4::Item item = tag->item("covr");
        if (!item.isValid())
            return QImage();

        const TagLib::MP4::CoverArtList covers = item.toCoverArtList();
        if (covers.isEmpty())
            return QImage();

        return imageFrom(covers.front().data());
    }

    if (extension == QStringLiteral(".ogg") || extension == QStringLiteral(".oga"))
    {
        TagLib::Ogg::Vorbis::File file(fileName.constData(), false);
        if (!file.isValid())
            return QImage();

        TagLib::Ogg::XiphComment *tag = file.tag();
        if (!tag)
            return QImage();

        const TagLib::List<TagLib::FLAC::Picture *> pictures = tag->pictureList();
        if (pictures.isEmpty())
            return QImage();

        return imageFrom(pictures.front()->data());
    }

    if (extension == QStringLiteral(".opus"))
    {
        TagLib::Ogg::Opus::File file(fileName.constData(), false);
        if (!file.isValid())
            return QImage();

        TagLib::Ogg::XiphComment *tag = file.tag();
        if (!tag)
            return QImage();

        const TagLib::List<TagLib::FLAC::Picture *> pictures = tag->pictureList();
        if (pictures.isEmpty())
            return QImage();

        return imageFrom(pictures.front()->data());
    }

    if (extension == QStringLiteral(".wav"))
    {
        TagLib::RIFF::WAV::File file(fileName.constData(), false);
        if (!file.isValid() || !file.hasID3v2Tag())
            return QImage();

        return imageFromId3v2(file.ID3v2Tag());
    }

    if (extension == QStringLiteral(".aiff") || extension == QStringLiteral(".aif"))
    {
        TagLib::RIFF::AIFF::File file(fileName.constData(), false);
        if (!file.isValid() || !file.hasID3v2Tag())
            return QImage();

        return imageFromId3v2(file.tag());
    }

    // The scanner also accepts `.wv`, `.ape` and `.mpc`; none of them has a
    // standard picture field, so they fall through to no artwork.
    return QImage();
}

}  // namespace

CoverArtProvider::CoverArtProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage CoverArtProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const Variant variant = splitId(id);

    // The variant is keyed by the whole id: the same cover is asked for at
    // several sizes and radii over a session.
    if (variant.transformed()) {
        if (const QImage cached = cachedVariant(id); !cached.isNull()) {
            if (size)
                *size = cached.size();
            return cached;
        }

        const QImage made = variantImage(artwork(variant.path), variant);
        if (!made.isNull())
            cacheVariant(id, made);

        if (size)
            *size = made.size();
        return made;
    }

    const QImage image = artwork(variant.path);

    if (size)
        *size = image.size();

    if (image.isNull() || !requestedSize.isValid())
        return image;

    // The row thumbnails ask for 44x44: scaling here keeps every delegate from
    // holding a full-size cover of its own.
    return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString CoverArtProvider::urlFor(const QString &path, QSize size, int blur, int round)
{
    if (path.isEmpty())
        return QString();

    QString url =
        QStringLiteral("image://cover/") + QString::fromLatin1(QUrl::toPercentEncoding(path));

    QStringList params;
    if (size.isValid()) {
        params.append(QStringLiteral("w=") + QString::number(size.width()));
        params.append(QStringLiteral("h=") + QString::number(size.height()));
    }
    if (blur > 0)
        params.append(QStringLiteral("blur=") + QString::number(blur));
    if (round > 0)
        params.append(QStringLiteral("round=") + QString::number(round));

    if (!params.isEmpty())
        url += QLatin1Char('?') + params.join(QLatin1Char('&'));

    return url;
}

QImage CoverArtProvider::artwork(const QString &path)
{
    if (path.isEmpty())
        return QImage();

    if (const QImage cached = cachedArtwork(path); !cached.isNull())
        return cached;

    // A decode can take tens of milliseconds, so the lock is not held across
    // it: the render thread would otherwise queue behind TagLib.
    const QImage decoded = decode(path);
    if (decoded.isNull())
        return decoded;

    // Another thread may have decoded the same cover meanwhile.
    if (const QImage raced = cachedArtwork(path); !raced.isNull())
        return raced;

    ArtworkCache &cache = artworkCache();
    QMutexLocker locker(&cache.mutex);
    cache.entries.insert(path, new QImage(decoded));

    return decoded;
}

QString CoverArtProvider::decodeId(const QString &id)
{
    return QUrl::fromPercentEncoding(id.toUtf8());
}
