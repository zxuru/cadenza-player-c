#pragma once

#include "DownloadModel.h"
#include "Downloader.h"
#include "GroupModel.h"
#include "Library.h"
#include "LyricsModel.h"
#include "LyricsProvider.h"
#include "Player.h"
#include "TrackFilterProxy.h"
#include "TrackModel.h"
#include "Translator.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

class Scanner;

/// Wires the library, the scanner and the player together, and is the single
/// object QML sees. Everything the interface can do goes through here, so the
/// QML files hold no logic beyond presentation.
class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Player *player READ player CONSTANT)
    /// The language the interface is drawn in, and the strings it is drawn
    /// with. Everything the user reads goes through here.
    Q_PROPERTY(Translator *translator READ translator CONSTANT)
    Q_PROPERTY(QAbstractItemModel *tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString rootFolder READ rootFolder NOTIFY rootFolderChanged)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QString selectedFolder READ selectedFolder NOTIFY selectedFolderChanged)
    Q_PROPERTY(QString selectedFolderName READ selectedFolderName NOTIFY selectedFolderChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    /// What the library list is a list of: "tracks", "artists", "albums" or
    /// "genres". Grouping is how a library of any size is walked: one row per
    /// artist with one picture, rather than forty rows with forty thumbnails.
    /// Part of looking at the library rather than a preference, so it is not
    /// remembered across launches.
    Q_PROPERTY(QString browse READ browse WRITE setBrowse NOTIFY browseChanged)
    /// Those names, in the order the interface offers them.
    Q_PROPERTY(QVariantList browseModes READ browseModes CONSTANT)
    /// The groups of the browse mode in force, for the list to bind to while
    /// one is showing. Empty when the browse mode is the flat track list.
    Q_PROPERTY(QAbstractItemModel *groups READ groups NOTIFY groupsChanged)
    /// The group the list is narrowed to, as the interface names it, and
    /// whether there is one. Opening a group is what drills into an artist,
    /// an album or a genre.
    Q_PROPERTY(bool inGroup READ inGroup NOTIFY groupChanged)
    Q_PROPERTY(QString groupName READ groupName NOTIFY groupChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
    Q_PROPERTY(QString libraryError READ libraryError NOTIFY libraryErrorChanged)
    /// The lyrics of the track that is playing, as a model the pane binds to.
    Q_PROPERTY(LyricsModel *lyrics READ lyrics CONSTANT)
    /// Whether the lyrics pane is showing. Remembered across launches.
    Q_PROPERTY(bool lyricsVisible READ lyricsVisible WRITE setLyricsVisible NOTIFY lyricsVisibleChanged)
    /// Whether a track that neither its file nor its folder can answer for may
    /// be looked up on LRCLIB. Off by default: the rest of the player never
    /// opens a socket, and that is the user's to keep or drop.
    Q_PROPERTY(bool onlineLyrics READ onlineLyrics WRITE setOnlineLyrics NOTIFY onlineLyricsChanged)
    /// What an online search found, and how far each result's download got.
    /// The model object never changes; the rows in it do.
    Q_PROPERTY(QAbstractItemModel *downloads READ downloads CONSTANT)
    /// True while a search against the catalogue is in flight.
    Q_PROPERTY(bool searchingOnline READ searchingOnline NOTIFY searchingOnlineChanged)
    /// One line about the last search: how many results, or what went wrong.
    Q_PROPERTY(QString onlineMessage READ onlineMessage NOTIFY onlineMessageChanged)
    /// Folder a download lands in: the folder the sidebar has selected, or the
    /// music folder itself when the selection is "All music".
    Q_PROPERTY(QString downloadFolder READ downloadFolder NOTIFY downloadFolderChanged)
    /// Its last path element, which is what the interface names it by.
    Q_PROPERTY(QString downloadFolderName READ downloadFolderName NOTIFY downloadFolderChanged)
    /// How the queue is played: "off", "list" -- the list playback started
    /// from, dealt again at random -- or "library", which draws on every
    /// indexed track. Remembered across launches.
    Q_PROPERTY(QString shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    /// What the interface calls the list playback is coming from: the playlist
    /// the queue was built in, the whole library, or the search that answered
    /// for it. The list is remembered when playback starts rather than read off
    /// the sidebar again, so browsing somewhere else cannot change what is
    /// playing or what it shuffles within.
    Q_PROPERTY(QString playbackSource READ playbackSource NOTIFY playbackSourceChanged)

public:
    explicit AppController(const QString &databasePath, QObject *parent = nullptr);
    ~AppController() override;

    [[nodiscard]] Player *player() { return &m_player; }
    [[nodiscard]] QAbstractItemModel *tracks() { return &m_proxy; }
    [[nodiscard]] int trackCount() const { return m_proxy.rowCount(); }
    [[nodiscard]] Translator *translator() { return &m_translator; }
    [[nodiscard]] QString status() const { return m_translator.render(m_status); }
    [[nodiscard]] bool scanning() const { return m_scanning; }
    [[nodiscard]] QString rootFolder() const { return m_rootFolder; }
    [[nodiscard]] QVariantList folders() const { return m_folders; }
    [[nodiscard]] QString selectedFolder() const { return m_selectedFolder; }
    [[nodiscard]] QString selectedFolderName() const;
    [[nodiscard]] QString filter() const { return m_proxy.query(); }
    [[nodiscard]] QString browse() const { return m_browse; }
    [[nodiscard]] QVariantList browseModes() const;
    [[nodiscard]] QAbstractItemModel *groups() { return &m_groups; }
    [[nodiscard]] bool inGroup() const { return m_proxy.grouped(); }
    [[nodiscard]] QString groupName() const { return m_groupName; }
    [[nodiscard]] int currentRow() const { return m_currentRow; }
    [[nodiscard]] QString libraryError() const { return m_translator.render(m_libraryError); }
    [[nodiscard]] LyricsModel *lyrics() { return &m_lyricsModel; }
    [[nodiscard]] bool lyricsVisible() const { return m_lyricsVisible; }
    [[nodiscard]] bool onlineLyrics() const { return m_lyrics.onlineEnabled(); }
    [[nodiscard]] QAbstractItemModel *downloads() { return &m_downloadModel; }
    [[nodiscard]] bool searchingOnline() const { return m_searchingOnline; }
    [[nodiscard]] QString onlineMessage() const { return m_translator.render(m_onlineMessage); }
    [[nodiscard]] QString downloadFolder() const;
    [[nodiscard]] QString downloadFolderName() const;
    [[nodiscard]] QString shuffle() const { return m_shuffle; }
    [[nodiscard]] QString playbackSource() const;

    void setLyricsVisible(bool visible);
    void setOnlineLyrics(bool enabled);
    void setShuffle(const QString &mode);

    void setFilter(const QString &filter);

    /// Chooses what the list is a list of: "tracks", "artists", "albums" or
    /// "genres". The query is what narrows it; this is what it is a list of,
    /// so either way the list is read again.
    void setBrowse(const QString &browse);

    /// Where the library lives: `$XDG_DATA_HOME/cadenza` on Linux,
    /// `%LOCALAPPDATA%` on Windows, `~/Library/Application Support` on macOS.
    [[nodiscard]] static QString defaultDatabasePath();

    /// First of `~/Music`, `~/Música` that exists, else the home directory.
    [[nodiscard]] static QString defaultRoot();

    /// Kicks off a scan of `folder` on a worker thread. Replaces the current
    /// root, and is a no-op if a scan is already running.
    Q_INVOKABLE void scan(const QString &folder);

    /// Re-scans the current root.
    Q_INVOKABLE void rescan();

    /// Shows the music folder -- the one every indexed file lives under -- in
    /// the desktop's own file manager, which is where anything the player does
    /// not do to a file is done. Nothing happens when there is no folder yet.
    Q_INVOKABLE void openRootFolder() const;

    /// Indexes the remembered root, or the platform default on a first run.
    /// Separate from `scan()` because only `main()` knows whether a folder was
    /// passed on the command line.
    Q_INVOKABLE void startInitialScan();

    /// Restricts the library to one playlist folder, and remembers the choice.
    /// An empty path lifts the restriction, which is what "All music" passes.
    Q_INVOKABLE void selectFolder(const QString &path);

    /// Renames the playlist at `path` -- a folder directly under the music
    /// folder -- to `name`, on disk, and brings everything that pointed at the
    /// old name along: the index, the selection, where downloads land, and the
    /// queue playing out of it.
    ///
    /// An empty string comes back when the folder is now called that, and
    /// otherwise the key of the string saying why it is not, for the interface
    /// to show where it asked. The folder and the index are settled by the time
    /// this returns; the rail and the queue are told on the next turn of the
    /// event loop, because the row the call came from is one the rail rebuilds.
    [[nodiscard]] Q_INVOKABLE QString renamePlaylist(const QString &path, const QString &name);

    /// Narrows the list to one group of the browse mode in force: the tracks
    /// behind the artist, album or genre row that was opened. `key` is the
    /// row's own, and `name` is what the interface then calls the group.
    Q_INVOKABLE void openGroup(const QString &key, const QString &name);

    /// Lifts that narrowing, so the whole of what was being browsed shows
    /// again. What the chip above the list does.
    Q_INVOKABLE void closeGroup();

    /// Plays proxy row `row`, making the currently visible list the queue.
    /// Under a shuffle mode the row that was clicked leads and the rest of the
    /// pool follows at random.
    Q_INVOKABLE void playRow(int row);

    /// Steps the shuffle mode on: off, then the list that is playing, then the
    /// whole library, and back to off. What the transport's button does.
    Q_INVOKABLE void cycleShuffle();

    Q_INVOKABLE void clearFilter();

    /// `image://cover/...` for a track: the picture cropped to `width` x
    /// `height`, softened by `blur` and cut to `round`, both in output pixels.
    /// A zero size, blur or round leaves that step out, and an empty string
    /// means the file carries no artwork.
    ///
    /// The transforms are baked into the image rather than applied by a shader,
    /// so a rounded pane keeps its corners on a backend where effects do not
    /// run.
    [[nodiscard]] Q_INVOKABLE QString coverUrl(const QString &path,
                                               int width,
                                               int height,
                                               int blur = 0,
                                               int round = 0) const;

    /// `m:ss`, or `h:mm:ss` past an hour.
    [[nodiscard]] Q_INVOKABLE QString formatTime(double seconds) const;

    /// Seeks to the moment line `row` of the lyrics starts, which is what
    /// clicking a line in the pane does.
    Q_INVOKABLE void playLyricLine(int row);

    /// Asks LRCLIB again about the track that is playing, even if it has
    /// already answered for it.
    Q_INVOKABLE void retryLyrics();

    /// Searches the online catalogue for music to download. Results land in
    /// `downloads()`; the files themselves land in `downloadFolder()`.
    Q_INVOKABLE void searchOnline(const QString &query);

    /// Downloads result `row`, or fetches it again when it is already in the
    /// library.
    Q_INVOKABLE void downloadResult(int row);

    /// Stops the download of result `row`, queued or in flight, and deletes
    /// what it had written so far.
    Q_INVOKABLE void cancelDownload(int row);

    /// Cancels a running scan and tears the player down. Called on quit.
    void shutdown();

signals:
    void tracksChanged();
    void statusChanged();
    void scanningChanged();
    void rootFolderChanged();
    void foldersChanged();
    void selectedFolderChanged();
    void filterChanged();
    void browseChanged();
    void groupsChanged();
    void groupChanged();
    void currentRowChanged();
    void libraryErrorChanged();
    void lyricsVisibleChanged();
    void onlineLyricsChanged();
    void searchingOnlineChanged();
    void onlineMessageChanged();
    void downloadFolderChanged();
    void shuffleChanged();
    void playbackSourceChanged();

private:
    void onScanFinished();
    void onScanProgress(int indexed, const QString &title);
    void onPlayerPathChanged();
    void onPlayerPositionChanged();
    void onLyricsResolved(const QString &path, const Lyrics &lyrics);
    void onLyricsLoading(const QString &path, bool loading);
    void reloadTracks();

    /// What every change to what the list shows has to say: the rows are
    /// different, the groups they are shown as may be, and the row the playing
    /// track sits on may be too.
    void refiltered();

    /// Rebuilds the groups from the rows the filter accepts. One pass over the
    /// library, and only while a grouped mode is showing: in the flat list
    /// there are no groups to keep in step.
    void rebuildGroups();

    /// Drops the open group without announcing it, and answers whether there
    /// was one: for the callers that are already announcing a bigger change --
    /// another playlist, another browse mode -- of which the group is only a
    /// part.
    [[nodiscard]] bool dropGroup();

    /// Starts the lookup for whatever is playing now. Does nothing when the
    /// track is not in the index, which is the case before the first scan.
    void requestLyrics(bool force = false);

    /// Recomputes `folders()` from the indexed paths: the top level under the
    /// root, plus an "All music" entry. Also drops `selectedFolder` when the
    /// rescan proved it no longer exists.
    void rebuildFolders();
    void setStatus(const Message &status);
    void setScanning(bool scanning);
    void setRootFolder(const QString &folder);
    void setCurrentRow(int row);
    void setLibraryError(const Message &error);
    void setSearchingOnline(bool searching);
    void setOnlineMessage(const Message &message);

    /// Announces the strings that are already on screen again, because the
    /// language they are written in changed. They are held as keys, so
    /// nothing has to be rebuilt: the interface re-reads them.
    void retranslate();

    /// Points the downloader at the folder downloads land in -- the selected
    /// playlist, or the music folder when nothing is selected -- and tells the
    /// interface when that has moved.
    void refreshDownloadFolder();

    /// Whether the queue is being dealt again at all, and whether that deal is
    /// drawn from the whole library rather than from the list it started in.
    [[nodiscard]] bool shuffling() const { return m_shuffle != QLatin1String("off"); }
    [[nodiscard]] bool shufflingLibrary() const { return m_shuffle == QLatin1String("library"); }

    /// Deals the part of the queue that has not been played yet again, from the
    /// pool the mode names: the list playback started from, or every indexed
    /// track. The track that is playing is left alone.
    void reorderQueue();

    /// Every indexed track, in the album order the library keeps them in: the
    /// pool the library shuffle draws on.
    [[nodiscard]] QStringList libraryPaths() const;

    /// What the interface calls the list a folder shows: its last path element,
    /// or the name of the whole library when nothing narrows the library down.
    [[nodiscard]] QString folderName(const QString &folder) const;

    /// Declared first: everything below that answers the user in words asks it
    /// for them.
    Translator m_translator;
    Library m_library;
    TrackModel m_model{m_translator};
    TrackFilterProxy m_proxy;
    GroupModel m_groups{m_translator};
    Player m_player;
    LyricsModel m_lyricsModel;
    LyricsProvider m_lyrics;
    DownloadModel m_downloadModel;
    Downloader m_downloader{&m_downloadModel};

    Scanner *m_scanner = nullptr;
    QFutureWatcher<int> m_watcher;

    QString m_rootFolder;
    QVariantList m_folders;
    QString m_selectedFolder;
    /// What the list is a list of, as the interface names it. Part of looking
    /// at the library rather than a preference: not remembered across launches,
    /// so a new session starts at the flat list.
    QString m_browse;
    /// The group the list is narrowed to: its opaque key, which is what the
    /// proxy matches rows against, and the name the interface calls it by.
    QString m_groupKey;
    QString m_groupName;
    /// The three lines the interface shows that come from here rather than
    /// from a model: what the walk and the downloads are doing, what the last
    /// search answered, and why the library could not be opened. Held as keys
    /// so that changing the language rewrites them.
    Message m_status;
    Message m_libraryError;
    Message m_onlineMessage;
    bool m_scanning = false;
    int m_currentRow = -1;
    /// What was handed to the player as the queue, kept in step with `playRow`:
    /// mpv holds the playlist itself, but a folder that is renamed moves out
    /// from under paths only this copy can rewrite.
    QStringList m_queue;
    /// How that queue is played: `off`, `list` -- dealt again at random -- or
    /// `library`, which draws on every indexed track. Remembered across
    /// launches, because it is how the listener wants to listen rather than
    /// what they are listening to.
    QString m_shuffle = QStringLiteral("off");
    /// The list the queue was built from, in the order the library shows it:
    /// what a shuffle draws on when it is narrowed to a list, and the order the
    /// queue goes back to when shuffling is turned off. Remembered when
    /// playback starts, because browsing to another playlist -- or searching --
    /// must not change either.
    QStringList m_sourceList;
    /// The playlist that list was shown under, the group it was opened in, and
    /// whether it was the answer to a search. Together they are the name the
    /// interface calls it by.
    QString m_sourceFolder;
    QString m_sourceGroup;
    bool m_sourceSearched = false;
    /// The lyrics pane is off until it is asked for: a column of text over
    /// half the view is not something to have on without saying so. The
    /// choice is remembered, so turning it on once is enough.
    bool m_lyricsVisible = false;
    bool m_searchingOnline = false;
    /// Set when a download lands while a walk is running: the walk that is
    /// running may have gone past the folder already, so one more is owed.
    bool m_rescanPending = false;
    /// Folder the downloader was last pointed at, so the interface is only
    /// told when it actually moves.
    QString m_downloadFolder;
};
