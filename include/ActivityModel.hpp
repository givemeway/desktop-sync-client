#pragma once
#include "Utility.hpp"
#include "qhash.h"
#include "qnamespace.h"
#include "qstringview.h"
#include "qtmetamacros.h"
#include "types.hpp"
#include <QAbstractListModel>

namespace sync_app {

class ActivityModel : public QAbstractListModel {

  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole,
    PercentageRole,
    PathRole,
    MetaRole,
    TypeRole,
    StatusRole,
    ProgressRole,
    SizeRole
  };
  explicit ActivityModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

public slots:
  void onActivityUpdated(const std::string &key, const SyncItem &item);
  void onActivityAdded(const std::string &key, const SyncItem &item);
  void onActivityRemoved(const std::string &key);

private:
  std::vector<ActivityItem> m_items;
  QHash<QString, int> m_indexMap;
  int findByKey(const QString &key) const;
  void rebuildIndexMap();

  static QString resolveStatus(bool inQueue, bool isActive, bool isDone,
                               bool isError) {

    if (isDone)
      return Utility::toQ(activityToString(ActivityStatus::DONE));
    if (isActive)
      return Utility::toQ(activityToString(ActivityStatus::SYNCING));
    if (inQueue)
      return Utility::toQ(activityToString(ActivityStatus::QUEUED));
    if (isError)
      return Utility::toQ(activityToString(ActivityStatus::ERROR));
    return Utility::toQ(activityToString(ActivityStatus::UNKNOWN));
  }
};

} // namespace sync_app
