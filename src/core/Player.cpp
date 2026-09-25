#include "Player.h"

#include <mpv/client.h>

#include <QByteArray>
#include <QFileInfo>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

/// Converts an mpv value tree into Qt's, recursing through arrays and maps.
/// mpv recycles the node it hands out on the next event, so every string and
/// every entry is copied here instead of being referenced.
[[nodiscard]] QVariant nodeToVariant(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_STRING:
        return node.u.string ? QVariant(QString::fromUtf8(node.u.string)) : QVariant();
    case MPV_FORMAT_FLAG:
        return QVariant(node.u.flag != 0);
    case MPV_FORMAT_INT64:
        return QVariant(static_cast<qlonglong>(node.u.int64));
    case MPV_FORMAT_DOUBLE:
        return QVariant(node.u.double_);
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        const mpv_node_list *entries = node.u.list;
        if (entries) {
            list.reserve(entries->num);
            for (int i = 0; i < entries->num; ++i)
                list.append(nodeToVariant(entries->values[i]));
        }
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        const mpv_node_list *entries = node.u.list;
        if (entries) {
            for (int i = 0; i < entries->num; ++i) {
                if (entries->keys && entries->keys[i]) {
                    map.insert(QString::fromUtf8(entries->keys[i]),
                               nodeToVariant(entries->values[i]));
                }
            }
        }
        return map;
    }
    case MPV_FORMAT_NONE:
    default:
        return QVariant();
    }
}

/// Reads a metadata entry as text. Tags are not always strings -- some files
/// carry a year or a track number where a name belongs -- so anything
/// printable is stringified.
[[nodiscard]] QString mapText(const QVariantMap &map, const char *key)
{
    return map.value(QString::fromLatin1(key)).toString();
}

/// mpv leaves `artist` out for albums tagged only with an album artist.
[[nodiscard]] QString metadataArtist(const QVariantMap &metadata)
{
    const QString artist = mapText(metadata, "artist");
    return artist.isEmpty() ? mapText(metadata, "album_artist") : artist;
}

/// True when `loadfile` hands the insertion index to its third argument and
/// the file's own options to the fourth, which is the layout mpv 0.38 gave it.
/// The release before that one took the options as the third argument and had
/// no fourth, and handing either version the other's layout does not play the
/// file at all -- so the library in front of us is asked, not assumed.
[[nodiscard]] bool loadfileTakesIndex(const QString &version)
{
    static const QRegularExpression release(QStringLiteral(R"((\d+)\.(\d+))"));
    const QRegularExpressionMatch match = release.match(version);
    if (!match.hasMatch())
        return false;

    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    return major > 0 || minor >= 38;
}

/// Sample rates are written the way audio hardware lists them: trailing zeros
/// are dropped, so 48000 Hz reads "48 kHz" while 44100 Hz keeps the ".1" that
/// is part of the rate and reads "44.1 kHz".
[[nodiscard]] QString formatSampleRate(int rate)
{
    static constexpr int scales[] = {1, 10, 100};

    const int divisor = rate >= 1000000 ? 1000000 : 1000;
    const QString suffix = divisor == 1000000 ? QStringLiteral(" MHz")
                                              : QStringLiteral(" kHz");

    int decimals = 3;
    for (int candidate = 0; candidate < 3; ++candidate) {
        if (rate % (divisor / scales[candidate]) == 0) {
            decimals = candidate;
            break;
        }
    }

    return QString::number(rate / static_cast<double>(divisor), 'f', decimals) + suffix;
}

} // namespace

Player::Player(QObject *parent)
    : QObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv)
        return;

    struct Option
    {
        const char *name;
        const char *value;
    };

    // vo=null together with vid=no keeps mpv from ever opening a window:
    // decoding and device output are all this player wants from it.
    // gapless-audio=yes is what makes consecutive album tracks run together
    // instead of through the silence an end-of-file -> play cycle would insert
    // between them.
    static constexpr Option options[] = {
        {"vo", "null"},
        {"vid", "no"},
        {"audio-display", "no"},
        {"force-window", "no"},
        {"idle", "yes"},
        {"gapless-audio", "yes"},
        {"keep-open", "no"},
        {"terminal", "no"},
        {"input-default-bindings", "no"},
        {"input-vo-keyboard", "no"},
        {"osc", "no"},
        {"ytdl", "no"},
        {"audio-pitch-correction", "yes"},
        {"volume-max", "100"},
        {"replaygain", "no"},
        {"audio-exclusive", "no"},
        {"msg-level", "all=no"},
        {"config", "no"},
    };

    for (const Option &option : options)
        mpv_set_option_string(m_mpv, option.name, option.value);

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "path", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "playlist-pos", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "replaygain", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "audio-device", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "audio-exclusive", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "metadata", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "audio-params", MPV_FORMAT_NODE);

    // Read once, before anything is loaded: what `loadfile` takes is a
    // property of the library, not of the call.
    m_loadfileTakesIndex = loadfileTakesIndex(stringProperty("mpv-version"));

    startEventLoop();
}

Player::~Player()
{
    if (!m_mpv)
        return;

    m_stopping = true;
    mpv_wakeup(m_mpv);
    // The join has to come before the destroy: until then the event thread is
    // still inside mpv_wait_event, and tearing the handle down underneath it
    // would leave it reading freed memory.
    if (m_eventThread.joinable())
        m_eventThread.join();
    mpv_terminate_destroy(m_mpv);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Player::setVolume(int volume)
{
    // mpv's scale is its own 0..100, not the 0..1 the Python version used.
    // mpv reports the change back through the property event, which is where
    // volumeChanged() comes from, so the value is not stored twice.
    setDouble("volume", std::clamp(volume, 0, 100));
}

void Player::setMuted(bool muted)
{
    setFlag("mute", muted);
}

void Player::setReplayGain(const QString &mode)
{
    // mpv knows exactly these three values; anything else would be rejected,
    // so an unknown mode falls back to the off state.
    static const QStringList modes{
        QStringLiteral("no"),
        QStringLiteral("track"),
        QStringLiteral("album"),
    };

    if (!m_mpv)
        return;

    const QByteArray value = (modes.contains(mode) ? mode : QStringLiteral("no")).toUtf8();
    mpv_set_property_string(m_mpv, "replaygain", value.constData());
}

void Player::setAudioDevice(const QString &device)
{
    if (!m_mpv)
        return;

    const QByteArray name = device.toUtf8();
    mpv_set_property_string(m_mpv, "audio-device", name.constData());
}

void Player::setExclusive(bool exclusive)
{
    // Exclusive mode is the bit-perfect path: WASAPI, CoreAudio and PipeWire
    // hand the decoder's samples straight to the device, bypassing the system
    // mixer. Outputs that have no notion of it ignore the flag.
    setFlag("audio-exclusive", exclusive);
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void Player::load(const QStringList &paths, int start, double resumeAt)
{
    if (!m_mpv)
        return;

    // mpv's own playlist is the queue: it is what gapless playback consumes,
    // and the copy kept here is only the order, so the two cannot say different
    // things.
    if (paths.isEmpty()) {
        m_queue.clear();
        command(QStringList{QStringLiteral("playlist-clear")});
        stop();
        return;
    }

    // `playlist-clear` keeps whatever is playing, which would leave the old
    // track at index 0 and push every new one down a place -- the queue would
    // then start on the track that was already on and every index after it
    // would be off by one. Replacing the list with the first file is what
    // drops the old queue for good. The pause flag is set first so that the
    // first file, which mpv starts as soon as it has it, is not heard before
    // the one that was actually asked for.
    setFlag("pause", true);

    // Where to start in the file that was already playing, when the queue is
    // being rebuilt under it. The `+` makes the time relative to the file's
    // start wherever mpv is told not to rebase the start time; the option is
    // dropped rather than guessed at on a library that would read it as the
    // insertion index.
    const QString resume = resumeAt > 0.0 && m_loadfileTakesIndex
                           ? QStringLiteral("start=+%1").arg(resumeAt, 0, 'f', 3)
                           : QString();

    // The queue the tail reorder works from, set before the files are handed
    // over so that a reorder arriving with them already finds it.
    m_queue = paths;

    for (int index = 0; index < paths.size(); ++index) {
        QStringList args{QStringLiteral("loadfile"), paths.at(index),
                         index == 0 ? QStringLiteral("replace") : QStringLiteral("append")};
        if (index == start && !resume.isEmpty())
            args << QStringLiteral("-1") << resume;
        command(args);
    }

    if (start >= 0 && start < paths.size())
        command(QStringList{QStringLiteral("playlist-play-index"), QString::number(start)});

    setFlag("pause", false);
}

bool Player::reorderTail(const QStringList &pool, bool randomise)
{
    // Nothing is playing: there is no playhead to keep where it is, and the
    // order of what comes next is decided by `load`.
    if (!m_mpv || m_queue.isEmpty() || m_index < 0 || m_index >= m_queue.size())
        return false;

    // Everything the playhead has passed stays where it is. The tail is the
    // pool's own entries minus those, which drops both what has already been
    // heard and whatever the pool does not name -- a queue built from another
    // list.
    const QSet<QString> passed(m_queue.cbegin(), m_queue.cbegin() + m_index + 1);

    QStringList tail;
    tail.reserve(pool.size());
    for (const QString &path : pool) {
        if (!passed.contains(path))
            tail.append(path);
    }

    if (randomise) {
        // Fisher-Yates: every order the pool can be dealt in is equally likely.
        for (int index = tail.size() - 1; index > 0; --index)
            tail.swapItemsAt(index, QRandomGenerator::global()->bounded(index + 1));
    }

    if (tail == m_queue.mid(m_index + 1))
        return false;

    // Removed from the end backwards, so the entries still to be dropped keep
    // the indexes they were found at, and the one that is playing is never
    // among them.
    for (int index = m_queue.size() - 1; index > m_index; --index)
        commandAsync(QStringList{QStringLiteral("playlist-remove"), QString::number(index)});

    for (const QString &path : std::as_const(tail)) {
        commandAsync(QStringList{QStringLiteral("loadfile"), path,
                                 QStringLiteral("append")});
    }

    m_queue = m_queue.mid(0, m_index + 1) + tail;
    return true;
}

void Player::play()
{
    if (m_index < 0)
        return;
    setFlag("pause", false);
}

void Player::pause()
{
    // Nothing to guard on: with no file loaded the flag has no audible effect,
    // and load() clears it before starting playback. Guarding on m_index here
    // would instead swallow a pause pressed right after a load, before the
    // playlist-pos event has come back.
    setFlag("pause", true);
}

void Player::toggle()
{
    if (playing())
        pause();
    else
        play();
}

void Player::stop()
{
    command(QStringList{QStringLiteral("stop")});
}

void Player::next()
{
    // `weak` turns the end of the queue into a no-op rather than an error,
    // which is what the Python version does by simply not advancing past the
    // last track.
    command(QStringList{QStringLiteral("playlist-next"), QStringLiteral("weak")});
}

void Player::previous()
{
    if (m_index < 0)
        return;

    // Stepping back mid-track should restart it; only within the first three
    // seconds does it mean the previous track.
    if (m_position > 3.0 || m_index <= 0)
        restart();
    else
        command(QStringList{QStringLiteral("playlist-prev"), QStringLiteral("weak")});
}

void Player::restart()
{
    seek(0.0);
}

void Player::seek(double seconds)
{
    command(QStringList{QStringLiteral("seek"),
                        QString::number(std::max(0.0, seconds), 'f', 3),
                        QStringLiteral("absolute+exact")});
}

void Player::seekBy(double seconds)
{
    // No clamping here: a negative delta is how the interface rewinds.
    command(QStringList{QStringLiteral("seek"),
                        QString::number(seconds, 'f', 3),
                        QStringLiteral("relative+exact")});
}

QVariantList Player::audioDevices() const
{
    QVariantList devices;
    if (!m_mpv)
        return devices;

    mpv_node node;
    if (mpv_get_property(m_mpv, "audio-device-list", MPV_FORMAT_NODE, &node) < 0)
        return devices;

    const QVariantList entries = nodeToVariant(node).toList();
    mpv_free_node_contents(&node);

    devices.reserve(entries.size());
    for (const QVariant &entry : entries) {
        const QVariantMap source = entry.toMap();
        QVariantMap device;
        device.insert(QStringLiteral("name"),
                      source.value(QStringLiteral("name")).toString());
        device.insert(QStringLiteral("description"),
                      source.value(QStringLiteral("description")).toString());
        devices.append(device);
    }
    return devices;
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------

void Player::startEventLoop()
{
    m_eventThread = std::thread([this] {
        for (;;) {
            // The half-second timeout is deliberate: it is how the loop
            // notices m_stopping without a wakeup having to win a race.
            mpv_event *event = mpv_wait_event(m_mpv, 0.5);
            if (m_stopping)
                break;
            if (event->event_id == MPV_EVENT_NONE)
                continue;
            handleEvent(event);
        }
    });
}

void Player::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *property = static_cast<const mpv_event_property *>(event->data);
        if (!property || !property->name)
            break;

        const QString name = QString::fromLatin1(property->name);
        QVariant value;

        switch (property->format) {
        case MPV_FORMAT_DOUBLE:
            value = QVariant(*static_cast<const double *>(property->data));
            break;
        case MPV_FORMAT_FLAG:
            // The payload of a flag event is an int, not a bool.
            value = QVariant(*static_cast<const int *>(property->data) != 0);
            break;
        case MPV_FORMAT_INT64:
            value = QVariant(static_cast<qlonglong>(*static_cast<const int64_t *>(property->data)));
            break;
        case MPV_FORMAT_STRING: {
            const char *text = *static_cast<const char *const *>(property->data);
            value = text ? QVariant(QString::fromUtf8(text)) : QVariant(QString());
            break;
        }
        case MPV_FORMAT_NODE:
            if (property->data)
                value = nodeToVariant(*static_cast<const mpv_node *>(property->data));
            break;
        case MPV_FORMAT_NONE:
        default:
            // An unavailable property -- nothing loaded, no decoder output yet
            // -- arrives as MPV_FORMAT_NONE with no payload at all, which the
            // empty QVariant left here stands for.
            break;
        }

        // The event and everything it points at are recycled by the next
        // mpv_wait_event, so only the copies made above cross the thread
        // boundary.
        QMetaObject::invokeMethod(this, [this, name, value] {
            // Only the signal whose value actually changed is emitted: QML
            // re-evaluates bindings on every emission, and mpv reports
            // time-pos four times a second.
            if (name == QLatin1String("time-pos")) {
                const double position = value.toDouble();
                if (position != m_position) {
                    m_position = position;
                    emit positionChanged();
                }
            } else if (name == QLatin1String("duration")) {
                const double duration = value.toDouble();
                if (duration != m_duration) {
                    m_duration = duration;
                    emit durationChanged();
                }
            } else if (name == QLatin1String("pause")) {
                const bool paused = value.toBool();
                if (paused != m_paused) {
                    m_paused = paused;
                    emit playingChanged();
                }
            } else if (name == QLatin1String("idle-active")) {
                const bool idle = value.toBool();
                if (idle != m_idle) {
                    m_idle = idle;
                    emit idleChanged();
                    // playing() is a function of both flags.
                    emit playingChanged();
                }
            } else if (name == QLatin1String("path")) {
                const QString path = value.toString();
                if (path != m_path) {
                    m_path = path;
                    emit pathChanged();
                    updateDisplayTitle();
                }
            } else if (name == QLatin1String("playlist-pos")) {
                // An emptied playlist reports the property as unavailable,
                // which means "no current entry" rather than entry 0.
                const int index = value.isValid() ? value.toInt() : -1;
                if (index != m_index) {
                    m_index = index;
                    emit indexChanged();
                }
            } else if (name == QLatin1String("volume")) {
                const int volume = qRound(value.toDouble());
                if (volume != m_volume) {
                    m_volume = volume;
                    emit volumeChanged();
                }
            } else if (name == QLatin1String("mute")) {
                const bool muted = value.toBool();
                if (muted != m_muted) {
                    m_muted = muted;
                    emit mutedChanged();
                }
            } else if (name == QLatin1String("replaygain")) {
                const QString mode = value.toString();
                if (mode != m_replayGain) {
                    m_replayGain = mode;
                    emit replayGainChanged();
                }
            } else if (name == QLatin1String("audio-device")) {
                const QString device = value.toString();
                if (device != m_audioDevice) {
                    m_audioDevice = device;
                    emit audioDeviceChanged();
                }
            } else if (name == QLatin1String("audio-exclusive")) {
                const bool exclusive = value.toBool();
                if (exclusive != m_exclusive) {
                    m_exclusive = exclusive;
                    emit exclusiveChanged();
                }
            } else if (name == QLatin1String("metadata")) {
                const QVariantMap metadata = value.toMap();
                setMetadata(mapText(metadata, "title"),
                            metadataArtist(metadata),
                            mapText(metadata, "album"));
            } else if (name == QLatin1String("audio-params")) {
                readAudioParams();
            }
        }, Qt::QueuedConnection);
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        QMetaObject::invokeMethod(this, [this] { onFileLoaded(); }, Qt::QueuedConnection);
        break;
    case MPV_EVENT_END_FILE: {
        const auto *endFile = static_cast<const mpv_event_end_file *>(event->data);
        if (!endFile)
            break;

        if (endFile->reason == MPV_END_FILE_REASON_EOF) {
            QMetaObject::invokeMethod(this, [this] { emit finished(); }, Qt::QueuedConnection);
        } else if (endFile->reason == MPV_END_FILE_REASON_ERROR) {
            const char *reason = mpv_error_string(endFile->error);
            const QString message = reason ? QString::fromUtf8(reason)
                                           : QStringLiteral("Playback failed");
            QMetaObject::invokeMethod(this, [this, message] {
                setError(message);
                emit failed(message);
            }, Qt::QueuedConnection);
        }
        // STOP, QUIT and REDIRECT are deliberate, so they stay silent: a stop
        // is not a failure.
        break;
    }
    case MPV_EVENT_SHUTDOWN:
        // The player is going away, so the loop has nothing left to wait for.
        m_stopping = true;
        break;
    default:
        break;
    }
}

void Player::onFileLoaded()
{
    if (!m_mpv)
        return;

    // A file that loaded cleanly clears whatever the previous one reported.
    setError(QString());

    // The names are read here as well as from the metadata event: a tag change
    // can land in the same tick as the unload, and the screen should never keep
    // the previous track's title.
    mpv_node node;
    if (mpv_get_property(m_mpv, "metadata", MPV_FORMAT_NODE, &node) >= 0) {
        const QVariantMap metadata = nodeToVariant(node).toMap();
        mpv_free_node_contents(&node);
        setMetadata(mapText(metadata, "title"),
                    metadataArtist(metadata),
                    mapText(metadata, "album"));
    }

    readAudioParams();
}

void Player::readAudioParams()
{
    if (!m_mpv)
        return;

    int sampleRate = 0;
    int channelCount = 0;
    QString layout;

    mpv_node node;
    if (mpv_get_property(m_mpv, "audio-params", MPV_FORMAT_NODE, &node) >= 0) {
        const QVariantMap params = nodeToVariant(node).toMap();
        mpv_free_node_contents(&node);
        sampleRate = params.value(QStringLiteral("samplerate")).toInt();
        channelCount = params.value(QStringLiteral("channel-count")).toInt();
        layout = params.value(QStringLiteral("hr-channels")).toString();
    }

    // Without a sample rate there is no decoder output to describe yet, so the
    // line stays empty rather than half filled.
    QString info;
    if (sampleRate > 0) {
        QStringList parts;

        const QString codec = stringProperty("current-tracks/audio/codec").toUpper();
        if (!codec.isEmpty())
            parts.append(codec);

        parts.append(formatSampleRate(sampleRate));

        // "stereo" says more than "2 ch", so the layout wins when mpv has one.
        if (!layout.isEmpty())
            parts.append(layout);
        else if (channelCount > 0)
            parts.append(QString::number(channelCount) + QStringLiteral(" ch"));

        info = parts.join(QStringLiteral(" · "));
    }

    if (info == m_streamInfo)
        return;
    m_streamInfo = info;
    emit streamInfoChanged();
}

// ---------------------------------------------------------------------------
// libmpv helpers
// ---------------------------------------------------------------------------

void Player::command(const char *first, ...) const
{
    if (!first)
        return;

    QStringList args{QString::fromLatin1(first)};

    va_list arguments;
    va_start(arguments, first);
    for (;;) {
        const char *argument = va_arg(arguments, const char *);
        if (!argument)
            break;
        args.append(QString::fromLatin1(argument));
    }
    va_end(arguments);

    command(args);
}

void Player::command(const QStringList &args) const
{
    if (!m_mpv || args.isEmpty())
        return;

    std::vector<QByteArray> storage;
    storage.reserve(args.size());
    for (const QString &arg : args)
        storage.push_back(arg.toUtf8());

    // Both vectors have to outlive the call: mpv_command only borrows the
    // pointers, but the bytes they point into have to stay put for as long as
    // it reads them.
    std::vector<const char *> argv;
    argv.reserve(storage.size() + 1);
    for (const QByteArray &arg : storage)
        argv.push_back(arg.constData());
    argv.push_back(nullptr);

    mpv_command(m_mpv, argv.data());
}

void Player::commandAsync(const QStringList &args) const
{
    if (!m_mpv || args.isEmpty())
        return;

    std::vector<QByteArray> storage;
    storage.reserve(args.size());
    for (const QString &arg : args)
        storage.push_back(arg.toUtf8());

    std::vector<const char *> argv;
    argv.reserve(storage.size() + 1);
    for (const QByteArray &arg : storage)
        argv.push_back(arg.constData());
    argv.push_back(nullptr);

    // mpv parses the command before queueing it, so these buffers only have to
    // outlive the call itself, and what it runs is run in the order it arrived.
    mpv_command_async(m_mpv, 0, argv.data());
}

void Player::setFlag(const char *name, bool value) const
{
    if (!m_mpv)
        return;

    // Flags travel as an int through this API, not as a bool.
    int flag = value ? 1 : 0;
    mpv_set_property(m_mpv, name, MPV_FORMAT_FLAG, &flag);
}

void Player::setDouble(const char *name, double value) const
{
    if (!m_mpv)
        return;

    mpv_set_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value);
}

QString Player::stringProperty(const char *name) const
{
    if (!m_mpv)
        return QString();

    char *value = mpv_get_property_string(m_mpv, name);
    if (!value)
        return QString();

    const QString text = QString::fromUtf8(value);
    mpv_free(value);
    return text;
}

double Player::doubleProperty(const char *name) const
{
    if (!m_mpv)
        return 0.0;

    double value = 0.0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value) < 0)
        return 0.0;
    return value;
}

bool Player::flagProperty(const char *name) const
{
    if (!m_mpv)
        return false;

    int value = 0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_FLAG, &value) < 0)
        return false;
    return value != 0;
}

int64_t Player::intProperty(const char *name) const
{
    if (!m_mpv)
        return -1;

    int64_t value = 0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_INT64, &value) < 0)
        return -1;
    return value;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void Player::setError(const QString &message)
{
    if (message == m_error)
        return;

    m_error = message;
    emit errorChanged();
}

void Player::setMetadata(const QString &title, const QString &artist, const QString &album)
{
    if (title == m_title && artist == m_artist && album == m_album)
        return;

    m_title = title;
    m_artist = artist;
    m_album = album;
    emit metadataChanged();
    updateDisplayTitle();
}

void Player::updateDisplayTitle()
{
    // The same rule the library names an untagged file by: its own name,
    // without the extension.
    const QString name = m_title.isEmpty() ? QFileInfo(m_path).completeBaseName() : m_title;
    if (name == m_displayTitle)
        return;

    m_displayTitle = name;
    emit displayTitleChanged();
}
