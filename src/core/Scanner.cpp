#include "Scanner.h"

#include "Track.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>

#include <initializer_list>
#include <utility>

namespace {

constexpr int kProgressEvery = 25;
constexpr int kFlushEvery = 500;

/// Owns the scan connection and tears it down on every exit path, the early
/// returns included.
class ScanConnection
{
public:
    explicit ScanConnection(const QString &name)
        : m_name(name)
    {
    }

    ScanConnection(const ScanConnection &) = delete;
    ScanConnection &operator=(const ScanConnection &) = delete;

    ~ScanConnection()
    {
        if (m_db.isOpen())
            m_db.close();

        // The handle is dropped first: removeDatabase() warns that a connection
        // is still in use while a QSqlDatabase object refers to it.
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_name);
    }

    /// Registers the connection and opens `path`. False when SQLite refuses.
    bool open(const QString &path)
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_db.setDatabaseName(path);
        return m_db.open();
    }

    QSqlDatabase &database() { return m_db; }

private:
    QString m_name;
    QSqlDatabase m_db;
};

/// Depth-first walk of `root`, handing every audio file to `visit`. Hidden
/// entries are skipped so the walk matches the Python implementation's
/// `os.walk`, which prunes hidden directories before descending into them.
/// Returns false as soon as `visit` asks to stop.
template <typename Visitor>
bool walkDirectory(const QString &root, const QStringList &extensions, Visitor &visit)
{
    const QDir directory(root);
    const QStringList names = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files);
    for (const QString &name : names) {
        if (name.startsWith(QLatin1Char('.')))
            continue;

        const QFileInfo entry(directory, name);
        if (entry.isDir()) {
            // A symlinked directory is listed but not descended into, matching
            // the Python walk's followlinks=False: a self-referencing link would
            // otherwise recurse forever.
            if (!entry.isSymLink() && !walkDirectory(entry.absoluteFilePath(), extensions, visit))
                return false;
        } else if (extensions.contains(audioExtension(name))) {
            if (!visit(entry.absoluteFilePath()))
                return false;
        }
    }
    return true;
}

/// First non-empty value among `keys`, trimmed. The key lists mirror the alias
/// table the Python version kept per field: TagLib normalises ID3 frame ids and
/// MP4 atoms to one name, but a Vorbis comment is stored under whatever spelling
/// the tagger used, and "ALBUM ARTIST" is as common as "ALBUMARTIST".
QString firstValue(const TagLib::PropertyMap &properties,
                   std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const TagLib::StringList values = properties.value(TagLib::String(key));
        if (values.isEmpty())
            continue;

        const QString value = TStringToQString(values.front()).trimmed();
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

/// `"3/12"` -> 3; anything non-numeric -> 0.
int number(const QString &text)
{
    bool ok = false;
    const int value = text.section(QLatin1Char('/'), 0, 0).trimmed().toInt(&ok);
    return ok && value > 0 ? value : 0;
}

/// Reads one file's tags into `track`. False when TagLib cannot make sense of
/// the file, which the caller treats as a skip.
bool probeTrack(const QString &path, Track *track)
{
    TagLib::FileRef ref(path.toUtf8().constData(), true);
    if (ref.isNull() || ref.tag() == nullptr)
        return false;

    const TagLib::Tag *tag = ref.tag();

    // The property map is TagLib's format-independent view of the tags: it is
    // what resolves ID3 frame ids, Vorbis comments and MP4 atoms to a single
    // name, the job the Python version's alias table did. The generic Tag
    // accessors cover only the few fields every format shares, and have no album
    // artist or disc number at all, so they are the fallback rather than the
    // source.
    const TagLib::PropertyMap properties = tag->properties();

    track->path = path;

    track->title = firstValue(properties, {"TITLE"});
    if (track->title.isEmpty())
        track->title = TStringToQString(tag->title()).trimmed();

    track->artist = firstValue(properties, {"ARTIST"});
    if (track->artist.isEmpty())
        track->artist = TStringToQString(tag->artist()).trimmed();

    track->album = firstValue(properties, {"ALBUM"});
    if (track->album.isEmpty())
        track->album = TStringToQString(tag->album()).trimmed();

    track->albumArtist = firstValue(properties, {"ALBUMARTIST", "ALBUM ARTIST"});

    track->genre = firstValue(properties, {"GENRE"});
    if (track->genre.isEmpty())
        track->genre = TStringToQString(tag->genre()).trimmed();

    track->year = firstValue(properties, {"DATE", "YEAR"});
    if (track->year.isEmpty()) {
        const unsigned int year = tag->year();
        if (year != 0)
            track->year = QString::number(year);
    }

    track->trackNo = number(firstValue(properties, {"TRACKNUMBER", "TRACK"}));
    if (track->trackNo == 0)
        track->trackNo = static_cast<int>(tag->track());

    track->discNo = number(firstValue(properties, {"DISCNUMBER", "DISC"}));

    if (const TagLib::AudioProperties *audio = ref.audioProperties())
        track->duration = audio->lengthInMilliseconds() / 1000.0;

    return true;
}

}  // namespace

Scanner::Scanner(QString databasePath, QObject *parent)
    : QObject(parent)
    , m_databasePath(std::move(databasePath))
{
}

int Scanner::run(const QStringList &roots)
{
    // One connection per instance: the walk happens on a worker thread, and
    // SQLite must never see one handle from two threads.
    ScanConnection connection(QStringLiteral("cadenza-scan-")
                              + QString::number(reinterpret_cast<quintptr>(this), 16));
    if (!connection.open(m_databasePath)) {
        emit finished(0, 0);
        return 0;
    }
    QSqlDatabase &db = connection.database();

    // The (mtime, size) pair of everything already indexed: a file is re-probed
    // only when that pair changed, which is what makes a rescan of an untouched
    // library cost nothing but the directory walk.
    QHash<QString, QPair<double, qint64>> known;
    {
        QSqlQuery query(db);
        if (query.exec(QStringLiteral("SELECT path, mtime, size FROM tracks"))) {
            while (query.next()) {
                known.insert(query.value(0).toString(),
                             qMakePair(query.value(1).toDouble(), query.value(2).toLongLong()));
            }
        }
    }

    int indexed = 0;
    int removed = 0;
    QVector<QVariantList> pending;
    QSet<QString> seen;

    // Commit every 500 rows: one transaction spanning a whole library would stay
    // open for minutes and be rolled back wholesale if anything went wrong.
    auto flush = [&db, &pending]() {
        if (pending.isEmpty())
            return;

        db.transaction();
        QSqlQuery insert(db);
        if (insert.prepare(QStringLiteral("INSERT OR REPLACE INTO tracks VALUES (?,?,?,?,?,?,?,?,?,?,?,?)"))) {
            for (const QVariantList &row : std::as_const(pending)) {
                for (int column = 0; column < row.size(); ++column)
                    insert.bindValue(column, row.at(column));
                insert.exec();
            }
        }
        db.commit();
        pending.clear();
    };

    auto visitFile = [&](const QString &path) -> bool {
        seen.insert(path);

        // A file that vanished between the walk and the stat is skipped, as in
        // the Python version.
        const QFileInfo info(path);
        if (!info.exists())
            return true;

        const double mtime = info.lastModified().toMSecsSinceEpoch() / 1000.0;
        const qint64 size = info.size();

        const auto knownIt = known.constFind(path);
        if (knownIt != known.constEnd() && knownIt->first == mtime && knownIt->second == size)
            return true;

        Track track;
        try {
            // TagLib reads ID3, Vorbis comments, MP4 atoms and the rest, so no
            // container needs a hand-rolled parser -- exactly what mutagen bought
            // the Python version.
            if (!probeTrack(path, &track))
                return true;
        } catch (...) {
            return true;  // unreadable, or an extension that lies
        }

        pending.append(QVariantList{path, mtime, size, track.title, track.artist, track.album,
                                    track.albumArtist, track.genre, track.year, track.trackNo,
                                    track.discNo, track.duration});
        ++indexed;
        if (indexed % kProgressEvery == 0)
            emit progress(indexed, track.displayTitle());
        if (pending.size() >= kFlushEvery)
            flush();

        return !isCancelled();
    };

    const QStringList extensions = audioExtensions();
    bool stopped = false;
    for (const QString &root : roots) {
        if (!walkDirectory(root, extensions, visitFile)) {
            stopped = true;
            break;
        }
    }

    flush();

    if (!stopped) {
        // Rows whose file is gone: everything indexed that the walk never saw.
        // A cancelled walk skips the pruning, because the files it never reached
        // are not missing.
        QVector<QString> stale;
        for (auto it = known.constBegin(); it != known.constEnd(); ++it) {
            if (!seen.contains(it.key()))
                stale.append(it.key());
        }

        if (!stale.isEmpty()) {
            QSqlQuery remove(db);
            if (remove.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"))) {
                db.transaction();
                for (const QString &path : std::as_const(stale)) {
                    remove.bindValue(0, path);
                    remove.exec();
                    ++removed;
                }
                db.commit();
            }
        }
    }

    m_removed = removed;
    emit finished(indexed, removed);
    return indexed;
}

void Scanner::cancel()
{
    m_cancelled.store(true);
}
