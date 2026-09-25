#pragma once

#include "Track.h"
#include "Translator.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

/// Exposes the indexed library to QML as a proper model.
///
/// A `QVariantList` of fifty thousand maps would cost tens of megabytes and
/// re-create every delegate on each change; a list model lets the view ask for
/// only the rows it is about to paint.
class TrackModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        AlbumArtistRole,
        GenreRole,
        YearRole,
        TrackNoRole,
        DiscNoRole,
        DurationRole,
        DisplayTitleRole,
        DisplayArtistRole,
        SubtitleRole,
    };
    Q_ENUM(Role)

    /// Which of a track's fields a search looks in, and what a group is a group
    /// of. `AnyField` is the whole row, which is the flat track list.
    ///
    /// The order is the order of the fields inside the folded pack built by
    /// `setTracks`, so this enum is also the pack's layout.
    enum Field {
        AnyField,
        TitleField,
        ArtistField,
        AlbumField,
        AlbumArtistField,
        GenreField,
    };
    Q_ENUM(Field)

    explicit TrackModel(const Translator &translator, QObject *parent = nullptr);

    /// Lowercased, Unicode-decomposed form of `text` with the combining marks
    /// removed, so that typing "cancion" finds "Canción".
    [[nodiscard]] static QString normalise(const QString &text);

    /// Whether `field` of row `row` contains `normalised`, which must already
    /// have been through `normalise()`. `AnyField` tests every field, and
    /// `AlbumField` tests the album and the album artist: an album row names
    /// both, so both are what a listener would expect to be searched.
    ///
    /// The whole cost of a keystroke: the folded text is built once, when the
    /// library loads, so this is one substring test per row rather than a
    /// Unicode decomposition of five strings.
    [[nodiscard]] bool rowMatches(int row, Field field, const QString &normalised) const;

    /// What row `row` is grouped under when the list is grouped by `field`.
    ///
    /// Opaque to everyone else: it is the folded value, and for an album the
    /// album artist in front of it, so that two albums sharing a name stay
    /// apart -- and so that sorting the keys sorts albums by artist and then by
    /// album, the order the library keeps tracks in.
    [[nodiscard]] QString groupKey(int row, Field field) const;

    void setTracks(QVector<Track> tracks);
    void clear();

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Row for `path`, or -1. Linear, but only walked when the playing track
    /// changes, not on every position tick.
    [[nodiscard]] int rowForPath(const QString &path) const;

    /// Announces the rows whose text the model writes itself -- the name of an
    /// artist no tag names -- because the language it is written in changed.
    void retranslate();

    [[nodiscard]] const QVector<Track> &tracks() const { return m_tracks; }

private:
    /// The folded value of `field` in row `row`: what a group key is made of.
    [[nodiscard]] QString foldedField(int row, Field field) const;

    /// Where the words the model writes itself come from.
    const Translator &m_translator;
    QVector<Track> m_tracks;
    QHash<QString, int> m_rowByPath;
    /// Parallel to `m_tracks`: every searchable field of a track, folded and
    /// packed into one string separated by `kFieldSeparator`, so that a search
    /// scoped to a single field costs no second copy of the text. Built once
    /// per load, never per keystroke.
    QVector<QString> m_folded;
};
