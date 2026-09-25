#pragma once

#include "Translator.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

/// One row of an online search, and how far its download got.
///
/// A catalogue hands back items, not files, and an item is whatever its
/// uploader assembled: a single recording, or a whole release. `fileCount` is
/// how many tracks a release turned out to hold, which is known from the moment
/// the search resolved it.
struct DownloadItem
{
    /// Unique per source: the YouTube id of the recording, or of the release.
    /// The download queue is keyed by this rather than by row, so a search
    /// landing in the middle of a download cannot move a transfer onto another
    /// item.
    QString id;
    QString title;
    QString artist;
    QString year;
    /// The row's cover, as a URL YouTube serves, or empty when the catalogue
    /// gave none. It is what makes a result recognizable before it is
    /// downloaded: two rows can carry the same title and only one be the
    /// release.
    QString thumbnail;

    /// Where the item is fetched from: the YouTube Music page yt-dlp is handed,
    /// which for a release is its track list and not its page.
    QString url;
    /// A release rather than one recording: what is fetched is several files,
    /// and they land in a folder of their own.
    bool album = false;

    enum class State {
        Idle,     ///< Found, and nobody has asked for it.
        Waiting,  ///< Queued behind a download that is already running.
        Working,  ///< Resolving the item, or in flight.
        Done,     ///< Every file of the item is on disk.
        Failed,   ///< Stopped; `detail` says why.
    };

    State state = State::Idle;
    /// 0 to 1 across the whole item, files already written included.
    double progress = 0.0;
    /// One line about the download: what it is doing, or why it stopped. A
    /// key rather than a sentence, so the row follows the language.
    Message detail;
    /// Audio files in the item, known once it has been resolved.
    int fileCount = 0;

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
};

/// The search results a view lists, each carrying the state of its download.
///
/// Plain data with per-row notification: a search is a few hundred bytes of
/// JSON, and a row's state changes while it is on screen, so the view has to
/// be told about that one row rather than have itself re-created.
class DownloadModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        YearRole,
        /// The cover the row is drawn with, at the size a row needs it.
        ThumbnailRole,
        /// The states a delegate acts on, spelled out as booleans: the view
        /// asks "is this row downloading" and never has to learn how the state
        /// machine names things.
        WorkingRole,
        WaitingRole,
        DoneRole,
        FailedRole,
        ProgressRole,
        /// The line under the title: the key of the string, and the values
        /// its placeholders are filled from.
        DetailKeyRole,
        DetailValuesRole,
    };
    Q_ENUM(Role)

    explicit DownloadModel(QObject *parent = nullptr);

    /// Replaces the rows with `items`, carrying over the state of any row the
    /// list already had -- the same item found again is the same download --
    /// and keeping the rows that are downloading even when the new search did
    /// not find them, because they are the only thing still happening.
    void setItems(QVector<DownloadItem> items);
    void clear();

    [[nodiscard]] int rowForId(const QString &id) const;
    [[nodiscard]] const QVector<DownloadItem> &items() const { return m_items; }

    /// Applies a change to row `row`, which is ignored when the index is stale.
    void setState(int row, DownloadItem::State state, const Message &detail = Message());
    void setProgress(int row, double progress);
    void setFileCount(int row, int fileCount);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    QVector<DownloadItem> m_items;
    /// Rebuilt on every change to `m_items`: a search result list is short, and
    /// the queue looks a row up by id after every file.
    QHash<QString, int> m_rowById;
};
