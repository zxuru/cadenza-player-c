#pragma once

#include "TrackModel.h"

#include <QSortFilterProxyModel>
#include <QString>

/// Case- and accent-insensitive search across the fields a listener would
/// actually type: title, artist, album, album artist and genre.
///
/// Which of them the query is matched against is the listener's to choose, so
/// that a search for "rock" can mean the track called that, or the one filed
/// under the genre.
///
/// Also the object QML talks to for row counts, because the view binds to the
/// proxy rather than to the source model.
class TrackFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TrackFilterProxy(QObject *parent = nullptr);

    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString &query);

    /// The field the query is matched against. `TrackModel::AnyField` -- the
    /// default -- matches every field at once.
    [[nodiscard]] TrackModel::Field field() const { return m_field; }
    void setField(TrackModel::Field field);

    /// Restricts the view to one folder subtree. An empty path lifts the
    /// restriction. Combined with the query, so searching inside a playlist
    /// searches that playlist only.
    [[nodiscard]] QString folder() const { return m_folder; }
    void setFolder(const QString &folder);

    /// Restricts the view to one group of `field`: the tracks whose
    /// `TrackModel::groupKey` is exactly `key`, which is what opening a group
    /// row does. Combined with everything else, so searching inside a group
    /// searches that group only. `clearGroup()` lifts it.
    void setGroup(TrackModel::Field field, const QString &key);
    void clearGroup();
    [[nodiscard]] bool grouped() const { return m_grouped; }

    /// Whether source row `row` passes the folder, the query and the group --
    /// the test `filterAcceptsRow` makes, for the one caller that has to walk
    /// the library itself: the grouping, which reads the model's own rows
    /// rather than asking the proxy for them one index at a time.
    [[nodiscard]] bool acceptsSourceRow(int row) const;

    /// Filesystem path of the track at proxy row `row`, or an empty string.
    [[nodiscard]] QString pathAt(int row) const;

    /// Every path in proxy order -- the queue handed to the player.
    [[nodiscard]] QStringList paths() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    /// Runs `change` -- which sets the members the filter reads -- as one
    /// filter change, and announces it.
    ///
    /// Qt 6.9 replaced `invalidateFilter()` with the begin/end pair, which lets
    /// the proxy keep its persistent indexes instead of rebuilding every row.
    /// The guard keeps the documented 6.5 floor building.
    template <typename Change>
    void changeFilter(Change change)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
        change();
        endFilterChange(Direction::Rows);
#else
        change();
        invalidateFilter();
#endif
    }

    QString m_query;
    /// `m_query` folded to its unaccented lowercase form, computed once per
    /// change rather than once per row.
    QString m_normalisedQuery;
    /// Which of a row's fields `m_normalisedQuery` is tested against.
    TrackModel::Field m_field = TrackModel::AnyField;
    /// The one group the view is narrowed to, and whether there is one: an
    /// empty key is a real group -- the tracks no tag names -- so the flag is
    /// what separates "grouped under nothing" from "not grouped".
    bool m_grouped = false;
    TrackModel::Field m_groupField = TrackModel::AnyField;
    QString m_groupKey;
    QString m_folder;
    /// `m_folder` with a trailing separator, so `/Music/Rock` cannot match
    /// `/Music/Rockabilly`.
    QString m_folderPrefix;
};
