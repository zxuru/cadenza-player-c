#pragma once

#include <QString>
#include <QVector>

/// One line of lyrics, and -- when the source timed them -- the moment it
/// starts.
struct LyricLine
{
    /// Seconds from the start of the track. Negative for unsynced lyrics,
    /// which carry no timing at all.
    double time = -1.0;
    QString text;
};

/// The lyrics of one track, however they were found, plus enough context for
/// the interface to say where they came from.
class Lyrics
{
public:
    enum class Source {
        None,      ///< Nothing looked for yet, or nothing found.
        Sidecar,   ///< A `.lrc` or `.txt` beside the audio file.
        Embedded,  ///< A tag inside the audio file itself.
        Online,    ///< LRCLIB, cached in the index so it is fetched once.
    };

    QVector<LyricLine> lines;

    /// True when `lines` carry timestamps that can follow playback.
    bool synced = false;

    /// The track has no vocals. Only LRCLIB reports this, and it is worth
    /// showing: it explains an empty pane instead of looking like a miss.
    bool instrumental = false;

    Source source = Source::None;

    [[nodiscard]] bool isEmpty() const { return lines.isEmpty(); }

    /// Parses LRC: `[mm:ss.xx] text`, several timestamps on one line, tags out
    /// of order, `[offset:±ms]`, and the inline `<mm:ss.xx>` word timings some
    /// taggers add, which a line-level view strips.
    ///
    /// Bracketed runs that do not parse as a time are metadata -- `[ar:...]`,
    /// `[ti:...]`, `[length:...]` -- and are dropped. Text with no timestamp
    /// anywhere comes back untimed, so a `.lrc` that is really plain text
    /// still reads.
    [[nodiscard]] static Lyrics parseLrc(const QString &text);

    /// One line per line, no timing: `USLT` frames, `UNSYNCEDLYRICS` comments,
    /// `.txt` files and LRCLIB's `plainLyrics`.
    [[nodiscard]] static Lyrics parsePlain(const QString &text);

    /// `parseLrc` when `text` carries at least one timestamp, `parsePlain`
    /// otherwise, so the caller never has to know which it was handed.
    [[nodiscard]] static Lyrics parse(const QString &text);

    /// The line that should be showing at `position` seconds, or -1 for
    /// unsynced lyrics and for the intro before the first line.
    [[nodiscard]] int indexAt(double position) const;
};
