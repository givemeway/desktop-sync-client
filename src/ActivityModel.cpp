#include "ActivityModel.hpp"
#include "Utility.hpp"
#include "qabstractitemmodel.h"
#include "qhash.h"
#include "qobject.h"
#include "qstringview.h"
#include "qvariant.h"
#include "types.hpp"
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <qdatetime.h>

namespace sync_app {

ActivityModel::ActivityModel(QObject *parent) : QAbstractListModel(parent) {}

int ActivityModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return static_cast<int>(m_items.size());
}

static QString groupFromTimestamp(const QString &unixString) {
  bool ok = false;
  qint64 secs = unixString.toLongLong(&ok);
  if (!ok)
    return "OLDER";

  QDateTime itemTime = QDateTime::fromSecsSinceEpoch(secs);
  QDate itemDate = itemTime.date();
  QDate today = QDate::currentDate();

  int days = itemDate.daysTo(today);

  if (days == 0)
    return "TODAY";
  if (days <= 1)
    return "YESTERDAY";
  if (days > 1 && days <= 7)
    return "LAST 7 DAYS";
  if (days <= 30)
    return "LAST 30 DAYS";
  return "OLDER";
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
  case PercentageRole:
    return item.percentage;
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
  case UpdatedRole:
    return item.lastUpdated;
  case GroupRole:
    return groupFromTimestamp(item.lastUpdated);
  default:
    return {};
  }
}

QHash<int, QByteArray> ActivityModel::roleNames() const {

  return {{IdRole, "id"},
          {NameRole, "name"},
          {PercentageRole, "percentage"},
          {PathRole, "path"},
          {MetaRole, "meta"},
          {TypeRole, "type"},
          {StatusRole, "status"},
          {ProgressRole, "progress"},
          {SizeRole, "size"},
          {UpdatedRole, "lastUpdated"},
          {GroupRole, "group"}};
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
  item.lastUpdated = Utility::toQ(dp.lastUpdated);
  item.progress = dp.progress / 100;
  size = Utility::formatFileSize(dp.size);
  item.size = Utility::toQ(size);

  auto status = resolveStatus(dp.inQueue, dp.isActive, dp.isDone, dp.isError);
  if (dp.meta != "folder") {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << dp.progress;
    item.percentage = Utility::toQ(ss.str() + "%");
    if (item.progress >= 1.0) {
      item.percentage = "Finalizing..";
    }
  }
  item.status = status;
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
  if (dp.isDone) {
    if (m_filesSyncing > 0) {
      --m_filesSyncing;
    }
    emit filesSyncingChanged(m_filesSyncing);
  }
}

void ActivityModel::onActivityAdded(const std::string &key, const SyncItem &up,
                                    bool isDBActivity) {
  QString qkey = Utility::toQ(key);
  int row = findByKey(qkey);
  std::string size;
  ActivityItem item;
  item.id = qkey;
  item.name = Utility::toQ(up.name);
  item.path = Utility::toQ(up.path);
  item.meta = Utility::toQ(up.meta);
  item.type = Utility::toQ(up.type);
  item.lastUpdated = Utility::toQ(up.lastUpdated);
  item.progress = up.progress;
  size = Utility::formatFileSize(up.size);
  item.size = Utility::toQ(size);
  item.status = resolveStatus(up.inQueue, up.isActive, up.isDone, up.isError);

  if (!isDBActivity) {
    ++m_filesSyncing;
    emit filesSyncingChanged(m_filesSyncing);
  }

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

void ActivityModel::loadActivities(const std::vector<SyncItem> &activities) {
  beginResetModel();
  m_items.clear();
  m_indexMap.clear();
  for (int i = 0; i < (int)activities.size(); i++) {
    QString qkey = Utility::toQ(activities[i].id);
    std::string size;
    ActivityItem item;
    item.id = qkey;
    item.name = Utility::toQ(activities[i].name);
    item.path = Utility::toQ(activities[i].path);
    item.meta = Utility::toQ(activities[i].meta);
    item.type = Utility::toQ(activities[i].type);
    item.lastUpdated = Utility::toQ(activities[i].lastUpdated);
    item.progress = activities[i].progress;
    size = Utility::formatFileSize(activities[i].size);
    item.size = Utility::toQ(size);
    item.status = resolveStatus(activities[i].inQueue, activities[i].isActive,
                                activities[i].isDone, activities[i].isError);
    m_indexMap[qkey] = i;
    m_items.push_back(item);
  }
  endResetModel();
  /*for (const auto &activity[i] : activities) {
    std::cout << "[activityModel] activity added: " << activity.id << " | "
              << activity[i].name << std::endl;
    emit onactivity[i] Added(activity.id, activity, true);
  }
  std::cout << "[activityModel] activities populated: " << m_items.size()
            << " | " << m_indexMap.size() << std::endl;
  */
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
