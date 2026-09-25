#include "Library.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <atomic>

namespace {

/// Connection names are process-wide, so the library takes one from a counter
/// rather than reusing a fixed string that a second instance would collide with.
QString nextConnectionName()
{
    static std::atomic<int> counter{0};
    return QStringLiteral("cadenza-library-%1").arg(counter.fetch_add(1));
}

}  // namespace

Library::Library(QObject *parent)
    : QObject(parent)
{
}

Library::~Library()
{
    close();
}

QString Library::schema()
{
    // The Python implementation's SCHEMA, verbatim in effect: both versions can
    // then share one database file.
    static const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks (\n"
        "    path        TEXT PRIMARY KEY,\n"
        "    mtime       REAL    NOT NULL,\n"
        "    size        INTEGER NOT NULL,\n"
        "    title       TEXT    NOT NULL DEFAULT '',\n"
        "    artist      TEXT    NOT NULL DEFAULT '',\n"
        "    album       TEXT    NOT NULL DEFAULT '',\n"
        "    albumartist TEXT    NOT NULL DEFAULT '',\n"
        "    genre       TEXT    NOT NULL DEFAULT '',\n"
        "    year        TEXT    NOT NULL DEFAULT '',\n"
        "    track_no    INTEGER NOT NULL DEFAULT 0,\n"
        "    disc_no     INTEGER NOT NULL DEFAULT 0,\n"
        "    duration    REAL    NOT NULL DEFAULT 0\n"
        ");\n"
        "CREATE INDEX IF NOT EXISTS tracks_album_order\n"
        "    ON tracks (albumartist, album, disc_no, track_no);\n"
        // Lyrics found online are kept here and nowhere else: the file itself
        // is the source of truth for tags and sidecars, and re-reading those
        // costs a stat plus a header, while a lookup costs a round trip.
        "CREATE TABLE IF NOT EXISTS lyrics (\n"
        "    path         TEXT PRIMARY KEY,\n"
        "    source       TEXT    NOT NULL DEFAULT '',\n"
        "    instrumental INTEGER NOT NULL DEFAULT 0,\n"
        "    text         TEXT    NOT NULL DEFAULT ''\n"
        ");\n");
    return sql;
}

bool Library::open(const QString &dbPath, Message *error)
{
    if (m_db.isValid())
        close();

    const QString directory = QFileInfo(dbPath).absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        if (error != nullptr)
            *error = Message{.key = QStringLiteral("error_library_folder"),
                             .values = {{QStringLiteral("path"), directory}}};
        return false;
    }

    m_connectionName = nextConnectionName();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        const QString message = m_db.lastError().text();
        close();
        if (error != nullptr)
            *error = Message{.text = message};
        return false;
    }

    // A QSqlQuery runs one command, so the schema is split on its terminators.
    const QStringList statements = schema().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &statement : statements) {
        const QString sql = statement.trimmed();
        if (sql.isEmpty())
            continue;

        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            const QString message = query.lastError().text();
            close();
            if (error != nullptr)
                *error = Message{.text = message};
            return false;
        }
    }

    m_path = dbPath;
    if (error != nullptr)
        *error = Message{};
    return true;
}

void Library::close()
{
    if (m_db.isOpen())
        m_db.close();

    // Our own handle goes first: removeDatabase() warns that the connection is
    // still in use while any QSqlDatabase object refers to it.
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

QVector<Track> Library::tracks() const
{
    QVector<Track> result;
    if (!m_db.isOpen())
        return result;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT path, mtime, size, title, artist, album, albumartist, "
                                   "genre, year, track_no, disc_no, duration FROM tracks "
                                   "ORDER BY albumartist, album, disc_no, track_no, title")))
        return result;

    while (query.next()) {
        Track track;
        track.path = query.value(0).toString();
        track.title = query.value(3).toString();
        track.artist = query.value(4).toString();
        track.album = query.value(5).toString();
        track.albumArtist = query.value(6).toString();
        track.genre = query.value(7).toString();
        track.year = query.value(8).toString();
        track.trackNo = query.value(9).toInt();
        track.discNo = query.value(10).toInt();
        track.duration = query.value(11).toDouble();
        result.append(track);
    }
    return result;
}

int Library::count() const
{
    if (!m_db.isOpen())
        return 0;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM tracks")) || !query.next())
        return 0;
    return query.value(0).toInt();
}

Library::CachedLyrics Library::cachedLyrics(const QString &path) const
{
    CachedLyrics cached;
    if (!m_db.isOpen() || path.isEmpty())
        return cached;

    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(
            "SELECT source, instrumental, text FROM lyrics WHERE path = ?")))
        return cached;

    query.addBindValue(path);
    if (!query.exec() || !query.next())
        return cached;

    cached.found = true;
    cached.source = query.value(0).toString();
    cached.instrumental = query.value(1).toInt() != 0;
    cached.text = query.value(2).toString();
    return cached;
}

bool Library::saveLyrics(const QString &path, const QString &source, bool instrumental,
                         const QString &text)
{
    if (!m_db.isOpen() || path.isEmpty())
        return false;

    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("INSERT OR REPLACE INTO lyrics (path, source, instrumental, "
                                      "text) VALUES (?, ?, ?, ?)")))
        return false;

    query.addBindValue(path);
    query.addBindValue(source);
    query.addBindValue(instrumental ? 1 : 0);
    query.addBindValue(text);
    return query.exec();
}

int Library::rebasePaths(const QString &from, const QString &to)
{
    if (!m_db.isOpen() || from.isEmpty() || to.isEmpty() || from == to)
        return 0;

    const QString oldPrefix = from + QLatin1Char('/');
    const QString newPrefix = to + QLatin1Char('/');

    // The prefix is matched by character count rather than with LIKE, which
    // would read the `%` and `_` a folder name is free to carry as wildcards.
    // One statement per table, because the two are keyed by the same paths but
    // share nothing else.
    static const QString tracksSql = QStringLiteral(
        "UPDATE tracks SET path = ? || substr(path, ? + 1) WHERE substr(path, 1, ?) = ?");
    static const QString lyricsSql = QStringLiteral(
        "UPDATE lyrics SET path = ? || substr(path, ? + 1) WHERE substr(path, 1, ?) = ?");

    QSqlQuery tracks(m_db);
    QSqlQuery lyrics(m_db);
    if (!tracks.prepare(tracksSql) || !lyrics.prepare(lyricsSql))
        return -1;

    for (QSqlQuery *query : {&tracks, &lyrics}) {
        query->addBindValue(newPrefix);
        query->addBindValue(oldPrefix.size());
        query->addBindValue(oldPrefix.size());
        query->addBindValue(oldPrefix);
    }

    // One transaction: an index that has the lyrics of a track but not the
    // track itself is worse than one that still names the old folder.
    if (!m_db.transaction())
        return -1;

    if (!tracks.exec()) {
        m_db.rollback();
        return -1;
    }

    const int moved = tracks.numRowsAffected();

    if (!lyrics.exec()) {
        m_db.rollback();
        return -1;
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return -1;
    }
    return moved;
}

QStringList Library::distinctValues(const QString &column) const
{
    // The whitelist is the only thing keeping caller-supplied text out of the
    // SQL below, so a name that is not in it never reaches the query.
    static const QStringList allowed = {
        QStringLiteral("albumartist"),
        QStringLiteral("album"),
        QStringLiteral("genre"),
        QStringLiteral("year"),
    };
    if (!allowed.contains(column) || !m_db.isOpen())
        return {};

    QStringList values;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT DISTINCT %1 FROM tracks WHERE %1 <> '' "
                                   "ORDER BY %1 COLLATE NOCASE")
                        .arg(column)))
        return values;

    while (query.next()) {
        const QString value = query.value(0).toString();
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}
