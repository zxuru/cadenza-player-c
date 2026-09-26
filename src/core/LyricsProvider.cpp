#include "LyricsProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QtConcurrent>

#include <taglib/apetag.h>
#include <taglib/fileref.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

/// Looked up from: https://lrclib.net/docs. A free, keyless, openly licensed
/// lyrics library that carries both synced (LRC) and plain lyrics, and is the
/// one service that asks only for a User-Agent string in return.
///
/// `get` answers one track and matches every field it is given exactly; it is
/// right only for files tagged the way LRCLIB spelled the track. `search`
/// matches loosely and answers many candidates, which is what the tags that
/// come off YouTube Music -- where the artist field names every voice on the
/// release -- need.
constexpr auto kLrclibEndpoint = "https://lrclib.net/api/get";
constexpr auto kLrclibSearchEndpoint = "https://lrclib.net/api/search";

/// How far a candidate's run time may be from the track's, in seconds. The
/// API's own `get` matches within two, and the search endpoint takes no
/// duration at all, so this is the filter its candidates pass here.
constexpr double kDurationTolerance = 2.0;

/// Written to `lyrics.source` and read back to decide whether a cached answer
/// may be trusted. Bumping it retires every answer an earlier lookup cached,
/// which is what a lookup that asks better questions needs: a miss cached by
/// the version that only knew the exact ask is not a fact about the track.
constexpr auto kCacheSource = "lrclib/v2";

/// Lyrics files are text; anything past this is not one.
constexpr qint64 kMaxLyricsBytes = 1024 * 1024;

/// Skipping through a queue must not become a burst against a free service,
/// and the API asks for a gap between requests.
constexpr int kRequestGapMs = 300;

/// Where a lyrics file for a track may live, after the file's own name.
const QStringList kSidecarExtensions{
    QStringLiteral(".lrc"),
    QStringLiteral(".LRC"),
    QStringLiteral(".txt"),
};

/// A name that can only ever be one path element.
bool isPlainName(const QString &text)
{
    return !text.isEmpty() && !text.contains(QLatin1Char('/'))
        && !text.contains(QLatin1Char('\\'));
}

/// Reads a lyrics file, tolerating what turns up in the wild: UTF-8 with or
/// without a BOM, UTF-16 as Windows tools write it, and the Latin-1 leftovers
/// of older taggers.
QString readLyricsFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    const QByteArray bytes = file.read(kMaxLyricsBytes);
    if (bytes.isEmpty())
        return QString();

    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(bytes.sliced(3));

    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF"))
    {
        QStringDecoder decoder(bytes.startsWith("\xFF\xFE") ? QStringDecoder::Utf16LE
                                                            : QStringDecoder::Utf16BE);
        return decoder.decode(bytes.sliced(2));
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(bytes);

    // Latin-1 maps every byte, so a file that is not UTF-8 still reads as
    // something rather than as replacement characters.
    return decoder.hasError() ? QString::fromLatin1(bytes) : text;
}

/// The text of a `.lrc` or `.txt` beside the audio file, or an empty string.
///
/// The three spellings cover the conventions in use: the file's own name,
/// which is what a player writes back; `Artist - Title`, which is what older
/// lyric tools wrote; and the bare title.
QString sidecarText(const Track &track)
{
    const QFileInfo audio(track.path);
    const QString directory = audio.absolutePath();
    const QString stem = audio.completeBaseName();

    QStringList names{stem};

    if (isPlainName(track.artist) && isPlainName(track.title))
    {
        const QString combined = track.artist + QStringLiteral(" - ") + track.title;
        if (combined != stem)
            names.append(combined);
    }

    if (isPlainName(track.title) && track.title != stem)
        names.append(track.title);

    const QDir folder(directory);
    for (const QString &name : std::as_const(names))
    {
        for (const QString &extension : kSidecarExtensions)
        {
            const QString candidate = folder.filePath(name + extension);
            if (QFileInfo::exists(candidate))
            {
                const QString text = readLyricsFile(candidate);
                if (!text.trimmed().isEmpty())
                    return text;
            }
        }
    }

    return QString();
}

/// The untimed lyrics of a tag, from whichever field the format uses.
///
/// `TagLib::PropertyMap` keys are case-insensitive, and TagLib maps a Vorbis
/// `lyrics` comment, a real ID3v2 `USLT` frame and an MP4 `©lyr` atom onto the
/// one name, so the canonical key alone covers every format the scanner
/// accepts -- the rest of the list is what other writers of the same formats
/// leave behind.
QString tagLyrics(TagLib::Tag *tag)
{
    if (tag == nullptr)
        return QString();

    auto firstValue = [](const TagLib::StringList &values) {
        for (const TagLib::String &value : values)
        {
            const QString text = TStringToQString(value).trimmed();
            if (!text.isEmpty())
                return text;
        }
        return QString();
    };

    const TagLib::PropertyMap properties = tag->properties();

    // `USLT` is not a typo: ffmpeg writes `-metadata lyrics=` as a `TXXX`
    // frame whose description is `USLT`, and TagLib keys those by their
    // description, so that is the name the text arrives under.
    static const char *const keys[] = {"LYRICS", "UNSYNCEDLYRICS", "SYNCEDLYRICS", "USLT"};
    for (const char *key : keys)
    {
        const QString text = firstValue(properties.value(key));
        if (!text.isEmpty())
            return text;
    }

    // A `USLT` frame that carries a description arrives as
    // `LYRICS:<description>`, so the exact keys above miss it.
    for (auto it = properties.begin(); it != properties.end(); ++it)
    {
        if (!it->first.upper().startsWith("LYRICS:"))
            continue;

        const QString text = firstValue(it->second);
        if (!text.isEmpty())
            return text;
    }

    return QString();
}

/// The timings of an ID3v2 `SYLT` frame, if the file carries one.
///
/// This is the only place a mainstream format stores lyrics already synced to
/// the audio, and it is rare but exact -- so when it is there it beats both
/// the untimed `USLT` and any guess made from a network lookup.
Lyrics syncedFromId3v2(TagLib::ID3v2::Tag *tag)
{
    if (tag == nullptr)
        return Lyrics();

    const TagLib::ID3v2::FrameList frames = tag->frameList("SYLT");
    for (const TagLib::ID3v2::Frame *frame : frames)
    {
        const auto *sylt = dynamic_cast<const TagLib::ID3v2::SynchronizedLyricsFrame *>(frame);
        if (sylt == nullptr
            || sylt->timestampFormat()
                != TagLib::ID3v2::SynchronizedLyricsFrame::AbsoluteMilliseconds)
        {
            continue;
        }

        Lyrics lyrics;
        for (const auto &entry : sylt->synchedText())
        {
            const QString text = TStringToQString(entry.text).trimmed();
            if (!text.isEmpty())
                lyrics.lines.append({entry.time / 1000.0, text});
        }

        if (!lyrics.lines.isEmpty())
        {
            lyrics.synced = true;
            return lyrics;
        }
    }

    return Lyrics();
}

/// Lyrics stored inside the audio file itself.
Lyrics embeddedLyrics(const QString &path)
{
    // TagLib keeps the pointer it is handed, so the encoded name outlives
    // every file object below.
    const QByteArray name = QFile::encodeName(path);

    // MP3 is the one format where a tag can carry real timings, so it gets the
    // frame-level look before falling back to the untimed text every format
    // shares. `false` skips the audio properties: only the tags are wanted.
    if (audioExtension(path) == QStringLiteral(".mp3"))
    {
        TagLib::MPEG::File file(name.constData(), false);
        if (!file.isValid())
            return Lyrics();

        TagLib::ID3v2::Tag *id3 = file.ID3v2Tag();

        if (Lyrics synced = syncedFromId3v2(id3); !synced.isEmpty())
            return synced;

        if (Lyrics lyrics = Lyrics::parse(tagLyrics(id3)); !lyrics.isEmpty())
            return lyrics;

        return Lyrics::parse(tagLyrics(file.APETag()));
    }

    // Everything else -- FLAC, Ogg, Opus, M4A, WavPack, APE, Musepack -- goes
    // through the format-independent view, which is what makes one code path
    // enough.
    TagLib::FileRef reference(name.constData(), false);
    if (reference.isNull() || reference.tag() == nullptr)
        return Lyrics();

    return Lyrics::parse(tagLyrics(reference.tag()));
}

/// The whole local search, on the worker thread that runs it.
Lyrics resolveLocal(const Track &track)
{
    // The sidecar first: it is the source a user edits by hand, and the only
    // local one that can carry timings without the file being re-tagged.
    const QString sidecar = sidecarText(track);
    if (!sidecar.isEmpty())
    {
        Lyrics lyrics = Lyrics::parse(sidecar);
        if (!lyrics.isEmpty())
        {
            lyrics.source = Lyrics::Source::Sidecar;
            return lyrics;
        }
    }

    Lyrics embedded = embeddedLyrics(track.path);
    if (!embedded.isEmpty())
        embedded.source = Lyrics::Source::Embedded;

    return embedded;
}

/// What LRCLIB is asked with. An empty title or artist means the question
/// cannot be formed, and the API requires both.
QString queryTitle(const Track &track)
{
    if (!track.title.isEmpty())
        return track.title;

    return QFileInfo(track.path).completeBaseName();
}

QString queryArtist(const Track &track)
{
    return track.artist.isEmpty() ? track.albumArtist : track.artist;
}

/// The words of a title, with everything that is the tagger's business taken
/// out -- case, accents, punctuation, spacing -- so that two spellings of the
/// same title compare equal.
QString foldedTitle(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);

    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar &character : decomposed)
    {
        if (character.isLetterOrNumber())
            folded.append(character.toLower());
    }
    return folded;
}

/// The first artist a credit names.
///
/// Cast recordings, compilations and anything tagged from a video's
/// description credit the whole line-up in the artist field ("A, B & Cast of
/// ..."), while LRCLIB files a track under the one artist that released it.
/// The first name in the list is that artist; everything after a separator or
/// a "feat." is the rest of the cast.
QString primaryArtist(const QString &artist)
{
    static const QString separators = QStringLiteral(",;&|");
    static const QRegularExpression credit(QStringLiteral("\\b(?:feat|ft|featuring|with)\\b\\.?"),
                                           QRegularExpression::CaseInsensitiveOption);

    int cut = artist.size();
    for (int i = 0; i < artist.size(); ++i)
    {
        if (separators.contains(artist.at(i)))
        {
            cut = i;
            break;
        }
    }

    const QRegularExpressionMatch match = credit.match(artist);
    if (match.hasMatch() && match.capturedStart() < cut)
        cut = match.capturedStart();

    const QString name = artist.left(cut).trimmed();
    return name.isEmpty() ? artist.trimmed() : name;
}

/// The candidate LRCLIB's fuzzy search offers for `track`, judged on the two
/// things that identify a recording: the words of its title, and its run time.
///
/// The search endpoint answers a couple of dozen candidates for almost any
/// question, so an answer is only taken when the title matches outright and --
/// when both sides know it -- the duration is the track's. Among those, one
/// that names the artist or the album wins, and timed lyrics beat untimed
/// ones, which is what lets the pane follow the playhead. `rawText` receives
/// the lyrics as they arrived, for the index to keep.
Lyrics bestFromSearch(const QByteArray &body, const Track &track, QString *rawText)
{
    const QString wantedTitle = foldedTitle(queryTitle(track));
    const QString wantedAlbum = foldedTitle(track.album);
    const QString wantedArtist = foldedTitle(primaryArtist(queryArtist(track)));

    int bestScore = 0;
    QString bestText;
    bool bestInstrumental = false;

    const QJsonArray candidates = QJsonDocument::fromJson(body).array();
    for (const QJsonValue &value : candidates)
    {
        const QJsonObject candidate = value.toObject();
        if (foldedTitle(candidate.value(QStringLiteral("trackName")).toString()) != wantedTitle)
            continue;

        const double duration = candidate.value(QStringLiteral("duration")).toDouble();
        if (track.duration >= 1.0 && duration >= 1.0
            && std::abs(duration - track.duration) > kDurationTolerance)
        {
            continue;
        }

        const QString synced = candidate.value(QStringLiteral("syncedLyrics")).toString();
        const QString plain = candidate.value(QStringLiteral("plainLyrics")).toString();
        const QString text = synced.trimmed().isEmpty() ? plain : synced;

        // Nothing below is a filter: with the title and the run time already
        // agreed on, these are what pick between the takes that remain. An
        // untimed copy still beats nothing, a timed one is the same words with
        // the moments attached, and an instrumental is worth reporting even
        // with nothing to show.
        const bool instrumental = candidate.value(QStringLiteral("instrumental")).toBool();
        int score = instrumental ? 1 : 0;
        if (!text.trimmed().isEmpty())
            score += 1;
        if (!synced.trimmed().isEmpty())
            score += 2;
        if (!wantedAlbum.isEmpty()
            && foldedTitle(candidate.value(QStringLiteral("albumName")).toString()) == wantedAlbum)
        {
            score += 1;
        }
        if (!wantedArtist.isEmpty()
            && foldedTitle(candidate.value(QStringLiteral("artistName")).toString())
                   .contains(wantedArtist))
        {
            score += 2;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestText = text;
            bestInstrumental = instrumental;
        }
    }

    if (rawText != nullptr)
        *rawText = bestText;

    Lyrics lyrics;
    lyrics.instrumental = bestInstrumental;
    if (bestText.trimmed().isEmpty())
        return lyrics;

    lyrics = Lyrics::parse(bestText);
    lyrics.instrumental = bestInstrumental;
    return lyrics;
}

}  // namespace

LyricsProvider::LyricsProvider(Library *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kRequestGapMs);
    connect(&m_debounce, &QTimer::timeout, this, &LyricsProvider::startOnline);
    connect(&m_local, &QFutureWatcher<Lyrics>::finished, this, &LyricsProvider::onLocalFinished);
}

LyricsProvider::~LyricsProvider()
{
    cancel();
}

void LyricsProvider::setOnlineEnabled(bool enabled)
{
    if (m_online == enabled)
        return;

    m_online = enabled;
    if (!enabled)
        cancel();
}

void LyricsProvider::request(const Track &track, bool force)
{
    if (track.path.isEmpty())
        return;

    m_pending = track;
    m_pendingForce = force;
    m_localPath = track.path;
    // Every lookup starts at the exact ask, whatever the last one reached.
    m_stage = Stage::Exact;

    // A new read supersedes whatever the last one was cleared for.
    m_onlinePath.clear();

    // `m_local` may still be watching an earlier read; replacing the future
    // drops its result, which is exactly what a track change wants. The task
    // itself is a stat and a header read, so it is cheaper to let it finish
    // than to cancel it.
    m_local.setFuture(QtConcurrent::run([track]() -> Lyrics {
        try
        {
            return resolveLocal(track);
        }
        catch (...)
        {
            return Lyrics();  // an unreadable file is not a reason to crash
        }
    }));
}

void LyricsProvider::cancel()
{
    m_debounce.stop();
    m_localPath.clear();
    m_onlinePath.clear();
    m_pending = Track();
    m_stage = Stage::Exact;
    m_fetchingTrack = Track();

    if (m_loading)
    {
        m_loading = false;
        emit loadingChanged(m_fetching, false);
    }

    if (m_reply != nullptr)
    {
        // Disconnected before aborting: aborting emits `finished`, and the
        // reply is being thrown away rather than answered.
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_fetching.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void LyricsProvider::onLocalFinished()
{
    // A queued completion from a read that has already been replaced: the one
    // that matters reports itself when it lands.
    if (!m_local.isFinished())
        return;

    const QString path = m_localPath;
    if (path.isEmpty())
        return;

    const Lyrics lyrics = m_local.result();

    if (!lyrics.isEmpty())
    {
        publish(path, lyrics);
        return;
    }

    const bool forced = m_pendingForce && m_pending.path == path;
    const Library::CachedLyrics cached = m_library->cachedLyrics(path);

    // Only an answer this lookup's own version cached counts: the marker in
    // `source` is what retires what an earlier, blinder lookup remembered, a
    // miss most of all.
    if (cached.found && cached.source == QLatin1String(kCacheSource) && !forced)
    {
        Lyrics remembered;
        if (!cached.text.isEmpty())
        {
            remembered = Lyrics::parse(cached.text);
            remembered.source = Lyrics::Source::Online;
        }
        remembered.instrumental = cached.instrumental;
        publish(path, remembered);
        return;
    }

    // A track the user has already moved away from is not worth a socket; the
    // request for the one they moved to brings its own.
    if (!m_online || m_pending.path != path)
    {
        publish(path, Lyrics());
        return;
    }

    m_onlinePath = path;
    if (!m_loading)
    {
        m_loading = true;
        emit loadingChanged(path, true);
    }

    m_debounce.start();
}

void LyricsProvider::publish(const QString &path, const Lyrics &lyrics)
{
    if (m_loading)
    {
        m_loading = false;
        emit loadingChanged(path, false);
    }

    emit resolved(path, lyrics);
}

void LyricsProvider::startOnline()
{
    // One request at a time, and only for a track whose own file has already
    // been read and had nothing to say.
    if (!m_online || m_reply != nullptr || m_pending.path.isEmpty()
        || m_pending.path != m_onlinePath)
    {
        return;
    }

    // A default QDeadlineTimer never expires, so this is only the wait after a
    // 429 that was asked for.
    if (!m_cooldown.hasExpired())
    {
        m_debounce.start(int(std::max<qint64>(1000, m_cooldown.remainingTime())));
        return;
    }

    const QString title = queryTitle(m_pending);
    const QString artist = queryArtist(m_pending);
    if (title.isEmpty() || artist.isEmpty())
    {
        publish(m_pending.path, Lyrics());
        m_pending = Track();
        return;
    }

    QNetworkRequest request(stageUrl(m_stage, m_pending));
    // The API asks clients to identify themselves; an anonymous client is the
    // one that gets rate limited.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Cadenza " CADENZA_VERSION
                                     " (https://github.com/zxuru/cadenza-player-c)"));
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    if (m_network == nullptr)
        m_network = new QNetworkAccessManager(this);

    m_fetching = m_pending.path;
    m_fetchingStage = m_stage;
    m_fetchingTrack = m_pending;
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &LyricsProvider::onReplyFinished);
}

QUrl LyricsProvider::stageUrl(Stage stage, const Track &track) const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), queryTitle(track));

    if (stage != Stage::Search)
    {
        const QString artist = queryArtist(track);
        query.addQueryItem(QStringLiteral("artist_name"),
                           stage == Stage::Primary ? primaryArtist(artist) : artist);

        // The album is the loosest thing in a tag -- folder names, compilations
        // and "unknown" all end up there -- so only the exact ask carries it.
        if (stage == Stage::Exact && !track.album.isEmpty())
            query.addQueryItem(QStringLiteral("album_name"), track.album);

        // The duration is what keeps a cover or a live version from answering
        // for the studio track; the API allows matching within two seconds.
        if (track.duration >= 1.0 && track.duration <= 3600.0)
            query.addQueryItem(QStringLiteral("duration"), QString::number(qRound(track.duration)));
    }
    // The fuzzy endpoint answers by the words of the title -- an artist it does
    // not recognise empties its answer, so it is given none -- and takes no
    // duration: `bestFromSearch` judges what comes back on both.

    QUrl url(QString::fromLatin1(stage == Stage::Search ? kLrclibSearchEndpoint : kLrclibEndpoint));
    url.setQuery(query);
    return url;
}

void LyricsProvider::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    if (reply == nullptr)
        return;

    m_reply = nullptr;

    const QString path = m_fetching;
    const Stage stage = m_fetchingStage;
    m_fetching.clear();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    Lyrics lyrics;
    QString text;
    bool remember = false;

    // A refusal is what moves the lookup on to its next, looser ask: a 429 is
    // a fact about the service and a timeout about the network, and neither
    // says anything about the track.
    const bool refused = status == 404 || status == 400;
    Stage next = stage == Stage::Exact ? Stage::Primary : Stage::Search;
    if (next == Stage::Primary
        && primaryArtist(queryArtist(m_fetchingTrack)) == queryArtist(m_fetchingTrack)
        && m_fetchingTrack.album.isEmpty())
    {
        // With one artist named and no album, `Primary` would ask the exact
        // same question again.
        next = Stage::Search;
    }

    if (status == 429)
    {
        // "Your client must honor this header": ignoring it earns a ban, so
        // the wait is kept and nothing is remembered about this track.
        bool ok = false;
        const qint64 seconds = reply->rawHeader("Retry-After").toLongLong(&ok);
        m_cooldown.setRemainingTime((ok ? std::max<qint64>(1, seconds) : 30) * 1000 + 500);
    }
    else if (refused && stage != Stage::Search && m_online && m_onlinePath == path
             && m_pending.path == path)
    {
        // Asked again, a little looser, and nothing published in between: the
        // pane keeps saying it is looking.
        m_stage = next;
        m_debounce.start();
        return;
    }
    else if (refused)
    {
        // A miss is remembered -- without it, every play of every instrumental
        // would ask again -- but only once the loosest ask has been refused
        // too. A track the user skipped past mid-lookup is asked in full the
        // next time it plays rather than remembered as having nothing.
        remember = stage == Stage::Search;
    }
    else if (error == QNetworkReply::NoError && status == 200)
    {
        if (stage == Stage::Search)
        {
            lyrics = bestFromSearch(body, m_fetchingTrack, &text);
            lyrics.source = Lyrics::Source::Online;
        }
        else
        {
            const QJsonObject object = QJsonDocument::fromJson(body).object();

            // Synced first: it is the same lyrics with timings attached.
            const QString synced = object.value(QStringLiteral("syncedLyrics")).toString();
            const QString plain = object.value(QStringLiteral("plainLyrics")).toString();
            const bool instrumental = object.value(QStringLiteral("instrumental")).toBool();

            text = !synced.trimmed().isEmpty() ? synced : plain;
            if (!text.trimmed().isEmpty())
            {
                lyrics = Lyrics::parse(text);
                lyrics.source = Lyrics::Source::Online;
            }

            lyrics.instrumental = instrumental;
        }

        remember = true;
    }
    else if (error != QNetworkReply::NoError && status != 200)
    {
        // Offline, timed out, or the service is unwell. Nothing is remembered:
        // this is a fact about the network, not about the track.
    }

    if (remember && !path.isEmpty())
    {
        m_library->saveLyrics(path, QString::fromLatin1(kCacheSource), lyrics.instrumental,
                              lyrics.isEmpty() ? QString() : text);
    }

    if (m_onlinePath == path)
        m_onlinePath.clear();

    publish(path, lyrics);

    // A different track was cleared for a lookup while this one was in flight.
    if (!m_onlinePath.isEmpty())
        m_debounce.start();
}
