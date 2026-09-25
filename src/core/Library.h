#pragma once

#include "Track.h"
#include "Translator.h"

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

/// Read-only view over the SQLite index.
///
/// Every method here is meant to be called from the GUI thread only: the
/// connection is opened once and reused. Writing happens in `Scanner`, which
/// opens its own connection on its worker thread, so SQLite never sees two
/// threads sharing a handle.
class Library : public QObject
{
    Q_OBJECT

public:
    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    /// Opens (creating if needed) `dbPath` and applies the schema.
    /// Returns false and fills `error` on failure: as a key when the reason is
    /// one of ours, and as SQLite's own words when it is not.
    bool open(const QString &dbPath, Message *error = nullptr);
    void close();

    /// Every indexed track, in album order:
    /// album artist, album, disc, track number, title.
    [[nodiscard]] QVector<Track> tracks() const;

    [[nodiscard]] int count() const;

    /// Distinct values of a column, for grouping. `column` must be one of the
    /// literals accepted by `sortColumn()`.
    [[nodiscard]] QStringList distinctValues(const QString &column) const;

    [[nodiscard]] QString databasePath() const { return m_path; }

    /// One row of the lyrics cache. `found` is what separates "already asked
    /// about and answered" from "never asked": a track LRCLIB has no lyrics
    /// for must not be looked up again on every play.
    struct CachedLyrics
    {
        bool found = false;
        bool instrumental = false;
        QString source;
        QString text;
    };

    /// The cached lyrics for `path`, if a lookup has been cached for it.
    /// Empty `text` with `found` set is a cached miss.
    [[nodiscard]] CachedLyrics cachedLyrics(const QString &path) const;

    /// Caches a lookup result, replacing an earlier one. `source` names where
    /// it came from and which version of that lookup wrote it
    /// (`lrclib/v2`), which is what lets a better lookup retire the answers of
    /// a blinder one; `text` is the lyrics as they arrived, so a better parser
    /// re-reads them without another lookup.
    bool saveLyrics(const QString &path, const QString &source, bool instrumental, const QString &text);

    /// Rewrites the indexed paths under `from` as being under `to`, in the
    /// tracks and in the lyrics cache: a folder that was renamed moved its
    /// files without changing them, and a path is what the index is keyed by.
    /// `from` and `to` are the folders' own paths, without a trailing
    /// separator.
    ///
    /// Returns the number of tracks that moved, or -1 when SQLite refused, in
    /// which case nothing was written and the index still names the old
    /// folder.
    int rebasePaths(const QString &from, const QString &to);

    /// The schema, shared with `Scanner` so both connections agree.
    static QString schema();

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_path;
};
