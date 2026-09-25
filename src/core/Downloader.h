#pragma once

#include "DownloadModel.h"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>

/// Gets music from YouTube Music and puts it in the folder the library watches.
///
/// The catalogue is asked through `yt-dlp`, which is also what fetches the
/// files. It has to be: YouTube serves its audio behind a URL that is signed
/// and deciphered by a script, and the script is YouTube's, replaced whenever
/// it feels like it. Doing that here would be a rewrite every few weeks, and
/// yt-dlp already has it -- along with the tags, the cover art and the
/// conversion, which are ffmpeg's job and not something to re-do either.
///
/// What this class owns is what the interface needs: a search that answers rows
/// with a title and an artist, a queue that downloads them one at a time and
/// says how far it got, and files that appear in the music folder finished, so
/// the library indexes them like anything else.
///
/// The tools are looked up next to the executable first, in `tools/`, which is
/// where the packages stage them, and on `PATH` after that. `yt-dlp` and a
/// JavaScript runtime are what a download needs; without ffmpeg the audio is
/// saved in the container YouTube served, untagged, and the row is downloaded
/// all the same.
///
/// One search and one download are in flight at a time. A search is a handful
/// of yt-dlp runs -- one page, then one per candidate -- and a download is one
/// process writing into the folder the sidebar has selected.
class Downloader : public QObject
{
    Q_OBJECT

public:
    explicit Downloader(DownloadModel *model, QObject *parent = nullptr);
    ~Downloader() override;

    /// Where downloads land, created on first use. An empty folder means there
    /// is nowhere to put them, and `download()` says so rather than writing
    /// into whatever the process's working directory happens to be.
    void setTargetFolder(const QString &folder);
    [[nodiscard]] QString targetFolder() const { return m_folder; }

    /// Searches for `query`, replacing the results. An empty query does
    /// nothing; a query with nothing searchable in it is refused.
    void search(const QString &query);

    /// Queues row `row` of the model. An item that is already downloading is
    /// left alone; one that is in the library is fetched again, because a file
    /// that was deleted since should come back.
    void download(int row);

    /// Takes row `row` out of the queue, or stops it when it is the one
    /// running. A stopped track leaves what it had already written, and yt-dlp
    /// takes away the partial file it was writing.
    void cancel(int row);

    /// Stops the search, the download and everything behind it. Called on quit.
    void cancelAll();

signals:
    /// True from the moment a search leaves until its results are in.
    void searchingChanged(bool searching);
    /// One line about the search itself, for the view that asked for it:
    /// how many results, or what went wrong.
    void message(const Message &text);
    /// One line about the download in flight, for the window's status line.
    void progress(const Message &text);
    /// Files have landed in `folder`, which the library should be told about.
    void downloaded(const QString &folder);

private:
    /// One entry of a search page, before it is known what it holds: a video
    /// that is one recording, or a release whose tracks are what gets fetched.
    struct Candidate
    {
        QString id;
        QUrl url;
        bool album = false;
    };

    /// What one candidate turned out to be, plus what ranks it: an upload that
    /// carries music metadata ranks above a video somebody re-uploaded.
    struct Resolved
    {
        DownloadItem item;
        bool music = false;
    };

    void startSearch();
    void onSearchFinished();
    void startProbe();
    void onProbeFinished(QProcess *process);
    void finishSearch();
    void stopSearch();

    void startNext();
    void readOutput(QProcess *process);
    void onJobFinished(int exitCode, QProcess::ExitStatus status);
    void stopJob();
    [[nodiscard]] QString tempFolder();
    void discardPartials(const QString &folder);
    void finishItem(const Message &detail);
    void failItem(const QString &id, const Message &detail);

    void setState(const QString &id, DownloadItem::State state, const Message &detail = Message());
    void setProgress(const QString &id, double progress);
    void setSearching(bool searching);

    [[nodiscard]] QStringList commonArguments() const;
    QProcess *spawn(const QStringList &arguments);
    /// Turns "this program cannot be started" into the failure the caller
    /// already handles: Qt reports it through `errorOccurred` and never emits
    /// `finished`, so without this a truncated binary would leave a search or a
    /// download waiting on a process that never ran.
    void guardStart(QProcess *process, const std::function<void()> &onFailed);

    DownloadModel *m_model = nullptr;

    /// Folder every download lands in.
    QString m_folder;

    /// Where yt-dlp works, one folder per run of the app. Created the first
    /// time a download starts, emptied when one ends.
    QString m_temp;

    /// Looked up once, when the window opens: the tools do not move while it is
    /// up, and every call would otherwise walk the filesystem again.
    QString m_ytdlp;
    QString m_ffmpeg;
    /// What `--js-runtimes` is given, empty when there is nothing to give.
    QString m_runtime;

    QProcess *m_search = nullptr;
    /// Still to be asked, best first: the music search, then plain YouTube.
    QStringList m_searchAttempts;
    QString m_query;
    QVector<Candidate> m_candidates;
    /// One slot per candidate, filled in whatever order the probes answer.
    QVector<Resolved> m_resolved;
    QVector<int> m_pending;
    QVector<QProcess *> m_probes;
    /// Which candidate each probe is resolving.
    QHash<QProcess *, int> m_probeIndex;
    /// True when the last thing tried was a failure rather than an empty page,
    /// which is the difference between "nothing found" and "nothing reached".
    bool m_searchFailed = false;
    /// Set when the program could not be started at all, which no attempt is
    /// going to fix.
    bool m_startFailed = false;

    /// Items waiting their turn, by id: a search can land between two of them
    /// and renumber the rows underneath the queue.
    QVector<QString> m_queue;
    QProcess *m_job = nullptr;
    QString m_active;
    /// Title, folder and track count of the item being written.
    QString m_title;
    QString m_destination;
    int m_tracks = 1;
    /// True when the item is a release, which lands in a folder of its own: what
    /// tells a stopped download to take that folder away with it when nothing
    /// else is in it.
    bool m_release = false;
    /// The file yt-dlp wrote last, which is the one in place: what the row
    /// names when a single recording is done.
    QString m_lastFile;
    /// Last percentage put on the row, so the model is not told 0.1% at a time.
    int m_percent = -1;
    /// Set while a cancel is taking the process down: the exit code that comes
    /// back then is not a failure.
    bool m_cancelling = false;
    /// The last thing yt-dlp said on stderr, for the line a failure shows.
    QString m_diagnostic;

    bool m_searching = false;
};
