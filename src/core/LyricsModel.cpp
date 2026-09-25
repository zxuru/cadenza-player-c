#include "LyricsModel.h"

LyricsModel::LyricsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void LyricsModel::setLyrics(const Lyrics &lyrics)
{
    beginResetModel();
    m_lyrics = lyrics;
    m_active = -1;
    endResetModel();

    emit lyricsChanged();
    emit activeIndexChanged();
}

void LyricsModel::clear()
{
    setLoading(false);

    if (m_lyrics.lines.isEmpty() && m_lyrics.source == Lyrics::Source::None)
        return;

    beginResetModel();
    m_lyrics = Lyrics();
    m_active = -1;
    endResetModel();

    emit lyricsChanged();
    emit activeIndexChanged();
}

void LyricsModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;

    m_loading = loading;
    emit loadingChanged();
}

void LyricsModel::setPosition(double seconds)
{
    if (!m_lyrics.synced)
        return;

    setActiveIndex(m_lyrics.indexAt(seconds));
}

void LyricsModel::setActiveIndex(int index)
{
    if (m_active == index)
        return;

    m_active = index;
    emit activeIndexChanged();
}

QString LyricsModel::sourceKey() const
{
    switch (m_lyrics.source)
    {
    case Lyrics::Source::Sidecar:
        return QStringLiteral("lyrics_source_file");
    case Lyrics::Source::Embedded:
        return QStringLiteral("lyrics_source_tags");
    case Lyrics::Source::Online:
        return QStringLiteral("lyrics_source_online");
    case Lyrics::Source::None:
        break;
    }

    return QString();
}

int LyricsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_lyrics.lines.size();
}

QVariant LyricsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lyrics.lines.size())
        return QVariant();

    const LyricLine &line = m_lyrics.lines.at(index.row());

    switch (role)
    {
    case TextRole:
        return line.text;
    case TimeRole:
        return line.time;
    default:
        break;
    }

    return QVariant();
}

QHash<int, QByteArray> LyricsModel::roleNames() const
{
    return {
        {TextRole, QByteArrayLiteral("text")},
        {TimeRole, QByteArrayLiteral("time")},
    };
}

double LyricsModel::timeAt(int row) const
{
    if (row < 0 || row >= m_lyrics.lines.size())
        return -1.0;

    return m_lyrics.lines.at(row).time;
}
