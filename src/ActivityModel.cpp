#include "ActivityModel.hpp"
#include "Utility.hpp"
#include "qabstractitemmodel.h"
#include "qhash.h"
#include "qobject.h"
#include "qstringview.h"
#include "qvariant.h"
#include "types.hpp"
#include <iomanip>

namespace sync_app {

ActivityModel::ActivityModel(QObject *parent) : QAbstractListModel(parent) {}

int ActivityModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return static_cast<int>(m_items.size());
}

QVariant ActivityModel::data(const QModelIndex &index, int role) const {

  if (!index.isValid() || index.row() >= (int)m_items.size())
    return {};

  const ActivityItem &item = m_items[index.row()];

  switch (role) {
  case IdRole:
    return item.id;
  case NameRole:
    return item.name;
  case PathRole:
    return item.path;
  case MetaRole:
    return item.meta;
  case TypeRole:
    return item.type;
  case StatusRole:
    return item.status;
  case ProgressRole:
    return item.progress;
  case SizeRole:
    return item.size;
  default:
    return {};
  }
}

QHash<int, QByteArray> ActivityModel::roleNames() const {

  return {{IdRole, "id"},
          {NameRole, "name"},
          {PathRole, "path"},
          {MetaRole, "meta"},
          {TypeRole, "type"},
          {StatusRole, "status"},
          {ProgressRole, "progress"},
          {SizeRole, "size"}};
}

void ActivityModel::onActivityUpdated(const std::string &key,
                                      const SyncItem &dp) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);
  std::string size;

  ActivityItem item;
  item.id = qkey;
  item.name = Utility::toQ(dp.name);
  item.path = Utility::toQ(dp.path);
  item.meta = Utility::toQ(dp.meta);
  item.type = Utility::toQ(dp.type);
  item.progress = dp.progress;
  size = Utility::formatFileSize(dp.size);
  item.size = Utility::toQ(size);

  auto status = resolveStatus(dp.inQueue, dp.isActive, dp.isDone, dp.isError);
  if (status == "syncing" && item.meta != "folder") {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << dp.progress;
    item.status = Utility::toQ(ss.str() + "%");
  } else {
    item.status = status;
  }

  if (row == -1) {
    // New item — insert row
    beginInsertRows({}, m_items.size(), m_items.size());
    m_indexMap[qkey] = m_items.size();
    m_items.push_back(item);
    endInsertRows();
  } else {
    // Existing item — update in place
    m_items[row] = item;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
  }
}

void ActivityModel::onActivityAdded(const std::string &key,
                                    const SyncItem &up) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);
  std::string size;
  ActivityItem item;
  item.id = qkey;
  item.name = Utility::toQ(up.name);
  item.path = Utility::toQ(up.path);
  item.meta = Utility::toQ(up.meta);
  item.type = Utility::toQ(up.type);
  item.progress = up.progress;
  size = Utility::formatFileSize(up.size);
  item.size = Utility::toQ(size);
  item.status = resolveStatus(up.inQueue, up.isActive, up.isDone, up.isError);

  if (row == -1) {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_indexMap[qkey] = m_items.size();
    m_items.push_back(item);
    endInsertRows();
  } else {
    m_items[row] = item;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
  }
}

void ActivityModel::onActivityRemoved(const std::string &key) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);
  if (row < 0)
    return;

  beginRemoveRows({}, row, row);
  m_items.erase(m_items.begin() + row);
  endRemoveRows();

  rebuildIndexMap();
}

// ── Helpers

int ActivityModel::findByKey(const QString &key) const {
  auto it = m_indexMap.find(key);
  return (it != m_indexMap.end()) ? it.value() : -1;
}

void ActivityModel::rebuildIndexMap() {
  m_indexMap.clear();
  for (int i = 0; i < (int)m_items.size(); i++)
    m_indexMap[m_items[i].id] = i;
}
}; // namespace sync_app
