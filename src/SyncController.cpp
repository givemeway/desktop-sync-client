#include "SyncController.hpp"
#include <QDebug>

namespace sync_app {

SyncController::SyncController(CloudSyncWorker& engine, QObject *parent)
    : QObject(parent), m_engine(engine), m_status("Initializing..."), m_progress(0.0) {
    // In a real implementation, we would connect signals from m_engine to our refresh slot
}

QString SyncController::status() const {
    return m_status;
}

double SyncController::progress() const {
    return m_progress;
}

QVariantList SyncController::recentActivity() const {
    return m_activity;
}

void SyncController::refresh() {
    // Pull latest data from engine without modifying it
    // m_status = m_engine.getCurrentStatus(); 
    emit statusChanged();
    emit progressChanged();
    emit activityChanged();
}

void SyncController::startSync() {
    // m_engine.start();
    m_status = "Syncing...";
    emit statusChanged();
}

void SyncController::stopSync() {
    // m_engine.stop();
    m_status = "Stopped";
    emit statusChanged();
}

} // namespace sync_app
