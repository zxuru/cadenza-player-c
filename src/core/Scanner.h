#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

/// Filesystem walk + tag extraction, writing into the same SQLite index that
/// `Library` reads.
///
/// `run()` is blocking by design: it is meant to be pushed onto a worker with
/// `QtConcurrent::run`, which keeps SQLite access confined to that one thread
/// and leaves the GUI thread free to keep painting.
///
/// Rescans are incremental. A file is re-probed only when its `(mtime, size)`
/// pair differs from the indexed one, and rows whose file has vanished are
/// pruned. Files that cannot be parsed are skipped rather than aborting the
/// walk -- a lying extension should not cost you the rest of the album.
class Scanner : public QObject
{
    Q_OBJECT

public:
    explicit Scanner(QString databasePath, QObject *parent = nullptr);

    /// Indexes `roots` recursively. Returns how many files were re-read.
    /// Emits `progress()` as it goes and `finished()` when done.
    int run(const QStringList &roots);

    /// Rows the last `run()` pruned because their file is gone. Together with
    /// its return value -- the files it re-read -- this is what tells a caller
    /// whether the index moved at all, and therefore whether the library is
    /// worth reading back.
    [[nodiscard]] int removed() const { return m_removed; }

    /// Asks a running scan to stop at the next file boundary.
    void cancel();

    /// True once `cancel()` has been called.
    [[nodiscard]] bool isCancelled() const { return m_cancelled.load(); }

signals:
    /// Emitted from the worker thread. `indexed` is the running count of files
    /// re-read so far; `title` is the track that just finished.
    void progress(int indexed, const QString &title);

    /// Emitted from the worker thread when the walk completes.
    void finished(int indexed, int removed);

private:
    QString m_databasePath;
    /// Written on the worker thread and read after the future has finished,
    /// which is the only time it is read.
    int m_removed = 0;
    std::atomic_bool m_cancelled{false};
};
