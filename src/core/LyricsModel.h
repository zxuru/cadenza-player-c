#pragma once

#include "Lyrics.h"

#include <QAbstractListModel>

/// The lyrics of the track that is playing, as a model the interface can bind
/// a list view to, plus the index of the line the playhead is inside.
///
/// Only the lines matter to the view -- the track's lyrics are held here as
/// the one copy -- so the model is replaced wholesale when a track changes
/// rather than patched.
class LyricsModel : public QAbstractListModel
{
    Q_OBJECT

    /// Index of the line being sung, or -1 for unsynced lyrics, for the intro
    /// before the first line and when nothing is loaded.
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeIndexChanged)
    Q_PROPERTY(bool hasLyrics READ hasLyrics NOTIFY lyricsChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY lyricsChanged)
    Q_PROPERTY(bool instrumental READ instrumental NOTIFY lyricsChanged)
    /// Key of the short label for where the lyrics came from, empty when
    /// there are none. The pane renders it, so it says where they came from in
    /// the language the rest of the interface is in.
    Q_PROPERTY(QString sourceKey READ sourceKey NOTIFY lyricsChanged)
    /// True while a lookup is in flight, so the pane can say so instead of
    /// claiming there are no lyrics.
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    /// Lines held, so the pane can test for an empty result without walking
    /// the model.
    Q_PROPERTY(int count READ count NOTIFY lyricsChanged)

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        /// Start of the line in seconds, or -1 when unsynced.
        TimeRole,
    };
    Q_ENUM(Role)

    explicit LyricsModel(QObject *parent = nullptr);

    void setLyrics(const Lyrics &lyrics);
    void clear();
    void setLoading(bool loading);

    /// Moves the active line to whatever contains `seconds`. Cheap enough to
    /// call on every position tick: one binary search, and no signal at all
    /// unless the line changed.
    void setPosition(double seconds);

    [[nodiscard]] int activeIndex() const { return m_active; }
    [[nodiscard]] int count() const { return m_lyrics.lines.size(); }
    [[nodiscard]] bool hasLyrics() const { return !m_lyrics.lines.isEmpty(); }
    [[nodiscard]] bool synced() const { return m_lyrics.synced; }
    [[nodiscard]] bool instrumental() const { return m_lyrics.instrumental; }
    [[nodiscard]] QString sourceKey() const;
    [[nodiscard]] bool loading() const { return m_loading; }

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Start of row `row` in seconds, or -1 when there is no such line or the
    /// lyrics are unsynced.
    [[nodiscard]] Q_INVOKABLE double timeAt(int row) const;

signals:
    void lyricsChanged();
    void activeIndexChanged();
    void loadingChanged();

private:
    void setActiveIndex(int index);

    Lyrics m_lyrics;
    int m_active = -1;
    bool m_loading = false;
};
