#include "DownloadModel.h"

#include <algorithm>
#include <utility>

namespace {

/// True while a row is in the download queue or in flight, which is the only
/// state a new search must not walk over.
bool isBusy(DownloadItem::State state)
{
    return state == DownloadItem::State::Waiting || state == DownloadItem::State::Working;
}

}  // namespace

DownloadModel::DownloadModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void DownloadModel::setItems(QVector<DownloadItem> items)
{
    // Rows that are downloading survive a new search: the new results may not
    // contain the item at all, and dropping its row would leave a transfer
    // running with nothing on screen to cancel it. They go first, so the thing
    // that is happening is the thing at the top.
    QVector<DownloadItem> merged;
    merged.reserve(items.size() + m_items.size());

    for (const DownloadItem &existing : std::as_const(m_items))
    {
        if (!isBusy(existing.state))
            continue;

        const bool listed = std::any_of(items.cbegin(), items.cend(),
                                        [&existing](const DownloadItem &item) { return item.id == existing.id; });
        if (!listed)
            merged.append(existing);
    }

    for (DownloadItem &item : items)
    {
        const int row = rowForId(item.id);
        if (row < 0)
        {
            merged.append(item);
            continue;
        }

        // The same item, found again: it is the same download, and a row that
        // is already in the library or already failed should say so.
        const DownloadItem &existing = m_items.at(row);
        if (existing.state != DownloadItem::State::Idle)
        {
            item.state = existing.state;
            item.progress = existing.progress;
            item.detail = existing.detail;
            item.fileCount = existing.fileCount;
        }

        merged.append(item);
    }

    beginResetModel();
    m_items = std::move(merged);
    m_rowById.clear();
    m_rowById.reserve(m_items.size());
    for (int row = 0; row < m_items.size(); ++row)
        m_rowById.insert(m_items.at(row).id, row);
    endResetModel();
}

void DownloadModel::clear()
{
    if (m_items.isEmpty())
        return;

    beginResetModel();
    m_items.clear();
    m_rowById.clear();
    endResetModel();
}

int DownloadModel::rowForId(const QString &id) const
{
    if (id.isEmpty())
        return -1;

    return m_rowById.value(id, -1);
}

void DownloadModel::setState(int row, DownloadItem::State state, const Message &detail)
{
    if (row < 0 || row >= m_items.size())
        return;

    DownloadItem &item = m_items[row];
    if (item.state == state && item.detail == detail)
        return;

    item.state = state;
    item.detail = detail;
    const QModelIndex index = this->index(row, 0);
    emit dataChanged(index, index,
                     {WorkingRole, WaitingRole, DoneRole, FailedRole, DetailKeyRole,
                      DetailValuesRole});
}

void DownloadModel::setProgress(int row, double progress)
{
    if (row < 0 || row >= m_items.size())
        return;

    const double bounded = std::clamp(progress, 0.0, 1.0);
    if (qFuzzyCompare(m_items.at(row).progress, bounded))
        return;

    m_items[row].progress = bounded;
    const QModelIndex index = this->index(row, 0);
    emit dataChanged(index, index, {ProgressRole});
}

void DownloadModel::setFileCount(int row, int fileCount)
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).fileCount == fileCount)
        return;

    m_items[row].fileCount = fileCount;
}

int DownloadModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant DownloadModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const DownloadItem &item = m_items.at(index.row());
    const DownloadItem::State state = item.state;

    switch (role)
    {
    case IdRole:
        return item.id;
    case TitleRole:
        return item.title;
    case ArtistRole:
        return item.artist;
    case YearRole:
        return item.year;
    case ThumbnailRole:
        return item.thumbnail;
    case WorkingRole:
        return state == DownloadItem::State::Working;
    case WaitingRole:
        return state == DownloadItem::State::Waiting;
    case DoneRole:
        return state == DownloadItem::State::Done;
    case FailedRole:
        return state == DownloadItem::State::Failed;
    case ProgressRole:
        return item.progress;
    case DetailKeyRole:
        return item.detail.key;
    case DetailValuesRole:
        return item.detail.values;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> DownloadModel::roleNames() const
{
    return {
        // `itemId`, not `id`: a property called `id` is not one QML will take.
        {IdRole, "itemId"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {YearRole, "year"},
        {ThumbnailRole, "thumbnail"},
        {WorkingRole, "working"},
        {WaitingRole, "waiting"},
        {DoneRole, "done"},
        {FailedRole, "failed"},
        {ProgressRole, "progress"},
        {DetailKeyRole, "detailKey"},
        {DetailValuesRole, "detailValues"},
    };
}
