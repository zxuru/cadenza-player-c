#include "TrackModel.h"

#include <QByteArray>
#include <QChar>
#include <QHash>
#include <QStringView>
#include <QVariant>

#include <iterator>
#include <utility>

namespace {

/// Between the folded fields packed into one string per track. The unit
/// separator is a character no tag editor writes and no keyboard can type, so
/// a field can always be sliced back out of the pack.
constexpr QChar kFieldSeparator = QChar(0x1F);

/// The raw fields of a track, in `TrackModel::Field` order, joined by the
/// separator: that order is what makes the pack addressable by field, and an
/// empty field keeps its place so the ones after it do not shift.
QString pack(const Track &track)
{
    QString packed;
    packed.reserve(track.title.size() + track.artist.size() + track.album.size()
                   + track.albumArtist.size() + track.genre.size() + 4);

    const QString fields[] = {track.title, track.artist, track.album,
                              track.albumArtist, track.genre};
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        // Unconditionally, so an empty field still holds its place: the pack is
        // read back by position.
        if (i > 0)
            packed.append(kFieldSeparator);
        packed.append(fields[i]);
    }
    return packed;
}

/// The segment of a pack that holds field `index` -- `TitleField` counting from
/// zero -- or an empty view when the pack is shorter than that.
QStringView segment(const QString &packed, int index)
{
    int start = 0;
    for (int i = 0; i < index; ++i) {
        const int separator = packed.indexOf(kFieldSeparator, start);
        if (separator < 0)
            return {};
        start = separator + 1;
    }

    int end = packed.indexOf(kFieldSeparator, start);
    if (end < 0)
        end = packed.size();

    return QStringView(packed).sliced(start, end - start);
}

}  // namespace

TrackModel::TrackModel(const Translator &translator, QObject *parent)
    : QAbstractListModel(parent)
    , m_translator(translator)
{
}

QString TrackModel::normalise(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);

    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (character.category() != QChar::Mark_NonSpacing)
            folded.append(character.toLower());
    }
    return folded;
}

bool TrackModel::rowMatches(int row, Field field, const QString &normalised) const
{
    if (normalised.isEmpty())
        return true;
    if (row < 0 || row >= m_folded.size())
        return false;

    const QString &packed = m_folded.at(row);

    // The whole row at once. The separators are in the way of nothing: a query
    // cannot contain one, so no match can straddle two fields.
    if (field == AnyField)
        return QStringView(packed).contains(normalised);

    if (segment(packed, field - TitleField).contains(normalised))
        return true;

    // An album row names its artist under the album, so both are searched:
    // typing an artist's name while browsing albums has to keep their albums.
    return field == AlbumField
           && segment(packed, AlbumArtistField - TitleField).contains(normalised);
}

QString TrackModel::foldedField(int row, Field field) const
{
    if (field == AnyField || row < 0 || row >= m_folded.size())
        return QString();

    return segment(m_folded.at(row), field - TitleField).toString();
}

QString TrackModel::groupKey(int row, Field field) const
{
    if (field == AnyField)
        return QString();

    // The album artist leads an album's key: sorting the keys then sorts albums
    // by artist and by album, which is the order the library keeps tracks in,
    // and two albums sharing a name stay apart.
    if (field == AlbumField)
        return foldedField(row, AlbumArtistField) + kFieldSeparator + foldedField(row, AlbumField);

    return foldedField(row, field);
}

void TrackModel::setTracks(QVector<Track> tracks)
{
    beginResetModel();
    m_tracks = std::move(tracks);

    m_rowByPath.clear();
    m_rowByPath.reserve(m_tracks.size());
    m_folded.clear();
    m_folded.reserve(m_tracks.size());

    for (int row = 0; row < m_tracks.size(); ++row) {
        const Track &track = m_tracks.at(row);
        m_rowByPath.insert(track.path, row);

        // Folded once here, in one pass over the whole pack rather than one
        // decomposition per field, and never inside the proxy's per-row filter:
        // that is what keeps a keystroke one substring test per row.
        m_folded.append(normalise(pack(track)));
    }

    endResetModel();
}

void TrackModel::clear()
{
    setTracks(QVector<Track>());
}

int TrackModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_tracks.size());
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return QVariant();

    const Track &track = m_tracks.at(index.row());
    switch (role) {
    case PathRole:
        return track.path;
    case TitleRole:
        return track.title;
    case ArtistRole:
        return track.artist;
    case AlbumRole:
        return track.album;
    case AlbumArtistRole:
        return track.albumArtist;
    case GenreRole:
        return track.genre;
    case YearRole:
        return track.year;
    case TrackNoRole:
        return track.trackNo;
    case DiscNoRole:
        return track.discNo;
    case DurationRole:
        return track.duration;
    case DisplayTitleRole:
        return track.displayTitle();
    case DisplayArtistRole:
        return track.displayArtist();
    case SubtitleRole:
        return track.subtitle(m_translator.text(QStringLiteral("library_unknown_artist")));
    default:
        break;
    }
    return QVariant();
}

QHash<int, QByteArray> TrackModel::roleNames() const
{
    // Built once: the names never change and the view asks for the map on every
    // reset.
    static const QHash<int, QByteArray> names = {
        {PathRole, QByteArrayLiteral("path")},
        {TitleRole, QByteArrayLiteral("title")},
        {ArtistRole, QByteArrayLiteral("artist")},
        {AlbumRole, QByteArrayLiteral("album")},
        {AlbumArtistRole, QByteArrayLiteral("albumArtist")},
        {GenreRole, QByteArrayLiteral("genre")},
        {YearRole, QByteArrayLiteral("year")},
        {TrackNoRole, QByteArrayLiteral("trackNo")},
        {DiscNoRole, QByteArrayLiteral("discNo")},
        {DurationRole, QByteArrayLiteral("duration")},
        {DisplayTitleRole, QByteArrayLiteral("displayTitle")},
        {DisplayArtistRole, QByteArrayLiteral("displayArtist")},
        {SubtitleRole, QByteArrayLiteral("subtitle")},
    };
    return names;
}

void TrackModel::retranslate()
{
    if (m_tracks.isEmpty())
        return;

    emit dataChanged(index(0, 0), index(m_tracks.size() - 1, 0), {SubtitleRole});
}

int TrackModel::rowForPath(const QString &path) const
{
    return m_rowByPath.value(path, -1);
}
