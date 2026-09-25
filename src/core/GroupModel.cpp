#include "GroupModel.h"

#include <QByteArray>
#include <QModelIndex>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace {

/// The tag a group of `field` is named by, as the file holds it: the words for
/// a tag nobody wrote are the interface's, and are added later.
QString valueOf(const Track &track, TrackModel::Field field)
{
    switch (field)
    {
    case TrackModel::AlbumField:
        return track.album;
    case TrackModel::ArtistField:
        return track.artist;
    case TrackModel::GenreField:
        return track.genre;
    default:
        return QString();
    }
}

}  // namespace

GroupModel::GroupModel(const Translator &translator, QObject *parent)
    : QAbstractListModel(parent)
    , m_translator(translator)
{
}

void GroupModel::clear()
{
    if (m_groups.isEmpty() && m_field == TrackModel::AnyField)
        return;

    beginResetModel();
    m_groups.clear();
    m_field = TrackModel::AnyField;
    endResetModel();
}

void GroupModel::rebuild(const TrackModel &model, const TrackFilterProxy &proxy, TrackModel::Field field)
{
    beginResetModel();
    m_groups.clear();
    m_field = field;

    if (field != TrackModel::AnyField)
    {
        const QVector<Track> &tracks = model.tracks();
        QHash<QString, int> rowOf;
        rowOf.reserve(128);

        for (int row = 0; row < tracks.size(); ++row)
        {
            if (!proxy.acceptsSourceRow(row))
                continue;

            const Track &track = tracks.at(row);
            const QString key = model.groupKey(row, field);

            auto found = rowOf.constFind(key);
            if (found == rowOf.constEnd())
            {
                Group group;
                group.key = key;
                // The first track of the group is what the row shows: the tag it
                // is named by, the artist under an album, and the artwork that
                // stands for the whole group.
                group.value = valueOf(track, field);
                group.artist = track.albumArtist;
                group.coverPath = track.path;
                found = rowOf.insert(key, static_cast<int>(m_groups.size()));
                m_groups.append(std::move(group));
            }

            Group &group = m_groups[found.value()];
            ++group.count;
            group.duration += track.duration;

            // An artist row says how many albums as well as how many tracks, so
            // an artist's albums have to be counted as its tracks go by.
            if (field == TrackModel::ArtistField)
                group.albums.insert(model.groupKey(row, TrackModel::AlbumField));
        }

        std::sort(m_groups.begin(), m_groups.end(), [](const Group &left, const Group &right) {
            return QString::localeAwareCompare(left.key, right.key) < 0;
        });

        for (Group &group : m_groups)
            describe(&group, field);
    }

    endResetModel();
}

void GroupModel::retranslate()
{
    if (m_groups.isEmpty())
        return;

    // The names and the lines under them are words this model writes itself,
    // so a language change rewrites them; nothing else about a group moves.
    for (Group &group : m_groups)
        describe(&group, m_field);

    emit dataChanged(index(0, 0), index(m_groups.size() - 1, 0),
                     {NameRole, SubtitleRole});
}

int GroupModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_groups.size());
}

QVariant GroupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size())
        return QVariant();

    const Group &group = m_groups.at(index.row());
    switch (role)
    {
    case KeyRole:
        return group.key;
    case NameRole:
        return group.name;
    case SubtitleRole:
        return group.subtitle;
    case CountRole:
        return group.count;
    case DurationRole:
        return group.duration;
    case CoverPathRole:
        return group.coverPath;
    default:
        break;
    }
    return QVariant();
}

QHash<int, QByteArray> GroupModel::roleNames() const
{
    static const QHash<int, QByteArray> names = {
        {KeyRole, QByteArrayLiteral("key")},
        {NameRole, QByteArrayLiteral("name")},
        {SubtitleRole, QByteArrayLiteral("subtitle")},
        {CountRole, QByteArrayLiteral("count")},
        {DurationRole, QByteArrayLiteral("duration")},
        {CoverPathRole, QByteArrayLiteral("coverPath")},
    };
    return names;
}

void GroupModel::describe(Group *group, TrackModel::Field field) const
{
    // The words for a tag nobody wrote, and the count of what is in the group:
    // both are the interface's, so both are written here rather than stored.
    const QString tracks = m_translator.plural(QStringLiteral("library_tracks"), group->count);

    switch (field)
    {
    case TrackModel::ArtistField:
    {
        const QString albums = m_translator.plural(QStringLiteral("library_group_albums"),
                                                   static_cast<int>(group->albums.size()));
        group->name = group->value.isEmpty()
                          ? m_translator.text(QStringLiteral("library_unknown_artist"))
                          : group->value;
        group->subtitle = tracks + QStringLiteral(" · ") + albums;
        break;
    }
    case TrackModel::AlbumField:
    {
        const QString artist = group->artist.isEmpty()
                                   ? m_translator.text(QStringLiteral("library_unknown_artist"))
                                   : group->artist;
        group->name = group->value.isEmpty()
                          ? m_translator.text(QStringLiteral("library_unknown_album"))
                          : group->value;
        group->subtitle = artist + QStringLiteral(" · ") + tracks;
        break;
    }
    default:
        group->name = group->value.isEmpty()
                          ? m_translator.text(QStringLiteral("library_unknown_genre"))
                          : group->value;
        group->subtitle = tracks;
        break;
    }
}
