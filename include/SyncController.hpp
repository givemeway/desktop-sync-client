#ifndef SYNCCONTROLLER_HPP
#define SYNCCONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QVariantList>
#include "CloudSyncWorker.hpp"

namespace sync_app {

class SyncController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantList recentActivity READ recentActivity NOTIFY activityChanged)

public:
    explicit SyncController(CloudSyncWorker& engine, QObject *parent = nullptr);

    QString status() const;
    double progress() const;
    QVariantList recentActivity() const;

signals:
    void statusChanged();
    void progressChanged();
    void activityChanged();

public slots:
    void refresh();
    void startSync();
    void stopSync();

private:
    CloudSyncWorker& m_engine;
    QString m_status;
    double m_progress;
    QVariantList m_activity;
};

} // namespace sync_app

#endif // SYNCCONTROLLER_HPP
