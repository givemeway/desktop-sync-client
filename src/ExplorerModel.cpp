#include "ExplorerModel.hpp"
#include "Utility.hpp"
#include "qabstractitemmodel.h"
#include "types.hpp"
#include <qobject.h>

namespace sync_app {

ExplorerModel::ExplorerModel(QObject *parent) : QAbstractListModel(parent) {}

// ── QAbstractListModel overrides ──────────────────────────────────────────────

int ExplorerModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return static_cast<int>(m_items.size());
}

QVariant ExplorerModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= (int)m_items.size())
    return {};

  const ExplorerItem &item = m_items[index.row()];

  switch (role) {
  case IdRole:
    return item.id;
  case NameRole:
    return item.name;
  case TypeRole:
    return item.type;
  case SizeRole:
    return item.size;
  case VersionRole:
    return item.versions;
  case LastModifiedRole:
    return item.lastModified;
  case PathRole:
    return item.path;
  case IsSelectedRole:
    return item.isSelected;
  default:
    return {};
  }
}

QHash<int, QByteArray> ExplorerModel::roleNames() const {
  return {
      {IdRole,           "id"},
      {NameRole,         "name"},
      {TypeRole,         "type"},
      {SizeRole,         "size"},
      {VersionRole,      "versions"},
      {LastModifiedRole, "lastModified"},
      {PathRole,         "path"},
      {IsSelectedRole,   "isSelected"},
  };
}

// ── populate ──────────────────────────────────────────────────────────────────
// Called by SyncController when a directory fetch completes.
// Replaces the entire model in one shot — QML ListView re-renders cleanly.
// Uses beginResetModel/endResetModel rather than row-by-row inserts because
// we are replacing ALL content, not appending.

void ExplorerModel::populate(const std::vector<ExplorerItem> &items) {
  beginResetModel();
  m_items.clear();
  m_indexMap.clear();
  for (int i = 0; i < (int)items.size(); i++) {
    m_indexMap[items[i].id] = i;
    m_items.push_back(items[i]);
  }
  endResetModel();
}

// ── Selection ─────────────────────────────────────────────────────────────────

void ExplorerModel::setSelected(const QString &id, bool selected) {
  int row = findByKey(id);
  if (row == -1)
    return;

  m_items[row].isSelected = selected;
  QModelIndex idx = index(row);
  // only notify IsSelectedRole so QML only re-renders the checkbox,
  // not the entire row
  emit dataChanged(idx, idx, {IsSelectedRole});
}

void ExplorerModel::selectAll() {
  if (m_items.empty())
    return;

  // if all are already selected, deselect all — toggle behaviour
  bool allSelected = std::all_of(m_items.begin(), m_items.end(),
                                 [](const ExplorerItem &i) { return i.isSelected; });

  for (auto &item : m_items) {
    item.isSelected = !allSelected;
  }

  // notify entire range in one call — more efficient than per-row emit
  emit dataChanged(index(0), index((int)m_items.size() - 1), {IsSelectedRole});
}

QStringList ExplorerModel::selectedIds() const {
  QStringList ids;
  for (const auto &item : m_items) {
    if (item.isSelected)
      ids.append(item.id);
  }
  return ids;
}

// ── Incremental slot updates ──────────────────────────────────────────────────
// These are for live changes after the initial populate —
// e.g. a file gets deleted while the user is browsing

void ExplorerModel::onRowAdded(const std::string &key,
                               const ExplorerItem &item) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);

  if (row == -1) {
    // new item — append
    beginInsertRows({}, m_items.size(), m_items.size());
    m_indexMap[qkey] = m_items.size();
    m_items.push_back(item);
    endInsertRows();
  } else {
    // already exists — update in place
    m_items[row] = item;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
  }
}

void ExplorerModel::onRowUpdated(const std::string &key,
                                 const ExplorerItem &item) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);

  if (row == -1)
    return; // nothing to update

  m_items[row] = item;
  QModelIndex idx = index(row);
  emit dataChanged(idx, idx);
}

void ExplorerModel::onRowRemoved(const std::string &key) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);

  if (row < 0)
    return;

  beginRemoveRows({}, row, row);
  m_items.erase(m_items.begin() + row);
  endRemoveRows();

  // rebuild index map since all rows after the removed one have shifted
  rebuildIndexMap();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

int ExplorerModel::findByKey(const QString &key) const {
  auto it = m_indexMap.find(key);
  return (it != m_indexMap.end()) ? it.value() : -1;
}

void ExplorerModel::rebuildIndexMap() {
  m_indexMap.clear();
  for (int i = 0; i < (int)m_items.size(); i++)
    m_indexMap[m_items[i].id] = i;
}

} // namespace sync_app
