#include "Downloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#if defined(Q_OS_UNIX)
#include <csignal>
#endif

namespace {

/// The search page, asked of YouTube Music itself. yt-dlp reads it and answers
/// JSON; `sp` is not set, so the page mixes songs and releases instead of the
/// song catalogue alone.
constexpr auto kMusicSearchUrl = "https://music.youtube.com/search?q=";

/// Entries one page is read for. Without a limit yt-dlp walks YouTube Music's
/// continuations to the end of the page: 474 entries for "los prisioneros", and
/// twenty seconds of it. The first thirty carry every result the interface
/// shows, and cost a second and a half.
constexpr int kPageEntries = 30;

/// Candidates resolved before ranking. The page is read for more than this and
/// what is worth downloading is chosen from what comes back -- a release is
/// ranked above a re-upload that YouTube happened to put first.
constexpr int kCandidates = 12;
/// Candidates resolved at once. Each one is a yt-dlp run of its own, so this is
/// also how many of them are in the air together.
constexpr int kProbeWorkers = 6;

/// Which of YouTube's player clients yt-dlp is allowed to ask. `default` comes
/// first, and `android_vr` is the one that still serves a stream URL that needs
/// no proof-of-origin token, which is what makes a download work from an
/// address YouTube has never seen before. Measured: with yt-dlp 2026.03.17 both
/// answer 403, with 2026.08.19 the file arrives, so an old yt-dlp is worth
/// refusing (see `packaging/tools/fetch-tools.sh`).
constexpr auto kPlayerClients = "youtube:player_client=default,android_vr";

/// What the audio is converted to. One lossy container every player reads, at
/// the bitrate YouTube's own audio was taken from.
constexpr auto kAudioFormat = "mp3";
constexpr auto kAudioQuality = "320K";

/// One line of progress per update, as `percent|track|tracks`. Everything else
/// yt-dlp writes on stdout is its own narration and is ignored.
constexpr auto kProgressTemplate = "cadenza:%(progress._percent_str)s|%(info.playlist_index)s|%(info.playlist_count)s";
constexpr auto kProgressPrefix = "cadenza:";
/// What yt-dlp writes before the path of every file it puts in place.
constexpr auto kDestinationMarker = "Destination: ";

/// Where a tool lives: `tools/` next to the executable first, which is where
/// the packages stage them, then beside the executable, and then `PATH`. Each
/// of them is shipped with the app or found on the machine, and which one it
/// was is not something the interface has to know.
QString findTool(const QString &name)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList directories = {appDir + QStringLiteral("/tools"), appDir};

#if defined(Q_OS_WIN)
    const QStringList names = {name + QStringLiteral(".exe"), name};
#else
    const QStringList names = {name};
#endif

    for (const QString &file : names)
    {
        for (const QString &directory : directories)
        {
            const QString path = directory + QLatin1Char('/') + file;
            if (QFileInfo(path).isExecutable())
                return path;
        }
    }

    return QStandardPaths::findExecutable(name);
}

/// What `--js-runtimes` is given: the runtime yt-dlp deciphers YouTube's stream
/// URLs with. A bundled quickjs wins because it is always there, then the ones
/// a machine tends to have anyway. An empty answer leaves yt-dlp with its own
/// default, which is a runtime this machine may not have at all.
QString jsRuntime()
{
    for (const char *name : {"qjs", "quickjs"})
    {
        const QString path = findTool(QString::fromLatin1(name));
        if (!path.isEmpty())
            return QStringLiteral("quickjs:") + path;
    }

    for (const char *name : {"node", "deno", "bun"})
    {
        if (!QStandardPaths::findExecutable(QString::fromLatin1(name)).isEmpty())
            return QString::fromLatin1(name);
    }

    return QString();
}

/// A path element that means the same thing on every platform. The separators
/// and the characters Windows refuses are folded to spaces: a folder the
/// filesystem will not take would fail the download after it had been paid for,
/// and release titles are full of them.
QString sanitiseName(const QString &name)
{
    static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
    QString clean = name;
    clean.replace(unsafe, QStringLiteral(" "));
    clean = clean.simplified();

    while (clean.endsWith(QLatin1Char('.')))
        clean.chop(1);

    if (clean.isEmpty() || clean == QStringLiteral(".") || clean == QStringLiteral(".."))
        return QStringLiteral("Album");

    return clean.left(120);
}

/// The name YouTube Music gives a release carries what it is: "Album - Corazones".
QString stripKind(const QString &title)
{
    for (const char *prefix : {"Album - ", "Playlist - ", "Mix - ", "Single - "})
    {
        const QLatin1String start(prefix);
        if (title.startsWith(start))
            return title.mid(start.size());
    }

    return title;
}

/// The first field of `object` that holds text. yt-dlp answers the same thing
/// under different names depending on the client it had to use, and any of them
/// is good enough to label a row with.
QString firstText(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys)
    {
        const QString text = object.value(QLatin1String(key)).toString().trimmed();
        if (!text.isEmpty())
            return text;
    }

    return QString();
}

/// The same picture, at the size a row shows it. The file name is the size:
/// `maxresdefault.jpg` is the master, `mqdefault.jpg` the 320-wide copy, and a
/// URL that names its sizes some other way is left as it is.
QString smallCover(const QString &url)
{
    for (const char *big : {"maxresdefault.jpg", "sddefault.jpg", "hqdefault.jpg"})
    {
        const QLatin1String suffix(big);
        if (url.endsWith(suffix))
            return url.chopped(suffix.size()) + QStringLiteral("mqdefault.jpg");
    }

    return url;
}

/// True when Qt can be expected to draw this picture. YouTube offers the same
/// frame as WebP as often as JPEG, and a Qt without its WebP plugin -- the usual
/// case for a distribution's Qt -- answers the former with "unsupported image
/// format" and draws nothing at all.
bool decodable(const QString &url)
{
    static const QStringList suffixes{QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                      QStringLiteral("png")};
    return suffixes.contains(QFileInfo(QUrl(url).path()).suffix().toLower());
}

/// The cover a row is drawn with, at the size a row needs it.
///
/// YouTube serves one picture at a handful of sizes and names the size in the
/// URL, so a 44-pixel row can ask for the 320-wide copy instead of the 1280-wide
/// master and still look right. A release carries no picture of its own on the
/// page yt-dlp reads; its first track does, and in YouTube Music a track's
/// picture is the release's cover.
QString coverUrl(const QJsonObject &info, const QJsonObject &first)
{
    for (const QJsonObject &source : {first, info})
    {
        if (source.isEmpty())
            continue;

        const QString chosen = source.value(QStringLiteral("thumbnail")).toString();
        if (decodable(chosen))
            return smallCover(chosen);

        // Either there is no chosen URL or it is one Qt cannot draw: the list
        // holds every size that was made, smallest first, and what a row wants
        // is the first that is both drawable and not a postage stamp.
        QString smallest;
        for (const QJsonValue &value : source.value(QStringLiteral("thumbnails")).toArray())
        {
            const QJsonObject thumb = value.toObject();
            const QString url = thumb.value(QStringLiteral("url")).toString();
            if (!decodable(url))
                continue;

            if (smallest.isEmpty())
                smallest = url;

            if (thumb.value(QStringLiteral("width")).toInt() >= 200)
                return smallCover(url);
        }

        if (!smallest.isEmpty())
            return smallCover(smallest);
    }

    return QString();
}

/// True when a URL from the page holds several tracks. An artist page is not
/// one, and neither are the auto-generated mixes YouTube Music labels
/// `VLRDCLAK5uy_`: what they hold is a radio station, not a release.
bool looksLikeRelease(const QString &url)
{
    return url.contains(QLatin1String("/browse/MPREb_"))
        || url.contains(QLatin1String("playlist?list="))
        || url.contains(QLatin1String("/browse/VLPL"));
}

/// The percentage in one progress line, or a negative number when the line is
/// not one.
double percentOf(const QString &text)
{
    QString number = text.trimmed();
    if (number.endsWith(QLatin1Char('%')))
        number.chop(1);

    bool ok = false;
    const double percent = number.trimmed().toDouble(&ok);
    return ok ? percent : -1.0;
}

/// The last line of `text` that is not blank: what yt-dlp's own error message is
/// before the traceback that follows it.
QString lastLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto line = lines.crbegin(); line != lines.crend(); ++line)
    {
        const QString trimmed = line->trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }

    return QString();
}

}  // namespace

Downloader::Downloader(DownloadModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    m_ytdlp = findTool(QStringLiteral("yt-dlp"));
    m_ffmpeg = findTool(QStringLiteral("ffmpeg"));
    m_runtime = jsRuntime();
}

Downloader::~Downloader()
{
    stopSearch();

    // The process is a child, so it would be killed anyway; killing it here
    // first is what keeps Qt from warning about a process destroyed while it was
    // still running.
    if (m_job != nullptr)
        m_job->kill();
}

void Downloader::setTargetFolder(const QString &folder)
{
    // A download already running keeps the folder it started in: moving a
    // half-written file because the sidebar selection changed would be worse
    // than finishing where it began.
    m_folder = folder;
}

/// The arguments every yt-dlp call carries. `--ignore-config` is the one that
/// matters most: without it the user's own `~/.config/yt-dlp/config` decides the
/// output template and the format, and the file would land somewhere this class
/// is not looking.
QStringList Downloader::commonArguments() const
{
    QStringList arguments{QStringLiteral("--ignore-config"), QStringLiteral("--no-warnings")};

    if (!m_runtime.isEmpty())
        arguments << QStringLiteral("--js-runtimes") << m_runtime;

    if (!m_ffmpeg.isEmpty())
        arguments << QStringLiteral("--ffmpeg-location") << m_ffmpeg;

    arguments << QStringLiteral("--extractor-args") << QString::fromLatin1(kPlayerClients);

    return arguments;
}

QProcess *Downloader::spawn(const QStringList &arguments)
{
    QProcess *process = new QProcess(this);
    process->setProgram(m_ytdlp);
    process->setArguments(arguments);
    process->start();
    return process;
}

void Downloader::guardStart(QProcess *process, const std::function<void()> &onFailed)
{
    connect(process, &QProcess::errorOccurred, this,
            [this, process, onFailed](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;

                m_diagnostic = process->errorString();
                m_startFailed = true;
                onFailed();
            });
}

void Downloader::search(const QString &query)
{
    const QString term = query.trimmed();
    if (term.isEmpty())
        return;

    if (m_ytdlp.isEmpty())
    {
        emit message(Message{.key = QStringLiteral("download_no_ytdlp")});
        return;
    }

    // A search still in flight belongs to a query nobody is looking at any
    // more; the downloads behind it are not the search's business.
    stopSearch();

    m_query = term;
    m_searchAttempts = {
        QString::fromLatin1(kMusicSearchUrl) + QString::fromUtf8(QUrl::toPercentEncoding(term)),
        QStringLiteral("ytsearch%1:%2").arg(kPageEntries).arg(term),
    };

    setSearching(true);
    startSearch();
}

void Downloader::startSearch()
{
    if (m_searchAttempts.isEmpty())
    {
        setSearching(false);
        emit message(m_searchFailed ? Message{.key = QStringLiteral("download_unreachable")}
                                    : Message{.key = QStringLiteral("download_not_found"),
                                              .values = {{QStringLiteral("query"), m_query}}});
        return;
    }

    const QString target = m_searchAttempts.takeFirst();
    m_startFailed = false;

    QStringList arguments = commonArguments();
    arguments << QStringLiteral("--flat-playlist") << QStringLiteral("-J")
              << QStringLiteral("-I") << QStringLiteral("1:%1").arg(kPageEntries)
              << target;

    m_search = spawn(arguments);
    guardStart(m_search, [this] { onSearchFinished(); });
    connect(m_search, &QProcess::finished, this, &Downloader::onSearchFinished);
}

void Downloader::onSearchFinished()
{
    QProcess *process = m_search;
    if (process == nullptr)
        return;  // a search that was taken down: its page is not wanted

    m_search = nullptr;
    const int status = process->exitStatus() == QProcess::NormalExit ? process->exitCode() : -1;
    const QByteArray body = process->readAllStandardOutput();
    process->deleteLater();

    m_searchFailed = m_startFailed || status != 0;

    const QJsonArray entries = QJsonDocument::fromJson(body)
                                   .object()
                                   .value(QStringLiteral("entries"))
                                   .toArray();

    m_candidates.clear();
    for (const QJsonValue &value : entries)
    {
        const QJsonObject entry = value.toObject();
        const QString url = entry.value(QStringLiteral("url")).toString();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (url.isEmpty() || id.isEmpty())
            continue;

        const bool song = entry.value(QStringLiteral("ie_key")).toString() == QLatin1String("Youtube");
        const bool release = looksLikeRelease(url);
        if (!song && !release)
            continue;

        m_candidates.append(Candidate{id, QUrl(url), release});
        if (m_candidates.size() >= kCandidates)
            break;
    }

    // Nothing on this page: ask the next question, or say there was nothing.
    if (m_candidates.isEmpty())
    {
        startSearch();
        return;
    }

    m_resolved = QVector<Resolved>(m_candidates.size());
    m_pending.clear();
    for (int index = 0; index < m_candidates.size(); ++index)
        m_pending.append(index);

    for (int worker = 0; worker < kProbeWorkers; ++worker)
        startProbe();

    // Every candidate was handed to a probe above; this only fires when there
    // was somehow none, so a search can never be left waiting on nothing.
    if (m_probes.isEmpty())
        finishSearch();
}

void Downloader::startProbe()
{
    if (m_pending.isEmpty() || m_probes.size() >= kProbeWorkers)
        return;

    const int index = m_pending.takeFirst();

    // One entry, without downloading it: enough to know what the candidate is
    // and what it is called. `-I 1:1` keeps a release from listing its tracks
    // back, which is the difference between a second and half a minute.
    QStringList arguments = commonArguments();
    arguments << QStringLiteral("-J") << QStringLiteral("--skip-download")
              << QStringLiteral("-I") << QStringLiteral("1:1")
              << m_candidates.at(index).url.toString();

    QProcess *process = spawn(arguments);
    m_probeIndex.insert(process, index);
    m_probes.append(process);
    guardStart(process, [this, process] { onProbeFinished(process); });
    connect(process, &QProcess::finished, this, [this, process] { onProbeFinished(process); });
}

void Downloader::onProbeFinished(QProcess *process)
{
    if (process == nullptr || !m_probeIndex.contains(process))
        return;  // a probe that was taken down: its answer is not wanted

    const int index = m_probeIndex.take(process);
    m_probes.removeAll(process);
    const QByteArray body = process->readAllStandardOutput();
    process->deleteLater();

    const QJsonObject info = QJsonDocument::fromJson(body).object();
    const bool playlist = info.value(QStringLiteral("_type")).toString() == QLatin1String("playlist");
    const QJsonArray tracks = info.value(QStringLiteral("entries")).toArray();
    const QJsonObject first = tracks.isEmpty() ? QJsonObject() : tracks.at(0).toObject();

    const Candidate &candidate = m_candidates.at(index);
    DownloadItem item;
    item.id = candidate.id;
    item.url = candidate.url.toString();
    item.album = playlist;

    if (playlist)
    {
        // The release's own row: what it is called, who it is by, and how many
        // tracks it holds. The artist is not on the page itself, so the first
        // track answers for all of them.
        item.title = stripKind(firstText(info, {"title", "playlist_title"}));
        item.artist = firstText(info, {"channel", "uploader"});
        if (item.artist.isEmpty())
            item.artist = firstText(first, {"artist", "creator", "album_artist", "uploader", "channel"});
        item.year = QString::number(first.value(QStringLiteral("release_year")).toInt());
        item.title = stripKind(item.title.isEmpty() ? firstText(first, {"album", "playlist_title"}) : item.title);
        item.fileCount = info.value(QStringLiteral("playlist_count")).toInt();
    }
    else
    {
        item.title = firstText(info, {"track", "title"});
        item.artist = firstText(info, {"artist", "creator", "album_artist", "uploader", "channel"});
        item.year = QString::number(info.value(QStringLiteral("release_year")).toInt());
    }

    // The year is only worth showing when there is one; yt-dlp answers 0 for
    // everything it does not know.
    if (item.year == QLatin1String("0"))
        item.year.clear();

    if (item.title.isEmpty())
        item.title = item.id;

    item.thumbnail = coverUrl(info, first);

    Resolved resolved;
    resolved.item = item;
    // An upload carrying music metadata is the release; one that carries only
    // the uploader is somebody's copy of it.
    resolved.music = playlist || (!item.artist.isEmpty()
                                  && !firstText(info, {"album"}).isEmpty());
    m_resolved[index] = resolved;

    startProbe();

    if (m_pending.isEmpty() && m_probes.isEmpty())
        finishSearch();
}

void Downloader::finishSearch()
{
    QVector<Resolved> results;
    for (const Resolved &entry : std::as_const(m_resolved))
    {
        if (!entry.item.id.isEmpty())
            results.append(entry);
    }

    // Releases and music uploads first, each group in the order YouTube ranked
    // it: the top hit for a song is often a re-upload by an unrelated channel,
    // and what is worth downloading is the release.
    std::stable_sort(results.begin(), results.end(), [](const Resolved &left, const Resolved &right) {
        return left.music && !right.music;
    });

    QVector<DownloadItem> items;
    items.reserve(results.size());
    for (const Resolved &entry : std::as_const(results))
        items.append(entry.item);

    m_model->setItems(items);
    setSearching(false);

    m_candidates.clear();
    m_resolved.clear();

    if (items.isEmpty())
        emit message(Message{.key = QStringLiteral("download_not_found"),
                             .values = {{QStringLiteral("query"), m_query}}});
}

void Downloader::stopSearch()
{
    // Pointers are dropped before the processes are taken down: killing one
    // makes it emit `finished`, and the handler that lands in must find nothing
    // to do.
    if (m_search != nullptr)
    {
        QProcess *stale = m_search;
        m_search = nullptr;
        stale->kill();
        stale->deleteLater();
    }

    for (QProcess *probe : std::as_const(m_probes))
    {
        probe->kill();
        probe->deleteLater();
    }

    m_probes.clear();
    m_probeIndex.clear();
    m_candidates.clear();
    m_resolved.clear();
    m_pending.clear();
    m_searchAttempts.clear();
    m_searchFailed = false;
}

void Downloader::download(int row)
{
    const QVector<DownloadItem> &items = m_model->items();
    if (row < 0 || row >= items.size())
        return;

    const DownloadItem item = items.at(row);
    if (item.id.isEmpty() || item.state == DownloadItem::State::Waiting
        || item.state == DownloadItem::State::Working)
    {
        return;
    }

    if (m_folder.isEmpty())
    {
        failItem(item.id, Message{.key = QStringLiteral("download_no_folder")});
        return;
    }

    if (m_ytdlp.isEmpty())
    {
        failItem(item.id, Message{.key = QStringLiteral("download_no_ytdlp")});
        return;
    }

    m_queue.append(item.id);
    setProgress(item.id, 0.0);
    setState(item.id, DownloadItem::State::Waiting);

    startNext();
}

void Downloader::startNext()
{
    if (m_job != nullptr)
        return;

    if (m_queue.isEmpty())
    {
        m_active.clear();
        return;
    }

    const QString id = m_queue.takeFirst();
    const int row = m_model->rowForId(id);
    if (row < 0)
    {
        // A search replaced the rows while this one waited; the item is gone
        // with them, and there is nothing left to write for it.
        startNext();
        return;
    }

    const DownloadItem item = m_model->items().at(row);

    m_active = id;
    m_title = item.title;
    m_release = item.album;
    m_tracks = item.album ? std::max(1, item.fileCount) : 1;
    m_percent = -1;
    m_lastFile.clear();
    m_diagnostic.clear();
    m_cancelling = false;

    // A release lands in a folder of its own inside the selected one, which is
    // also what makes it one playlist in this player. A single recording lands
    // beside the others.
    m_destination = item.album ? QDir(m_folder).filePath(sanitiseName(item.title)) : m_folder;
    if (!QDir().mkpath(m_destination))
    {
        failItem(id, Message{.key = QStringLiteral("download_unwritable"),
                             .values = {{QStringLiteral("path"), m_destination}}});
        return;
    }

    const QString name = item.album ? QStringLiteral("%(playlist_index)02d - %(title)s.%(ext)s")
                                    : QStringLiteral("%(title)s.%(ext)s");

    QStringList arguments = commonArguments();
    if (!m_ffmpeg.isEmpty())
    {
        arguments << QStringLiteral("-x")
                  << QStringLiteral("--audio-format") << QString::fromLatin1(kAudioFormat)
                  << QStringLiteral("--audio-quality") << QString::fromLatin1(kAudioQuality)
                  << QStringLiteral("--embed-metadata")
                  << QStringLiteral("--embed-thumbnail")
                  << QStringLiteral("--convert-thumbnails") << QStringLiteral("jpg");
    }
    else
    {
        // No ffmpeg: the container YouTube serves is written as it comes, with
        // no conversion and nothing written into its tags.
        arguments << QStringLiteral("-f") << QStringLiteral("bestaudio");
    }

    arguments << QStringLiteral("--newline")
              << QStringLiteral("--progress-template") << QString::fromLatin1(kProgressTemplate)
              // Intermediates in the temp folder, the finished file in the
              // destination: `-o` is the name only, and the two `-P` decide
              // which folder each end of the work happens in.
              << QStringLiteral("-P") << QStringLiteral("temp:") + tempFolder()
              << QStringLiteral("-P") << QStringLiteral("home:") + m_destination
              << QStringLiteral("-o") << name
              << item.url;

    setState(id, DownloadItem::State::Working,
             Message{.key = QStringLiteral("download_progress"),
                     .values = {{QStringLiteral("percent"), 0}}});
    emit progress(m_tracks > 1
                      ? Message{.key = QStringLiteral("download_status_files"),
                                .values = {{QStringLiteral("index"), 1},
                                           {QStringLiteral("total"), m_tracks},
                                           {QStringLiteral("title"), m_title}}}
                      : Message{.key = QStringLiteral("download_status_one"),
                                .values = {{QStringLiteral("title"), m_title}}});

    m_job = spawn(arguments);
    guardStart(m_job, [this] { onJobFinished(1, QProcess::CrashExit); });
    connect(m_job, &QProcess::readyReadStandardOutput, this, [this] {
        if (m_job != nullptr)
            readOutput(m_job);
    });
    connect(m_job, &QProcess::readyReadStandardError, this, [this] {
        if (m_job != nullptr)
            m_diagnostic = lastLine(QString::fromUtf8(m_job->readAllStandardError()));
    });
    connect(m_job, &QProcess::finished, this, &Downloader::onJobFinished);
}

/// Reads what yt-dlp has written on stdout so far: one line of progress, or the
/// path of a file that is in place. The process is handed in rather than taken
/// from the member, because the last of its output arrives with the exit and
/// there is no process in flight any more by then.
void Downloader::readOutput(QProcess *process)
{
    while (process->canReadLine())
    {
        const QString line = QString::fromUtf8(process->readLine()).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String(kProgressPrefix)))
        {
            const QStringList parts = line.mid(QLatin1String(kProgressPrefix).size())
                                          .split(QLatin1Char('|'));

            const double percent = percentOf(parts.value(0));
            if (percent < 0.0)
                continue;

            const int track = parts.value(1).toInt();
            const int tracks = parts.value(2).toInt();
            if (tracks > 0)
                m_tracks = tracks;

            const int index = track > 0 ? track : 1;
            setProgress(m_active, (index - 1 + percent / 100.0) / double(std::max(1, m_tracks)));

            // The row is told once per whole percent: a download reports a
            // dozen times a second, and the model is not the place to put that.
            if (qRound(percent) == m_percent)
                continue;

            m_percent = qRound(percent);

            if (m_tracks > 1)
            {
                setState(m_active, DownloadItem::State::Working,
                         Message{.key = QStringLiteral("download_progress_track"),
                                 .values = {{QStringLiteral("index"), index},
                                            {QStringLiteral("total"), m_tracks},
                                            {QStringLiteral("percent"), m_percent}}});
            }
            else
            {
                setState(m_active, DownloadItem::State::Working,
                         Message{.key = QStringLiteral("download_progress"),
                                 .values = {{QStringLiteral("percent"), m_percent}}});
            }
        }
        else if (line.contains(QLatin1String(kDestinationMarker)))
        {
            // yt-dlp announces every file it writes this way -- the download
            // itself and then each post-processed copy of it -- so the last one
            // is the file that is in place. `--print after_move:filepath` would
            // say so in one line, but asking for it turns the progress off.
            const QString path = line.section(QLatin1String(kDestinationMarker), 1).trimmed();
            if (!path.isEmpty() && QFileInfo(path).suffix() != QLatin1String("part"))
                m_lastFile = path;
        }
    }
}

void Downloader::onJobFinished(int exitCode, QProcess::ExitStatus status)
{
    QProcess *process = m_job;
    if (process == nullptr)
        return;  // a download that was taken down: its exit code says nothing

    m_job = nullptr;
    readOutput(process);

    const QString id = m_active;
    const bool cancelled = m_cancelling;
    const QString said = lastLine(QString::fromUtf8(process->readAllStandardError()));
    process->deleteLater();

    m_cancelling = false;

    if (cancelled)
    {
        discardPartials(m_destination);

        if (!id.isEmpty())
        {
            setState(id, DownloadItem::State::Idle,
                     Message{.key = QStringLiteral("download_cancelled_row")});
            setProgress(id, 0.0);
        }

        emit progress(Message{.key = QStringLiteral("download_cancelled_status")});
        m_active.clear();
        m_lastFile.clear();
        m_destination.clear();
        m_release = false;
        startNext();
        return;
    }

    if (status != QProcess::NormalExit || exitCode != 0)
    {
        discardPartials(m_destination);
        failItem(id, Message{.key = QStringLiteral("download_stopped"),
                             .values = {{QStringLiteral("reason"),
                                         said.isEmpty() ? m_diagnostic : said}}});
        return;
    }

    Message detail;
    if (m_tracks > 1)
    {
        detail = Message{.key = QStringLiteral("download_saved_files"),
                         .values = {{QStringLiteral("count"), m_tracks},
                                    {QStringLiteral("folder"), QDir(m_destination).dirName()}}};
    }
    else
    {
        detail = Message{.key = QStringLiteral("download_saved_as"),
                         .values = {{QStringLiteral("name"),
                                     m_lastFile.isEmpty() ? m_title
                                                          : QFileInfo(m_lastFile).fileName()}}};
    }

    finishItem(detail);
}

/// Where yt-dlp keeps what it is working on: the container it downloaded, the
/// thumbnail, the state file, the pieces of a file still arriving. None of that
/// belongs in the folder the library watches -- a `.webm` next to the finished
/// `.mp3` is a second copy of the same track to a scanner that reads
/// extensions -- so `-P temp:` sends it all here, and the finished file is the
/// only thing that crosses over.
QString Downloader::tempFolder()
{
    if (m_temp.isEmpty())
    {
        m_temp = QDir(QDir::tempPath())
                     .filePath(QStringLiteral("cadenza-%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(m_temp);
    }

    return m_temp;
}

/// What a stopped or failed download leaves behind: everything in the temp
/// folder above, whatever `.part` of a file landed in the destination anyway,
/// and -- for a release -- the folder it had made for itself, once nothing else
/// is in it. Without this, cancelling would leave half a track in the folder
/// the library watches, and the next run of the same link would pick the pieces
/// back up.
void Downloader::discardPartials(const QString &folder)
{
    if (!m_temp.isEmpty())
        QDir(m_temp).removeRecursively();

    if (folder.isEmpty())
        return;

    QDir directory(folder);
    const QStringList patterns{QStringLiteral("*.part"), QStringLiteral("*.part-*"),
                               QStringLiteral("*.ytdl"), QStringLiteral("*.temp")};

    for (const QFileInfo &info : directory.entryInfoList(patterns, QDir::Files))
        QFile::remove(info.absoluteFilePath());

    // The release's own folder goes too, but only when the tracks already
    // written into it are not what would be taken away with it.
    if (m_release && folder != m_folder)
        directory.rmdir(directory.absolutePath());
}

void Downloader::stopJob()
{
    if (m_job == nullptr)
        return;

    QProcess *process = m_job;

#if defined(Q_OS_UNIX)
    // SIGINT, not SIGTERM: yt-dlp stops on the interrupt it already handles,
    // which takes away the partial file it was writing instead of leaving it in
    // the folder for the library to find.
    ::kill(qint64(process->processId()), SIGINT);
#else
    process->terminate();
#endif

    // A process that did not take the interrupt is killed outright, so a cancel
    // cannot leave the queue waiting on it.
    QTimer::singleShot(4000, this, [this, process] {
        if (m_job == process)
            process->kill();
    });
}

void Downloader::finishItem(const Message &detail)
{
    if (m_active.isEmpty())
        return;

    const QString id = m_active;
    const QString folder = m_destination;
    discardPartials(QString());
    m_active.clear();
    m_lastFile.clear();
    m_release = false;

    setState(id, DownloadItem::State::Done, detail);
    setProgress(id, 1.0);
    emit progress(detail);

    m_destination.clear();

    // The files are in the folder now, and the library does not know about them
    // yet.
    emit downloaded(folder);

    startNext();
}

void Downloader::failItem(const QString &id, const Message &detail)
{
    if (id == m_active)
    {
        m_active.clear();
        m_lastFile.clear();
        m_destination.clear();
        m_release = false;
    }

    // Everything the item still had queued goes with it: the same release
    // queued twice does not get a second run after the first one failed.
    m_queue.removeAll(id);

    setState(id, DownloadItem::State::Failed, detail);
    setProgress(id, 0.0);
    emit progress(detail);

    startNext();
}

void Downloader::cancel(int row)
{
    const QVector<DownloadItem> &items = m_model->items();
    if (row < 0 || row >= items.size())
        return;

    const QString id = items.at(row).id;
    const DownloadItem::State state = items.at(row).state;
    if (state != DownloadItem::State::Waiting && state != DownloadItem::State::Working)
        return;

    // The queue is drained first, so nothing that has not started can begin
    // while the process in the air is being taken down.
    m_queue.removeAll(id);

    if (id == m_active && m_job != nullptr)
    {
        m_cancelling = true;
        stopJob();
        return;  // the row is put back when the process is gone
    }

    setState(id, DownloadItem::State::Idle, Message{.key = QStringLiteral("download_cancelled_row")});
    setProgress(id, 0.0);
    emit progress(Message{.key = QStringLiteral("download_cancelled_status")});

    startNext();
}

void Downloader::cancelAll()
{
    stopSearch();

    m_queue.clear();

    if (m_job != nullptr)
    {
        m_cancelling = true;
        stopJob();
    }

    for (const DownloadItem &item : m_model->items())
    {
        if (item.state == DownloadItem::State::Waiting || item.state == DownloadItem::State::Working)
            setState(item.id, DownloadItem::State::Idle);
    }

    setSearching(false);
}

void Downloader::setState(const QString &id, DownloadItem::State state, const Message &detail)
{
    if (id.isEmpty())
        return;

    const int row = m_model->rowForId(id);
    if (row >= 0)
        m_model->setState(row, state, detail);
}

void Downloader::setProgress(const QString &id, double progress)
{
    if (id.isEmpty())
        return;

    const int row = m_model->rowForId(id);
    if (row >= 0)
        m_model->setProgress(row, progress);
}

void Downloader::setSearching(bool searching)
{
    if (m_searching == searching)
        return;

    m_searching = searching;
    emit searchingChanged(searching);
}
