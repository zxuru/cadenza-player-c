#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>
#include <thread>

struct mpv_handle;
struct mpv_event;

/// Playback engine: a thin, opinionated wrapper around one libmpv handle.
///
/// libmpv does the decoding (via FFmpeg), the device output (WASAPI on
/// Windows, CoreAudio on macOS, ALSA/PulseAudio/PipeWire on Linux) and the
/// queue, so this class owns no audio code of its own. What it adds is:
///
///   * a gapless queue built from `playlist-*` commands, so consecutive album
///     tracks run together instead of through a silence gap;
///   * ReplayGain applied by the decoder rather than by hand;
///   * every mpv property the interface needs, surfaced as Qt properties.
///
/// Threading: mpv delivers events on its own thread. That thread never touches
/// this object directly -- it marshals each change onto the GUI thread with a
/// queued invocation, so QML bindings are always updated from one thread.
class Player : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY playingChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int index READ index NOTIFY indexChanged)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title NOTIFY metadataChanged)
    /// What the interface calls the track that is loaded: its title, or the
    /// name of the file when its tags carry none. Read by both places that
    /// name what is playing, so the transport and the hero cannot disagree.
    Q_PROPERTY(QString displayTitle READ displayTitle NOTIFY displayTitleChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY metadataChanged)
    Q_PROPERTY(QString album READ album NOTIFY metadataChanged)
    Q_PROPERTY(QString streamInfo READ streamInfo NOTIFY streamInfoChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString replayGain READ replayGain WRITE setReplayGain NOTIFY replayGainChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(bool exclusive READ exclusive WRITE setExclusive NOTIFY exclusiveChanged)

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    /// False when libmpv could not be created -- the interface reports it and
    /// disables the transport rather than pretending to play.
    [[nodiscard]] bool isAvailable() const { return m_mpv != nullptr; }

    [[nodiscard]] bool playing() const { return !m_paused && !m_idle; }
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] bool idle() const { return m_idle; }
    [[nodiscard]] double position() const { return m_position; }
    [[nodiscard]] double duration() const { return m_duration; }
    [[nodiscard]] int volume() const { return m_volume; }
    [[nodiscard]] bool muted() const { return m_muted; }
    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString title() const { return m_title; }
    [[nodiscard]] QString displayTitle() const { return m_displayTitle; }
    [[nodiscard]] QString artist() const { return m_artist; }
    [[nodiscard]] QString album() const { return m_album; }
    [[nodiscard]] QString streamInfo() const { return m_streamInfo; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString replayGain() const { return m_replayGain; }
    [[nodiscard]] QString audioDevice() const { return m_audioDevice; }
    [[nodiscard]] bool exclusive() const { return m_exclusive; }

    void setVolume(int volume);
    void setMuted(bool muted);
    void setReplayGain(const QString &mode);
    void setAudioDevice(const QString &device);
    void setExclusive(bool exclusive);

    /// Replaces the queue with `paths` and starts at `start`.
    /// An empty list clears the queue.
    ///
    /// `resumeAt` is how far into that file to start, in seconds, and is there
    /// for a queue that is being rebuilt under a file that is already playing:
    /// the paths changed, the recording did not. It is applied by mpv as a
    /// per-file option, so playback starts at that point rather than running
    /// from the beginning to it.
    void load(const QStringList &paths, int start = 0, double resumeAt = 0.0);

    /// The order the queue is in now, which is what `load` was handed plus
    /// whatever `reorderTail` has done to it since.
    [[nodiscard]] QStringList queue() const { return m_queue; }

    /// Replaces what the queue holds after the entry that is playing with the
    /// entries of `pool` the playhead has not passed: in `pool`'s own order, or
    /// in a random one, which is what the shuffle modes ask for. Whatever is
    /// playing keeps playing -- the change is heard from the next track on --
    /// and the entries behind it stay where they are, so stepping back still
    /// walks what has been heard. Returns whether the queue changed at all,
    /// which is false when nothing is playing and there is nothing to reorder.
    Q_INVOKABLE bool reorderTail(const QStringList &pool, bool randomise);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void stop();

    /// Steps to the next queue entry. At the end of the queue this stops,
    /// matching the Python implementation rather than wrapping around.
    Q_INVOKABLE void next();

    /// Restarts the current track, unless we are within three seconds of the
    /// start -- then it steps back one, which is what listeners expect.
    Q_INVOKABLE void previous();

    /// Restarts the current track from zero.
    Q_INVOKABLE void restart();

    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekBy(double seconds);

    /// Output devices libmpv knows about, as `{name, description}` maps.
    Q_INVOKABLE QVariantList audioDevices() const;

signals:
    void playingChanged();
    void idleChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void indexChanged();
    void pathChanged();
    void metadataChanged();
    void displayTitleChanged();
    void streamInfoChanged();
    void errorChanged();
    void replayGainChanged();
    void audioDeviceChanged();
    void exclusiveChanged();

    /// The queue ran off its end.
    void finished();

    /// A file failed to load. `message` is already human-readable.
    void failed(const QString &message);

private:
    void startEventLoop();
    void handleEvent(mpv_event *event);
    void onFileLoaded();
    void readAudioParams();

    /// Runs `mpv_command` with a null-terminated argument list.
    void command(const char *first, ...) const;
    void command(const QStringList &args) const;

    /// Queues the command without waiting for it, for the calls that arrive in
    /// bulk -- a queue can run to the size of the library. mpv runs what it is
    /// given in order either way, so a blocking call issued after a run of
    /// these still finds all of them done.
    void commandAsync(const QStringList &args) const;
    void setFlag(const char *name, bool value) const;
    void setDouble(const char *name, double value) const;
    [[nodiscard]] QString stringProperty(const char *name) const;
    [[nodiscard]] double doubleProperty(const char *name) const;
    [[nodiscard]] bool flagProperty(const char *name) const;
    [[nodiscard]] int64_t intProperty(const char *name) const;

    void setError(const QString &message);
    void setMetadata(const QString &title, const QString &artist, const QString &album);

    /// Recomputed whenever the path or the tags change, so the name the
    /// interface has for what is playing is never stale.
    void updateDisplayTitle();

    mpv_handle *m_mpv = nullptr;
    std::thread m_eventThread;
    std::atomic_bool m_stopping{false};

    /// What this build's `loadfile` takes as its arguments, read from mpv at
    /// startup: false on the releases whose third argument is the file's
    /// options and which have no fourth.
    bool m_loadfileTakesIndex = false;

    bool m_paused = false;
    bool m_idle = true;
    double m_position = 0.0;
    double m_duration = 0.0;
    int m_volume = 100;
    bool m_muted = false;
    int m_index = -1;
    /// The queue in the order mpv is holding it. mpv owns the playlist itself;
    /// this is the copy `reorderTail` needs to know what the playhead has
    /// passed, kept in step with every load and reorder.
    QStringList m_queue;
    QString m_path;
    QString m_title;
    QString m_displayTitle;
    QString m_artist;
    QString m_album;
    QString m_streamInfo;
    QString m_error;
    QString m_replayGain = QStringLiteral("no");
    QString m_audioDevice;
    bool m_exclusive = false;
};
