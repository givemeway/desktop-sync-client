#pragma once

#include "qabstractitemmodel.h"
#include "qhash.h"
#include "qtmetamacros.h"
#include "types.hpp"
#include <QStringList>
#include <qcontainerfwd.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qvariant.h>

namespace sync_app {

class ExplorerModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole,
    TypeRole,
    SizeRole,
    VersionRole,
    LastModifiedRole,
    PathRole,
    IsSelectedRole  // checkbox state
  };

  explicit ExplorerModel(QObject *parent = nullptr);

  // ── QAbstractListModel overrides ──────────────────────────────────────────
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // ── Called by SyncController after background fetch completes ─────────────
  // Replaces entire model contents — used when navigating to a new directory
  void populate(const std::vector<ExplorerItem> &items);

  // ── Selection — called by SyncController from QML invokables ─────────────
  void setSelected(const QString &id, bool selected);
  void selectAll();
  QStringList selectedIds() const;

public slots:
  // ── Incremental updates — used for live changes after initial load ─────────
  void onRowAdded(const std::string &key, const ExplorerItem &item);
  void onRowUpdated(const std::string &key, const ExplorerItem &item);
  void onRowRemoved(const std::string &key);

private:
  std::vector<ExplorerItem> m_items;
  QHash<QString, int> m_indexMap;

  int findByKey(const QString &key) const;
  void rebuildIndexMap();
};

} // namespace sync_app
