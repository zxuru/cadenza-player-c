#pragma once

#include "Library.h"
#include "Lyrics.h"
#include "Track.h"

#include <QDeadlineTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

/// Finds the lyrics of a track, from the cheapest source that has them:
///
///   1. a `.lrc` file beside the audio file -- no index, no network, and an
///      edited file is picked up on the very next track change;
///   2. the file's own tags (`SYLT`, `USLT`, `LYRICS`, `©lyr`) -- one header
///      read through TagLib, which is already a dependency;
///   3. LRCLIB, when online lookups are on, and only for a track that neither
///      of the first two could answer for. The answer -- including "no lyrics
///      for this track" -- is cached in the index, so it is asked once.
///
/// The online step asks twice, because LRCLIB answers in two shapes: `get`
/// matches the track's fields exactly, and `search` matches them loosely. The
/// exact ask is tried first, and only a refusal -- a file whose artist tag
/// spells out every voice in the track, which is what cast recordings and
/// anything tagged by YouTube carry, refuses -- falls through to the fuzzy
/// one, whose candidates are then judged by title and run time.
///
/// The local sources are read on a worker thread: TagLib opens the file, and
/// the GUI thread must not wait on a slow disk or a stale mount. The network
/// step stays here, because a QNetworkAccessManager is not safe to share
/// between threads and there is exactly one lookup in flight at a time.
class LyricsProvider : public QObject
{
    Q_OBJECT

public:
    explicit LyricsProvider(Library *library, QObject *parent = nullptr);
    ~LyricsProvider() override;

    /// Whether LRCLIB may be asked about tracks the local sources cannot
    /// answer for. Off by default: the rest of the player never opens a
    /// socket, and that promise is the user's to keep or drop.
    [[nodiscard]] bool onlineEnabled() const { return m_online; }
    void setOnlineEnabled(bool enabled);

    /// Looks for the lyrics of `track` and reports them through `resolved()`.
    /// `force` asks LRCLIB again about a track it already answered for, which
    /// is what the interface's retry button wants; without it a cached miss is
    /// final.
    void request(const Track &track, bool force = false);

    /// Abandons the lookup in flight. A local read that is already running
    /// cannot be stopped, and its result is dropped by the path check in
    /// `AppController` -- this exists so that quitting does not wait on a
    /// socket.
    void cancel();

signals:
    /// One report per request. Empty lyrics mean "nothing found", which the
    /// interface needs as much as a hit: it is what stops it saying "looking"
    /// and starts it offering the retry.
    void resolved(const QString &path, const Lyrics &lyrics);

    /// True while the online step of `path` is in flight.
    void loadingChanged(const QString &path, bool loading);

private:
    /// Which of LRCLIB's asks the lookup has reached: the exact one, or one of
    /// the two looser ones it falls back to.
    enum class Stage { Exact, Primary, Search };

    void onLocalFinished();
    void publish(const QString &path, const Lyrics &lyrics);
    void startOnline();
    void onReplyFinished();

    /// The ask `stage` makes of LRCLIB for `track`.
    [[nodiscard]] QUrl stageUrl(Stage stage, const Track &track) const;

    Library *m_library = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;

    QFutureWatcher<Lyrics> m_local;
    /// Path the watched local read belongs to.
    QString m_localPath;
    /// Path whose own file was already read and had nothing to say, which is
    /// the only track the network step may run for.
    QString m_onlinePath;

    /// Newest track that still needs an online answer.
    Track m_pending;
    bool m_pendingForce = false;
    /// Path the in-flight reply belongs to, and whether it was a forced ask.
    QString m_fetching;
    /// Ask the in-flight reply answers, and the track it was formed from --
    /// kept per reply, so a track change mid-flight cannot make one stage read
    /// another's answer.
    Stage m_fetchingStage = Stage::Exact;
    Track m_fetchingTrack;
    /// Ask the next request will make.
    Stage m_stage = Stage::Exact;
    /// Spaces out requests: skipping through a queue must not turn into a
    /// burst against a free service.
    QTimer m_debounce;
    /// Set from `Retry-After` after a 429. Held as a deadline rather than an
    /// elapsed time so the wait cannot drift.
    QDeadlineTimer m_cooldown;

    bool m_online = false;
    bool m_loading = false;
};
