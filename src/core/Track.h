#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

/// One indexed audio file. Mirrors the columns of the `tracks` table so a row
/// can be read straight out of SQLite without any reshaping.
struct Track
{
    QString path;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    QString year;
    int trackNo = 0;
    int discNo = 0;
    double duration = 0.0;

    /// Falls back to the file stem, so a file with no tags is still nameable.
    [[nodiscard]] QString displayTitle() const;

    /// The artist the tags name, else the album artist, else nothing: a file
    /// nobody named is named by the interface, in its own language.
    [[nodiscard]] QString displayArtist() const;

    /// `artist · album`, skipping whichever half is empty. `unknownArtist`
    /// stands in when no tag names one; the model passes it in, because those
    /// are words rather than data.
    [[nodiscard]] QString subtitle(const QString &unknownArtist) const;

    [[nodiscard]] bool isValid() const { return !path.isEmpty(); }
};

/// Every extension the scanner will look at. Anything else is ignored even if
/// it happens to be parseable, matching the Python implementation.
[[nodiscard]] QStringList audioExtensions();

/// Lowercase extension of `path`, including the leading dot.
[[nodiscard]] QString audioExtension(const QString &path);

Q_DECLARE_METATYPE(Track)
