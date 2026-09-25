#include "AppController.h"

#include "CoverArtProvider.h"
#include "Scanner.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace {

/// Set once `shutdown()` has run: `aboutToQuit` and the destructor both call
/// it, and the library must not be closed twice.
const char kShutdownProperty[] = "cadenzaShutdown";

/// The row the view is showing `path` on, or -1. The player holds no path until
/// something is queued, and an empty path is a row of nothing.
///
/// The library keeps a row per path in a hash, so this is one lookup and one
/// map rather than a walk of everything the view is showing -- which, in a
/// library of fifty thousand tracks, is what every keystroke would otherwise
/// pay for while something is playing.
int rowFor(const TrackModel &model, const TrackFilterProxy &proxy, const QString &path)
{
    if (path.isEmpty())
        return -1;

    const int sourceRow = model.rowForPath(path);
    if (sourceRow < 0)
        return -1;

    const QModelIndex mapped = proxy.mapFromSource(model.index(sourceRow, 0));
    return mapped.isValid() ? mapped.row() : -1;
}

/// True when `path` lies inside `root`. The separator is what makes this
/// correct: a bare prefix test would accept `/Music/Rockabilly` as living under
/// `/Music/Rock`.
bool isUnder(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty())
        return false;

    return path == root || path.startsWith(root + QLatin1Char('/'));
}

/// Rewrites the paths in `list` that lie under `from` as being under `to`, and
/// answers the last index that moved, or -1 when nothing did. That index is
/// what says whether the move reaches the part of a queue still to be played.
int rebasePathsIn(QStringList *list, const QString &from, const QString &to)
{
    const QString oldPrefix = from + QLatin1Char('/');
    const QString newPrefix = to + QLatin1Char('/');

    int last = -1;
    for (int index = 0; index < list->size(); ++index) {
        QString &path = (*list)[index];
        if (!path.startsWith(oldPrefix))
            continue;

        path = newPrefix + path.mid(oldPrefix.size());
        last = index;
    }
    return last;
}

/// The shuffle mode an unknown word means: `off`, `list` or `library`, and off
/// for anything else, the same way an unknown ReplayGain mode means none.
QString shuffleMode(const QString &mode)
{
    return mode == QLatin1String("list") || mode == QLatin1String("library")
               ? mode
               : QStringLiteral("off");
}

/// `pool` in the order it is played when it is shuffled: `first` -- the track
/// that was asked for, which is what plays -- at the head, and the rest of the
/// pool dealt at random behind it. Every order is equally likely, and no track
/// is played twice before the pool runs out.
QStringList shuffledFrom(const QStringList &pool, const QString &first)
{
    QStringList rest = pool;
    rest.removeAll(first);

    // Fisher-Yates.
    for (int index = rest.size() - 1; index > 0; --index)
        rest.swapItemsAt(index, QRandomGenerator::global()->bounded(index + 1));

    QStringList order;
    order.reserve(pool.size());
    order.append(first);
    order += rest;
    return order;
}

/// What the library list can be a list of, in the order the interface offers
/// it, paired with the field a row is grouped by and searched in. Each name is
/// also the tail of its strings: `library_browse_` + the name.
struct BrowseMode
{
    const char *name;
    TrackModel::Field field;
};

constexpr BrowseMode kBrowseModes[] = {
    {"tracks", TrackModel::AnyField},
    {"artists", TrackModel::ArtistField},
    {"albums", TrackModel::AlbumField},
    {"genres", TrackModel::GenreField},
};

/// What `name` asks to browse, falling back to the flat list: a mode the
/// interface does not know shows every track rather than nothing at all.
TrackModel::Field browseFieldFor(const QString &name)
{
    for (const BrowseMode &mode : kBrowseModes) {
        if (name == QLatin1String(mode.name))
            return mode.field;
    }

    return TrackModel::AnyField;
}

/// What browsing `field` is called, which is what the interface holds.
QString browseNameFor(TrackModel::Field field)
{
    for (const BrowseMode &mode : kBrowseModes) {
        if (mode.field == field)
            return QString::fromLatin1(mode.name);
    }

    return QString::fromLatin1(kBrowseModes[0].name);
}

/// Why `name` cannot be a playlist's name -- as the key of the string that says
/// so, or an empty string when it can. A playlist is the folder it lives in, so
/// what a folder cannot be called is what is refused: nothing at all, the two
/// names that mean a directory rather than name one, and anything carrying a
/// separator or a control character.
QString playlistNameProblem(const QString &name)
{
    if (name.isEmpty())
        return QStringLiteral("playlist_name_empty");

    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return QStringLiteral("playlist_name_invalid");

    for (const QChar character : name) {
        if (character == QLatin1Char('/') || character == QLatin1Char('\\')
            || character.category() == QChar::Other_Control)
            return QStringLiteral("playlist_name_invalid");
    }

    return QString();
}

}  // namespace

AppController::AppController(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_lyrics(&m_library)
{
    Message error;
    if (!m_library.open(databasePath, &error))
    {
        // Nothing below needs a live library: the model stays empty, `scan()`
        // becomes a no-op and the interface disables the transport off the
        // error property.
        setLibraryError(error.isEmpty() ? Message{.key = QStringLiteral("error_library_open")}
                                        : error);
    }

    m_proxy.setSourceModel(&m_model);

    // The library starts as the flat list of tracks: what the list is a list of
    // belongs to looking at it, so it does not survive a launch the way the
    // playlist does. Taken from the proxy so the two cannot disagree.
    m_browse = browseNameFor(m_proxy.field());

    // The remembered root and playlist, so a launch lands where the last one
    // left off. A root that has since been deleted or unmounted falls back to
    // the platform default instead of scanning nothing -- and so does the
    // empty string a first run has remembered, because `QDir("")` is the
    // current directory, and that exists.
    const QSettings settings;
    const QString savedRoot = settings.value(QStringLiteral("library/root")).toString();
    m_rootFolder = !savedRoot.isEmpty() && QDir(savedRoot).exists() ? savedRoot : defaultRoot();

    const QString savedFolder = settings.value(QStringLiteral("library/folder")).toString();
    if (isUnder(savedFolder, m_rootFolder))
    {
        m_selectedFolder = savedFolder;
        m_proxy.setFolder(savedFolder);
    }

    m_lyricsVisible = settings.value(QStringLiteral("lyrics/visible"), false).toBool();
    m_lyrics.setOnlineEnabled(settings.value(QStringLiteral("lyrics/online"), false).toBool());

    // How the queue is played is remembered the way the lyrics pane is: it is
    // how the listener wants to listen rather than what they are listening to.
    m_shuffle = shuffleMode(settings.value(QStringLiteral("player/shuffle"),
                                           QStringLiteral("off")).toString());

    connect(&m_watcher, &QFutureWatcher<int>::finished, this, &AppController::onScanFinished);
    connect(&m_translator, &Translator::languageChanged, this, &AppController::retranslate);
    connect(&m_player, &Player::pathChanged, this, &AppController::onPlayerPathChanged);
    connect(&m_player, &Player::positionChanged, this, &AppController::onPlayerPositionChanged);
    connect(&m_lyrics, &LyricsProvider::resolved, this, &AppController::onLyricsResolved);
    connect(&m_lyrics, &LyricsProvider::loadingChanged, this, &AppController::onLyricsLoading);

    connect(&m_downloader, &Downloader::searchingChanged, this, &AppController::setSearchingOnline);
    connect(&m_downloader, &Downloader::message, this, &AppController::setOnlineMessage);
    connect(&m_downloader, &Downloader::progress, this, &AppController::setStatus);

    // A file that has arrived is a file the index has never seen. The walk is
    // incremental, so pointing it at the folder again is the whole of what has
    // to happen for the track to appear -- unless a walk is already running,
    // which may have passed that folder already. Then it is remembered, and
    // the next walk is the one that sees it.
    connect(&m_downloader, &Downloader::downloaded, this, [this](const QString &) {
        if (m_scanning)
            m_rescanPending = true;
        else
            rescan();
    });

    refreshDownloadFolder();
}

AppController::~AppController()
{
    shutdown();
}

QString AppController::defaultDatabasePath()
{
    // `AppDataLocation` is resolved through the application and organisation
    // names set in `main()`, which is what puts the index with the platform's
    // other application data: `$XDG_DATA_HOME` on Linux, `%LOCALAPPDATA%` on
    // Windows and `~/Library/Application Support` on macOS.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(directory).filePath(QStringLiteral("library.db"));
}

QString AppController::defaultRoot()
{
    const QDir home(QDir::homePath());
    const QStringList candidates{
        QStringLiteral("Music"),
        QStringLiteral("M\u00fasica"),
    };

    for (const QString &candidate : candidates)
    {
        const QString path = home.filePath(candidate);
        if (QDir(path).exists())
            return path;
    }

    return QDir::homePath();
}

void AppController::scan(const QString &folder)
{
    if (m_scanning || !m_libraryError.isEmpty())
        return;

    const QString root = QDir(folder).absolutePath();
    setRootFolder(root);
    QSettings().setValue(QStringLiteral("library/root"), root);
    setScanning(true);
    setStatus(Message{.key = QStringLiteral("status_scanning"),
                      .values = {{QStringLiteral("path"), root}}});

    // The worker only gets a pointer to the scanner, which stays owned here and
    // outlives the task; `onScanFinished` deletes it once the future is done.
    m_scanner = new Scanner(m_library.databasePath());
    connect(m_scanner, &Scanner::progress, this, &AppController::onScanProgress);
    connect(m_scanner, &Scanner::finished, this, &AppController::onScanFinished);

    const QStringList roots{root};
    Scanner *scanner = m_scanner;
    m_watcher.setFuture(QtConcurrent::run([scanner, roots] { return scanner->run(roots); }));
}

void AppController::rescan()
{
    scan(m_rootFolder);
}

void AppController::openRootFolder() const
{
    if (!m_rootFolder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_rootFolder));
}

void AppController::startInitialScan()
{
    // `m_rootFolder` already holds the remembered root, or the platform default
    // when nothing was remembered or the remembered folder is gone.
    rescan();
}

void AppController::selectFolder(const QString &path)
{
    if (m_selectedFolder == path)
        return;

    m_selectedFolder = path;
    m_proxy.setFolder(path);

    // Switching playlists is a change of context: a query left over from the
    // previous one would only make the new playlist look empty.
    if (!m_proxy.query().isEmpty())
    {
        m_proxy.setQuery(QString());
        emit filterChanged();
    }

    QSettings settings;
    settings.setValue(QStringLiteral("library/folder"), path);

    // Downloads land in the folder that is selected: picking a playlist is
    // also saying where the next thing you fetch should go.
    refreshDownloadFolder();

    emit selectedFolderChanged();

    // A group belongs to the playlist it was opened in as much as to the browse
    // mode: an artist is not what the new playlist is a list of.
    if (dropGroup())
        emit groupChanged();

    refiltered();
}

QString AppController::selectedFolderName() const
{
    return folderName(m_selectedFolder);
}

QString AppController::folderName(const QString &folder) const
{
    if (folder.isEmpty())
        return m_translator.text(QStringLiteral("library_all_music"));

    return QDir(folder).dirName();
}

QString AppController::playbackSource() const
{
    // Nothing has played yet, so there is no list to remember: what a click
    // would start is the list on screen, and the sidebar's own selection is
    // what it is called.
    const bool started = !m_sourceList.isEmpty();
    const bool searched = started ? m_sourceSearched : !m_proxy.query().isEmpty();
    if (searched)
        return m_translator.text(QStringLiteral("transport_shuffle_results"));

    // A group is a smaller list than a playlist and a truer name for one: what
    // is playing is that artist, not everything in the folder.
    const QString group = started ? m_sourceGroup : m_groupName;
    if (!group.isEmpty())
        return group;

    return folderName(started ? m_sourceFolder : m_selectedFolder);
}

void AppController::playRow(int row)
{
    // The visible list is the queue, so playing from a search result plays the
    // search results.
    const QStringList list = m_proxy.paths();
    if (row < 0 || row >= list.size())
        return;

    // The list this playback starts from is remembered here, because it is
    // what the shuffle modes draw on and the order the queue goes back to: the
    // sidebar is free to point somewhere else while this plays.
    m_sourceList = list;
    m_sourceFolder = m_selectedFolder;
    m_sourceGroup = m_groupName;
    m_sourceSearched = !m_proxy.query().isEmpty();
    emit playbackSourceChanged();

    // Shuffling the library draws on every indexed track instead, so the list
    // the row was clicked in is only the first of the pool.
    const QStringList pool = shufflingLibrary() ? libraryPaths() : list;
    m_queue = shuffling() ? shuffledFrom(pool, list.at(row)) : pool;

    // Kept because a rename has to know what the queue is holding: mpv owns the
    // playlist, but not the knowledge of which of its paths a folder moved out
    // from under. The row that was clicked leads a shuffled queue, so that is
    // where it starts; otherwise it is the row itself.
    m_player.load(m_queue, shuffling() ? 0 : row);
}

void AppController::setShuffle(const QString &mode)
{
    const QString wanted = shuffleMode(mode);
    if (wanted == m_shuffle)
        return;

    m_shuffle = wanted;
    QSettings().setValue(QStringLiteral("player/shuffle"), m_shuffle);
    emit shuffleChanged();

    // What comes next changes now rather than at the next thing asked for: the
    // mode is a thing about the queue that is playing.
    reorderQueue();
}

void AppController::cycleShuffle()
{
    setShuffle(m_shuffle == QLatin1String("off")    ? QStringLiteral("list")
               : m_shuffle == QLatin1String("list") ? QStringLiteral("library")
                                                    : QStringLiteral("off"));
}

void AppController::reorderQueue()
{
    // Nothing has played yet: there is no queue to deal again, and the order of
    // the next one is decided by `playRow`.
    if (m_sourceList.isEmpty())
        return;

    const QStringList pool = shufflingLibrary() ? libraryPaths() : m_sourceList;
    if (m_player.reorderTail(pool, shuffling()))
        m_queue = m_player.queue();
}

QStringList AppController::libraryPaths() const
{
    const QVector<Track> &tracks = m_model.tracks();

    QStringList paths;
    paths.reserve(tracks.size());
    for (const Track &track : tracks)
        paths.append(track.path);
    return paths;
}

QString AppController::renamePlaylist(const QString &path, const QString &name)
{
    const QString wanted = name.trimmed();
    if (const QString problem = playlistNameProblem(wanted); !problem.isEmpty())
        return problem;

    // Only a folder directly under the music folder is a playlist, and "All
    // music" -- the empty path -- is not a folder at all.
    if (m_rootFolder.isEmpty() || QFileInfo(path).absolutePath() != m_rootFolder)
        return QStringLiteral("playlist_rename_failed");

    const QString target = m_rootFolder + QLatin1Char('/') + wanted;
    if (target == path)
        return QString();

    if (!QFileInfo(path).isDir())
        return QStringLiteral("playlist_rename_failed");

    // Two folders cannot share a name, so neither can two playlists. A name
    // that differs only in case is not a clash: where the filesystem folds
    // case the destination is the folder being renamed, and changing the case
    // of a name is as much a rename as changing the word.
    if (QString::compare(target, path, Qt::CaseInsensitive) != 0 && QFileInfo::exists(target))
        return QStringLiteral("playlist_name_taken");

    if (!QDir().rename(path, target))
        return QStringLiteral("playlist_rename_failed");

    // The index follows the folder: a path is what it is keyed by, and the
    // recordings have not changed, only where they are. It is left alone under
    // a walk that is running, which read the paths as they were before the
    // rename and would prune the rows that moved as files it never saw; the
    // walk owed when it ends is what settles them instead. A rewrite SQLite
    // refuses -- a path under the new name from a folder deleted since, most
    // likely -- is answered the same way, because the walk prunes what is stale
    // and indexes what is there.
    bool indexed = false;
    if (m_scanning)
    {
        m_rescanPending = true;
    }
    else
    {
        indexed = m_library.rebasePaths(path, target) >= 0;
        if (!indexed)
            rescan();
    }

    // Everything that answers to the new name is told on the next turn of the
    // event loop rather than inside this call, and that is not politeness: the
    // call comes from the row of the playlist being renamed, and rebuilding the
    // rail -- which is what the folders list does -- destroys that row, field
    // and all. A row cannot be taken out from under the code still inside it.
    const QString source = path;
    QTimer::singleShot(0, this, [this, source, target, indexed] {
        // A download lands where the sidebar points, so a selected playlist
        // that moved is still the selected one under its new name.
        if (m_selectedFolder == source)
            selectFolder(target);

        // Only worth doing where the rows were rewritten: under a walk the
        // sidebar is drawn from the old rows, and it settles when the walk
        // does.
        if (indexed)
            reloadTracks();

        // The list the shuffle modes draw on moved with it as well, whether or
        // not the queue still names it: a mode switched to later must not deal
        // paths that are no longer there.
        rebasePathsIn(&m_sourceList, source, target);

        // Playback was handed whole paths, so a queue built from the renamed
        // folder names files that are no longer there. It is rebuilt onto the
        // paths they have now -- but only when something still to be played
        // moved, because rebuilding starts the file that is playing again, and
        // doing that to a queue a rename never touched is worse than leaving
        // it. Where that file did not move, it is put back where it was, to the
        // second, by the mpv option that starts a file partway in.
        const int moved = rebasePathsIn(&m_queue, source, target);
        const int current = m_player.index();
        if (moved >= 0 && current >= 0 && moved >= current)
        {
            const bool paused = m_player.paused();
            m_player.load(m_queue, current, m_player.position());
            if (paused)
                m_player.pause();
        }
    });

    return QString();
}

void AppController::setFilter(const QString &filter)
{
    m_proxy.setQuery(filter);
    emit filterChanged();
    refiltered();
}

void AppController::setBrowse(const QString &browse)
{
    // Canonical, so a name the interface does not know settles on the flat list
    // rather than on a spelling it would then display back.
    const QString wanted = browseNameFor(browseFieldFor(browse));
    if (m_browse == wanted)
        return;

    m_browse = wanted;
    m_proxy.setField(browseFieldFor(wanted));

    // A group belongs to the mode it was opened in: an artist is not something
    // to stay inside while the list becomes a list of albums.
    const bool dropped = dropGroup();

    emit browseChanged();
    if (dropped)
        emit groupChanged();
    refiltered();
}

QVariantList AppController::browseModes() const
{
    QVariantList names;
    names.reserve(std::size(kBrowseModes));
    for (const BrowseMode &mode : kBrowseModes)
        names.append(QString::fromLatin1(mode.name));
    return names;
}

void AppController::openGroup(const QString &key, const QString &name)
{
    // Nothing to open in the flat list: its rows are tracks, and a track is not
    // a group of anything.
    const TrackModel::Field field = browseFieldFor(m_browse);
    if (field == TrackModel::AnyField)
        return;

    if (m_proxy.grouped() && m_groupKey == key)
        return;

    m_groupKey = key;
    m_groupName = name;
    m_proxy.setGroup(field, key);

    // Inside a group the field is already settled by the group itself, so the
    // search is free again: what it narrows is the tracks in there, by any of
    // their fields.
    m_proxy.setField(TrackModel::AnyField);

    emit groupChanged();
    refiltered();
}

void AppController::closeGroup()
{
    if (!dropGroup())
        return;

    emit groupChanged();
    refiltered();
}

bool AppController::dropGroup()
{
    if (!m_proxy.grouped())
        return false;

    m_proxy.clearGroup();
    m_groupKey.clear();
    m_groupName.clear();

    // Back out to the list of groups, where the search narrows the names of
    // what is listed again.
    m_proxy.setField(browseFieldFor(m_browse));
    return true;
}

void AppController::rebuildGroups()
{
    // In the flat list there is nothing to group, and inside a group the list
    // is tracks rather than groups: either way the model is emptied once and
    // then left alone, so a keystroke inside a group costs no pass over the
    // library at all.
    if (browseFieldFor(m_browse) == TrackModel::AnyField || m_proxy.grouped()) {
        if (m_groups.rowCount() > 0) {
            m_groups.clear();
            emit groupsChanged();
        }
        return;
    }

    m_groups.rebuild(m_model, m_proxy, browseFieldFor(m_browse));
    emit groupsChanged();
}

void AppController::refiltered()
{
    rebuildGroups();

    emit tracksChanged();
    setCurrentRow(rowFor(m_model, m_proxy, m_player.path()));
}

void AppController::clearFilter()
{
    setFilter(QString());
}

QString AppController::coverUrl(const QString &path, int width, int height, int blur, int round) const
{
    return CoverArtProvider::urlFor(path, QSize(width, height), blur, round);
}

QString AppController::formatTime(double seconds) const
{
    if (!(seconds > 0.0))
        return QStringLiteral("0:00");

    // mpv reports out-of-range clocks before a file has loaded, and the
    // truncation below is undefined for those.
    const double bounded = std::min(seconds, double(std::numeric_limits<int>::max()));
    const int total = static_cast<int>(bounded);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int remainder = total % 60;

    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remainder, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2").arg(minutes).arg(remainder, 2, 10, QLatin1Char('0'));
}

void AppController::shutdown()
{
    // Idempotent: `aboutToQuit` and the destructor both land here, and the
    // library must not be closed twice. The flag is a dynamic property.
    if (property(kShutdownProperty).toBool())
        return;

    setProperty(kShutdownProperty, true);

    // The scanner exists for exactly as long as the walk does, which makes it
    // the honest test for work still in flight.
    if (m_scanner)
    {
        m_scanner->cancel();
        m_watcher.waitForFinished();
    }

    delete m_scanner;
    m_scanner = nullptr;

    // Closes the socket rather than waiting for the reply: a quit is not a
    // reason to keep the process alive.
    m_lyrics.cancel();
    m_downloader.cancelAll();

    m_player.stop();
    m_library.close();
}

void AppController::onScanFinished()
{
    // The scanner's own signal and the watcher both reach this slot; whichever
    // arrives first owns the completion, the other one finds `m_scanner`
    // cleared.
    if (!m_scanner || !m_watcher.isFinished())
        return;

    const int indexed = m_watcher.result();

    // A walk that re-read nothing and lost nothing leaves the library exactly
    // as it was, and reading fifty thousand rows back, folding them and
    // resetting the view is work a rescan of an unchanged folder -- which is
    // every rescan that follows a download landing in an album already indexed
    // -- has no reason to cost. The first walk of a session still loads, because
    // the model is empty until it does.
    if (indexed > 0 || m_scanner->removed() > 0 || m_model.rowCount() == 0)
        reloadTracks();

    if (m_model.rowCount() > 0)
    {
        setStatus(Message{.key = QStringLiteral("status_indexed"),
                          .values = {{QStringLiteral("count"), m_model.rowCount()},
                                     {QStringLiteral("indexed"), indexed}},
                          .count = m_model.rowCount()});
    }
    else
    {
        setStatus(Message{.key = QStringLiteral("status_no_audio"),
                          .values = {{QStringLiteral("path"), m_rootFolder}}});
    }

    delete m_scanner;
    m_scanner = nullptr;
    setScanning(false);

    // A download that landed while this walk was running is the one thing the
    // walk could have missed.
    if (m_rescanPending)
    {
        m_rescanPending = false;
        rescan();
    }
}

void AppController::onScanProgress(int indexed, const QString &title)
{
    // The track being read is part of the line rather than appended to it: a
    // language is free to put it wherever it reads best.
    Message message{.key = QStringLiteral("status_indexing"),
                    .values = {{QStringLiteral("count"), indexed}},
                    .count = indexed};

    if (!title.isEmpty())
    {
        message.key = QStringLiteral("status_indexing_named");
        message.values.insert(QStringLiteral("title"), title);
    }

    setStatus(message);
}

void AppController::onPlayerPathChanged()
{
    setCurrentRow(rowFor(m_model, m_proxy, m_player.path()));

    // The lines belong to the track that just stopped; leaving them up under
    // the new title is worse than an empty pane for a moment.
    m_lyricsModel.clear();
    requestLyrics();
}

void AppController::onPlayerPositionChanged()
{
    // One binary search over a few dozen lines, four times a second, and no
    // signal at all unless the line changed.
    m_lyricsModel.setPosition(m_player.position());
}

void AppController::onLyricsResolved(const QString &path, const Lyrics &lyrics)
{
    // A lookup that started before the user moved on. Its result is about a
    // track that is no longer playing.
    if (path != m_player.path())
        return;

    m_lyricsModel.setLoading(false);
    m_lyricsModel.setLyrics(lyrics);
}

void AppController::onLyricsLoading(const QString &path, bool loading)
{
    if (path != m_player.path())
        return;

    m_lyricsModel.setLoading(loading);
}

void AppController::requestLyrics(bool force)
{
    const int row = m_model.rowForPath(m_player.path());
    if (row < 0)
    {
        m_lyrics.cancel();
        return;
    }

    // Set here rather than on the provider's signal: the local read is fast
    // enough that the pane would otherwise flick through "nothing found"
    // before the lines arrive.
    m_lyricsModel.setLoading(true);
    m_lyrics.request(m_model.tracks().at(row), force);
}

void AppController::playLyricLine(int row)
{
    const double time = m_lyricsModel.timeAt(row);
    if (time < 0.0)
        return;

    m_player.seek(time);
}

void AppController::retryLyrics()
{
    requestLyrics(true);
}

void AppController::setLyricsVisible(bool visible)
{
    if (m_lyricsVisible == visible)
        return;

    m_lyricsVisible = visible;
    QSettings().setValue(QStringLiteral("lyrics/visible"), visible);
    emit lyricsVisibleChanged();
}

void AppController::setOnlineLyrics(bool enabled)
{
    if (m_lyrics.onlineEnabled() == enabled)
        return;

    m_lyrics.setOnlineEnabled(enabled);
    QSettings().setValue(QStringLiteral("lyrics/online"), enabled);
    emit onlineLyricsChanged();

    // Turning it on is a request for the track that is playing: nothing was
    // looked up while it was off, so there is an answer waiting.
    if (enabled)
        requestLyrics();
}

QString AppController::downloadFolder() const
{
    // The sidebar's selection is a playlist inside the music folder, and "All
    // music" is the absence of one: a download follows the selection, and
    // lands in the music folder itself when nothing is selected.
    return m_selectedFolder.isEmpty() ? m_rootFolder : m_selectedFolder;
}

QString AppController::downloadFolderName() const
{
    const QString folder = downloadFolder();
    return folder.isEmpty() ? QString() : QDir(folder).dirName();
}

void AppController::searchOnline(const QString &query)
{
    m_downloader.search(query);
}

void AppController::downloadResult(int row)
{
    m_downloader.download(row);
}

void AppController::cancelDownload(int row)
{
    m_downloader.cancel(row);
}

void AppController::setSearchingOnline(bool searching)
{
    if (m_searchingOnline == searching)
        return;

    m_searchingOnline = searching;
    emit searchingOnlineChanged();
}

void AppController::setOnlineMessage(const Message &message)
{
    if (m_onlineMessage == message)
        return;

    m_onlineMessage = message;
    emit onlineMessageChanged();
}

void AppController::refreshDownloadFolder()
{
    const QString folder = downloadFolder();
    m_downloader.setTargetFolder(folder);

    if (m_downloadFolder == folder)
        return;

    m_downloadFolder = folder;
    emit downloadFolderChanged();
}

void AppController::reloadTracks()
{
    m_model.setTracks(m_library.tracks());

    // Before the change is announced: rebuilding can clear a selection the
    // rescan proved stale, and the view should see one settled model rather
    // than a list that filters down to nothing and then recovers.
    rebuildFolders();

    refiltered();
}

void AppController::rebuildFolders()
{
    QVariantList folders;
    folders.append(QVariantMap{
        {QStringLiteral("name"), m_translator.text(QStringLiteral("library_all_music"))},
        {QStringLiteral("path"), QString()},
        {QStringLiteral("count"), m_model.rowCount()},
    });

    const QString prefix = m_rootFolder + QLatin1Char('/');
    QHash<QString, int> counts;

    if (!m_rootFolder.isEmpty())
    {
        for (const Track &track : m_model.tracks())
        {
            if (!track.path.startsWith(prefix))
                continue;

            // A playlist is the first path component under the root, so a track
            // in `Rock/Album/song.flac` counts towards `Rock` and picking that
            // entry plays the whole subtree. Files sitting loose in the root
            // belong to no playlist and appear only under "All music".
            const QString relative = track.path.mid(prefix.size());
            const int separator = relative.indexOf(QLatin1Char('/'));
            if (separator <= 0)
                continue;

            ++counts[relative.left(separator)];
        }
    }

    QStringList names = counts.keys();
    std::sort(names.begin(), names.end(), [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });

    QStringList paths;
    paths.reserve(names.size());
    for (const QString &name : std::as_const(names))
    {
        const QString path = prefix + name;
        paths.append(path);
        folders.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("path"), path},
            {QStringLiteral("count"), counts.value(name)},
        });
    }

    m_folders = folders;
    emit foldersChanged();

    // A selection that survived by accident -- the folder was deleted, or the
    // root moved -- would leave the library filtered down to nothing.
    if (!m_selectedFolder.isEmpty() && !paths.contains(m_selectedFolder))
        selectFolder(QString());
}

void AppController::setStatus(const Message &status)
{
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void AppController::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;

    m_scanning = scanning;
    emit scanningChanged();
}

void AppController::setRootFolder(const QString &folder)
{
    if (m_rootFolder == folder)
        return;

    m_rootFolder = folder;
    emit rootFolderChanged();
    refreshDownloadFolder();
}

void AppController::setCurrentRow(int row)
{
    if (m_currentRow == row)
        return;

    m_currentRow = row;
    emit currentRowChanged();
}

void AppController::setLibraryError(const Message &error)
{
    if (m_libraryError == error)
        return;

    m_libraryError = error;
    emit libraryErrorChanged();
}

void AppController::retranslate()
{
    // Everything the interface is holding from here travels as a key, so the
    // strings already on screen only have to be announced again. The folders
    // are the one exception: they carry a name that came off the disk.
    if (!m_folders.isEmpty())
    {
        QVariantMap all = m_folders.first().toMap();
        all.insert(QStringLiteral("name"), m_translator.text(QStringLiteral("library_all_music")));
        m_folders[0] = all;
        emit foldersChanged();
    }

    // The rows are the other exception: the name for a file no tag names, and
    // the words a group is described with, are the models' own.
    m_model.retranslate();
    m_groups.retranslate();

    emit statusChanged();
    emit onlineMessageChanged();
    emit libraryErrorChanged();
    emit selectedFolderChanged();
    emit playbackSourceChanged();
}
