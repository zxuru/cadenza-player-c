#include "Track.h"

#include <QFileInfo>
#include <QStringList>

QStringList audioExtensions()
{
    // Built once: the scanner calls this for every file it walks, and the list
    // is constant for the life of the process.
    static const QStringList extensions{
        QStringLiteral(".flac"),
        QStringLiteral(".mp3"),
        QStringLiteral(".m4a"),
        QStringLiteral(".mp4"),
        QStringLiteral(".ogg"),
        QStringLiteral(".oga"),
        QStringLiteral(".opus"),
        QStringLiteral(".wav"),
        QStringLiteral(".aiff"),
        QStringLiteral(".aif"),
        QStringLiteral(".wv"),
        QStringLiteral(".ape"),
        QStringLiteral(".mpc"),
    };
    return extensions;
}

QString audioExtension(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        return QString();
    return QStringLiteral(".") + suffix;
}

QString Track::displayTitle() const
{
    if (!title.isEmpty())
        return title;
    // An untagged file is still nameable by its own name.
    return QFileInfo(path).completeBaseName();
}

QString Track::displayArtist() const
{
    if (!artist.isEmpty())
        return artist;
    return albumArtist;
}

QString Track::subtitle(const QString &unknownArtist) const
{
    // Joined rather than concatenated so a missing half never leaves a
    // separator hanging off one end.
    QStringList parts;
    const QString named = displayArtist();
    const QString performer = named.isEmpty() ? unknownArtist : named;
    if (!performer.isEmpty())
        parts.append(performer);
    if (!album.isEmpty())
        parts.append(album);
    return parts.join(QStringLiteral(" · "));
}
