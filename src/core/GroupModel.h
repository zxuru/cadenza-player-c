#pragma once

#include "Track.h"
#include "TrackFilterProxy.h"
#include "TrackModel.h"
#include "Translator.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

/// The library seen as one row per artist, album or genre: what the list shows
/// when the browse mode is not the flat track list.
///
/// A group is the rows the filter accepts, keyed by the field being browsed, so
/// a playlist, a search and the browse mode all narrow the groups exactly the
/// way they narrow the tracks. Rebuilt in one pass over the library -- a hash
/// insert per accepted row -- because while a grouped mode is showing it is
/// rebuilt on every keystroke.
class GroupModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        /// What the row stands for, to be handed back when it is opened:
        /// `TrackModel::groupKey`, opaque here.
        KeyRole = Qt::UserRole + 1,
        /// The tag itself, or the word for a track that carries none.
        NameRole,
        /// The line under it: the album artist, and how much is in the group.
        SubtitleRole,
        CountRole,
        DurationRole,
        /// Path of a track of the group, whose artwork stands for the whole
        /// group: the one picture a group has.
        CoverPathRole,
    };
    Q_ENUM(Role)

    explicit GroupModel(const Translator &translator, QObject *parent = nullptr);

    /// Rebuilds from every row of `model` that `proxy` accepts, grouped by
    /// `field`. `AnyField` -- the flat list -- empties the model, because a
    /// group of everything is not a list anybody asked for.
    void rebuild(const TrackModel &model, const TrackFilterProxy &proxy, TrackModel::Field field);
    void clear();

    /// The field the rows are grouped by, as the model was last rebuilt.
    [[nodiscard]] TrackModel::Field field() const { return m_field; }

    /// Rewrites the lines this model writes itself -- the count under a name,
    /// and the words for a track that carries no tag -- because the language
    /// they are written in changed.
    void retranslate();

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    struct Group
    {
        /// Sort order as well as identity: folded, and for an album the album
        /// artist in front of the album.
        QString key;
        /// The tag itself, as the file holds it, empty when it carries none.
        QString value;
        /// The album artist of an album group: the line under the name, and
        /// what tells two albums sharing a name apart.
        QString artist;
        QString name;
        QString subtitle;
        QString coverPath;
        int count = 0;
        double duration = 0.0;
        /// The albums an artist's tracks come from, so that an artist row can
        /// say how many there are rather than only how many tracks.
        QSet<QString> albums;
    };

    /// Writes the name and the line under it, in the language in force: the
    /// only parts of a group that are words rather than data.
    void describe(Group *group, TrackModel::Field field) const;

    /// Declared first: everything below that answers in words asks it for them.
    const Translator &m_translator;
    QVector<Group> m_groups;
    TrackModel::Field m_field = TrackModel::AnyField;
};
