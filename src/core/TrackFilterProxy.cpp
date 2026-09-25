#include "TrackFilterProxy.h"

#include "TrackModel.h"

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QVariant>

TrackFilterProxy::TrackFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void TrackFilterProxy::setQuery(const QString &query)
{
    if (m_query == query)
        return;

    // A Spanish-language library is full of accented titles and nobody types
    // accents into a search box, so both sides are folded.
    const QString folded = TrackModel::normalise(query);

    changeFilter([&] {
        m_query = query;
        m_normalisedQuery = folded;
    });
}

void TrackFilterProxy::setField(TrackModel::Field field)
{
    if (m_field == field)
        return;

    changeFilter([&] { m_field = field; });
}

void TrackFilterProxy::setFolder(const QString &folder)
{
    if (m_folder == folder)
        return;

    const QString prefix = folder.isEmpty() ? QString() : folder + QLatin1Char('/');

    changeFilter([&] {
        m_folder = folder;
        m_folderPrefix = prefix;
    });
}

void TrackFilterProxy::setGroup(TrackModel::Field field, const QString &key)
{
    if (m_grouped && m_groupField == field && m_groupKey == key)
        return;

    changeFilter([&] {
        m_grouped = true;
        m_groupField = field;
        m_groupKey = key;
    });
}

void TrackFilterProxy::clearGroup()
{
    if (!m_grouped)
        return;

    changeFilter([&] {
        m_grouped = false;
        m_groupField = TrackModel::AnyField;
        m_groupKey.clear();
    });
}

QString TrackFilterProxy::pathAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return QString();
    return index(row, 0).data(TrackModel::PathRole).toString();
}

QStringList TrackFilterProxy::paths() const
{
    const int rows = rowCount();

    QStringList result;
    result.reserve(rows);
    for (int row = 0; row < rows; ++row)
        result.append(pathAt(row));
    return result;
}

bool TrackFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    // The library is a flat list: every row has the same parent, which is what
    // the unnamed second parameter is.
    return acceptsSourceRow(sourceRow);
}

bool TrackFilterProxy::acceptsSourceRow(int row) const
{
    if (m_normalisedQuery.isEmpty() && m_folderPrefix.isEmpty() && !m_grouped)
        return true;

    // The library model, which is the only source this proxy is ever given: the
    // folded text and the group keys live there, built once per load rather
    // than per keystroke.
    const auto *model = static_cast<const TrackModel *>(sourceModel());
    if (model == nullptr || row < 0 || row >= model->rowCount())
        return false;

    if (!m_folderPrefix.isEmpty()
        && !model->tracks().at(row).path.startsWith(m_folderPrefix))
    {
        return false;
    }

    if (m_grouped && model->groupKey(row, m_groupField) != m_groupKey)
        return false;

    return m_normalisedQuery.isEmpty() || model->rowMatches(row, m_field, m_normalisedQuery);
}
