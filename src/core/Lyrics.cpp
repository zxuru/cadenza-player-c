#include "Lyrics.h"

#include <QChar>
#include <QStringView>
#include <QVarLengthArray>

#include <algorithm>
#include <utility>

namespace {

/// The characters that separate the parts of a timestamp: `mm:ss.xx`, and the
/// `mm:ss:cc` spelling that some taggers write instead.
bool isSeparator(QChar character)
{
    return character == QLatin1Char(':') || character == QLatin1Char('.')
        || character == QLatin1Char(',');
}

/// `mm:ss.xx` into seconds, or -1 when `body` is not a timestamp at all.
///
/// That -1 is the whole of LRC's grammar: a bracketed run beginning with a
/// digit is a time, anything else is metadata (`[ar:...]`, `[offset:...]`,
/// `[Chorus]`), and the caller tells them apart by this result alone.
double parseTimestamp(QStringView body)
{
    double parts[3] = {0.0, 0.0, 0.0};
    int part = 0;
    int digits = 0;
    bool anyDigit = false;

    for (const QChar character : body)
    {
        if (character.isDigit())
        {
            parts[part] = parts[part] * 10.0 + (character.unicode() - u'0');
            ++digits;
            anyDigit = true;
            continue;
        }

        if (isSeparator(character))
        {
            // `[00:]`, `[00::12]` and anything past `mm:ss:cc` are not times.
            if (digits == 0 || part == 2)
                return -1.0;

            ++part;
            digits = 0;
            continue;
        }

        return -1.0;
    }

    // A bare number is a line number in some karaoke exports, never a time.
    if (!anyDigit || part == 0)
        return -1.0;

    // A third part counts digits rather than units, so `12`, `120` and `1200`
    // all mean the same fraction of a second. That is how every writer of LRC
    // means it, and it is why `[00:12.5]` is 12.5 seconds and not 12.05.
    double fraction = parts[2];
    for (int digit = 0; part == 2 && digit < digits; ++digit)
        fraction /= 10.0;

    return parts[0] * 60.0 + parts[1] + fraction;
}

/// `[offset:±ms]` into `value`. The offset is the one piece of metadata worth
/// honouring: it is a deliberate correction applied to the whole file.
bool parseOffset(QStringView body, double *value)
{
    constexpr auto kTag = QLatin1StringView("offset:");

    if (!body.startsWith(kTag, Qt::CaseInsensitive))
        return false;

    bool ok = false;
    const int milliseconds = body.mid(kTag.size()).trimmed().toInt(&ok);

    if (ok)
        *value = milliseconds;

    return ok;
}

/// The words of a line, with the inline `<mm:ss.xx>` tags removed. Those time
/// the syllables inside a line; a line-level view has no use for them, and
/// leaving them in would print them.
QString lineText(QStringView body)
{
    QString text;
    text.reserve(body.size());

    for (int index = 0; index < body.size(); ++index)
    {
        const QChar character = body.at(index);

        if (character == QLatin1Char('<'))
        {
            const int close = body.indexOf(QLatin1Char('>'), index + 1);
            if (close > index && parseTimestamp(body.mid(index + 1, close - index - 1)) >= 0.0)
            {
                index = close;
                continue;
            }
        }

        text.append(character);
    }

    return text.trimmed();
}

}  // namespace

Lyrics Lyrics::parseLrc(const QString &text)
{
    Lyrics lyrics;

    QVector<LyricLine> timed;
    QVector<QString> untimed;
    double offset = 0.0;

    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &raw : rawLines)
    {
        // `trimmed()` also takes the `\r` off a CRLF file.
        const QStringView line = QStringView(raw).trimmed();
        if (line.isEmpty())
            continue;

        QVarLengthArray<double, 4> stamps;
        int cursor = 0;

        while (cursor < line.size() && line.at(cursor) == QLatin1Char('['))
        {
            const int close = line.indexOf(QLatin1Char(']'), cursor + 1);
            if (close < 0)
                break;

            const QStringView body = line.mid(cursor + 1, close - cursor - 1);
            const double stamp = parseTimestamp(body);

            if (stamp >= 0.0)
                stamps.append(stamp);
            else
                parseOffset(body, &offset);

            cursor = close + 1;
        }

        const QString words = lineText(line.mid(cursor));
        if (words.isEmpty())
            continue;

        // Several timestamps may share one line; that is a chorus repeated, so
        // the words are stored once per time.
        for (const double stamp : stamps)
            timed.append({stamp, words});

        if (stamps.isEmpty())
            untimed.append(words);
    }

    if (!timed.isEmpty())
    {
        for (LyricLine &line : timed)
            line.time = std::max(0.0, line.time - offset / 1000.0);

        // Files are normally in order, but nothing in the format says they are.
        std::stable_sort(timed.begin(), timed.end(),
                         [](const LyricLine &left, const LyricLine &right) {
                             return left.time < right.time;
                         });

        lyrics.lines = std::move(timed);
        lyrics.synced = true;
    }
    else
    {
        // Nothing was timed, so the bracketed lines were metadata and the rest
        // is plain text that happens to live in a `.lrc` file.
        for (const QString &words : std::as_const(untimed))
            lyrics.lines.append({-1.0, words});
    }

    return lyrics;
}

Lyrics Lyrics::parsePlain(const QString &text)
{
    Lyrics lyrics;

    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &raw : rawLines)
    {
        const QString words = raw.trimmed();
        if (!words.isEmpty())
            lyrics.lines.append({-1.0, words});
    }

    return lyrics;
}

Lyrics Lyrics::parse(const QString &text)
{
    Lyrics lyrics = parseLrc(text);
    if (!lyrics.synced)
        lyrics = parsePlain(text);

    return lyrics;
}

int Lyrics::indexAt(double position) const
{
    if (!synced || position < 0.0 || lines.isEmpty())
        return -1;

    // The first line that starts after `position`, minus one: the line being
    // sung. Binary search, because this runs four times a second for the whole
    // of a track.
    const auto after = std::upper_bound(
        lines.cbegin(), lines.cend(), position,
        [](double seconds, const LyricLine &line) { return seconds < line.time; });

    if (after == lines.cbegin())
        return -1;

    return static_cast<int>(after - lines.cbegin()) - 1;
}
